#include <Arduino.h>
#include "AudioKit.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Global State
int current_volume = 80;
int current_waveform = 0; // 0: Triangle, 1: Square, 2: Saw
int last_note = -1;

// Define UART pins for MIDI communication
// Adjust these pins according to your specific wiring and AudioKit board version
#define MIDI_RX_PIN 22
#define MIDI_TX_PIN 23

AudioKit kit;

// Synth Parameters
int sample_rate = 44100;
float frequency = 440.0;
bool note_on = false;

// Oscillator State
float phase = 0.0f;
float phase_inc = 0.0f;

void updatePhaseInc() {
    phase_inc = (frequency * 2.0f) / (float)sample_rate;
}

// Simple MIDI Parser
int midi_state = 0; // 0: status, 1: data1, 2: data2
uint8_t status_byte = 0;
uint8_t data1_byte = 0;

void handleMidiMessage(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t cmd = status & 0xF0;

    if (cmd == 0x90) { // Note On
        if (d2 > 0) {
            float freq = 440.0f * pow(2.0f, (d1 - 69.0f) / 12.0f);
            frequency = freq;
            updatePhaseInc();
            note_on = true;
            last_note = d1;
        } else {
            // Note On with velocity 0 is Note Off
            note_on = false;
        }
    } else if (cmd == 0x80) { // Note Off
        note_on = false;
    } else if (cmd == 0xB0) { // Control Change
        if (d1 == 7) { // Volume
             current_volume = map(d2, 0, 127, 0, 100);
             kit.setVolume(current_volume);
        } else if (d1 == 70) { // Waveform
             // Map 0-127 to 0-2
             if (d2 < 42) current_waveform = 0;
             else if (d2 < 84) current_waveform = 1;
             else current_waveform = 2;
        }
    }
}

void displayTask(void *parameter) {
    // Try to initialize OLED
    // Note: AudioKit likely initializes Wire. If this fails, we might need to check pins.
    // We pass false as the last argument to prevent re-initializing Wire, which would break the Codec I2C.
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)) {
        Serial.println(F("SSD1306 allocation failed"));
        // Just loop forever or delete task
        vTaskDelete(NULL);
    }

    display.clearDisplay();
    display.display();

    for(;;) {
        display.clearDisplay();

        // Title
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println(F("ESP32 Synth"));

        // Volume
        display.setCursor(0, 12);
        display.print(F("Vol: "));
        display.print(current_volume);

        // Waveform
        display.setCursor(0, 24);
        display.print(F("Wave: "));
        switch(current_waveform) {
            case 0: display.print(F("Triangle")); break;
            case 1: display.print(F("Square")); break;
            case 2: display.print(F("Saw")); break;
            default: display.print(F("Unknown")); break;
        }

        // Note Info
        display.setCursor(0, 36);
        display.print(F("Note: "));
        if (last_note != -1) {
            display.print(last_note);
        } else {
            display.print(F("--"));
        }

        if (note_on) {
            display.setCursor(70, 36);
            display.print(F("*ON*"));
        }

        // Instructions
        display.setCursor(0, 52);
        display.print(F("CC7:Vol CC70:Wave"));

        display.display();

        // Update at ~10 FPS
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

    // AudioKit setup
    auto cfg = kit.defaultConfig();
    cfg.sample_rate = sample_rate;
    cfg.channels = 2; // Stereo output
    cfg.bits_per_sample = 16;

    // Attempt to auto-detect or use default board
    // Uncomment the following line if auto-detect fails for v2.2
    kit.setBoard(AudioKitBoard::AI_THINKER_V2_2);

    kit.begin(cfg);
    kit.setVolume(current_volume);

    // Create Display Task on Core 0
    xTaskCreatePinnedToCore(
        displayTask,
        "Display Task",
        4096, // Stack size
        NULL,
        1,    // Priority
        NULL,
        0     // Core 0
    );

    updatePhaseInc();
    Serial.println("Audio Synth Ready. Waiting for MIDI...");
}

void loop() {
    // 1. Process MIDI
    // Read all available bytes to prevent buffer overflow
    while (Serial2.available()) {
        uint8_t b = Serial2.read();

        if (b >= 0x80) { // Status byte
            status_byte = b;
            midi_state = 1;
        } else if (midi_state == 1) {
            data1_byte = b;
            midi_state = 2;
        } else if (midi_state == 2) {
            handleMidiMessage(status_byte, data1_byte, b);
            midi_state = 0;
            // Note: This simple parser expects a Status byte for every message
            // or we need to implement Running Status if the sender uses it.
            // We'll ensure the S3 sender always sends status bytes.
        }
    }

    // 2. Generate Audio
    // Write a small chunk to keep latency low but efficient
    const int SAMPLES_PER_CHUNK = 64;
    int16_t stereo_buffer[SAMPLES_PER_CHUNK * 2];

    for (int i = 0; i < SAMPLES_PER_CHUNK; i++) {
        int16_t sample = 0;

        if (note_on) {
            float sample_f = 0.0f;

            if (current_waveform == 0) { // Triangle
                if (phase < 1.0f) {
                    sample_f = -1.0f + 2.0f * phase;
                } else {
                    sample_f = 1.0f - 2.0f * (phase - 1.0f);
                }
            } else if (current_waveform == 1) { // Square
                if (phase < 1.0f) sample_f = -1.0f;
                else sample_f = 1.0f;
            } else if (current_waveform == 2) { // Sawtooth
                sample_f = -1.0f + phase; // 0..2 -> -1..1
            }

            // Advance phase
            phase += phase_inc;
            if (phase >= 2.0f) phase -= 2.0f;

            sample = (int16_t)(sample_f * 10000.0f);
        } else {
            // Silence - could implement decay here for better sound
            sample = 0;
            phase = 0.0f; // Reset phase on silence? Or keep free running.
            // Resetting phase avoids click on next attack if envelope is instant
        }

        stereo_buffer[i * 2] = sample;     // Left
        stereo_buffer[i * 2 + 1] = sample; // Right
    }

    kit.write((uint8_t*)stereo_buffer, sizeof(stereo_buffer));
}
