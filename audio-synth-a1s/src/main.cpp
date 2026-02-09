#include <Arduino.h>
#include "AudioKitHAL.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Synth.h"
#include "WebManager.h"

// --- OLED Settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- MIDI Pins ---
// Adjust these pins according to your specific wiring and AudioKit board version
#define MIDI_RX_PIN 22
#define MIDI_TX_PIN 23

// --- Objects ---
AudioKit kit;
Synth synth(44100);
WebManager webManager(&synth);

// --- Global State for Display ---
int last_note = -1;
bool is_note_on = false; // Mirror synth state for display "ON" indicator

// --- MIDI Parser State ---
int midi_state = 0; // 0: status, 1: data1, 2: data2
uint8_t status_byte = 0;
uint8_t data1_byte = 0;

void handleMidiMessage(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t cmd = status & 0xF0;
    uint8_t ch = status & 0x0F;

    // Filter by channel if configured
    int midi_channel = webManager.getMidiChannel();
    if (midi_channel != -1 && ch != midi_channel) {
        return;
    }

    if (cmd == 0x90) { // Note On
        if (d2 > 0) {
            float freq = 440.0f * pow(2.0f, (d1 - 69.0f) / 12.0f);
            synth.noteOn(freq);
            last_note = d1;
            is_note_on = true;
        } else {
            // Note On with velocity 0 is Note Off
            synth.noteOff();
            is_note_on = false;
        }
    } else if (cmd == 0x80) { // Note Off
        synth.noteOff();
        is_note_on = false;
    } else if (cmd == 0xB0) { // Control Change
        if (d1 == webManager.getCcVol()) { // Volume
             // Map 0-127 to 0-100
             int vol = map(d2, 0, 127, 0, 100);
             synth.setVolume(vol);
        } else if (d1 == webManager.getCcWave()) { // Waveform
             // Map 0-127 to 0-3 (Triangle, Sine, Square, Saw)
             int wave = 0;
             if (d2 < 32) wave = 0;
             else if (d2 < 64) wave = 1;
             else if (d2 < 96) wave = 2;
             else wave = 3;
             synth.setWaveform(wave);
        }
    }
}

void displayTask(void *parameter) {
    // Try to initialize OLED
    // We pass false as the last argument to prevent re-initializing Wire, which would break the Codec I2C.
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)) {
        Serial.println(F("SSD1306 allocation failed"));
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
        display.print(synth.getVolume());

        // Waveform
        display.setCursor(0, 24);
        display.print(F("Wave: "));
        switch(synth.getWaveform()) {
            case 0: display.print(F("Triangle")); break;
            case 1: display.print(F("Sine")); break;
            case 2: display.print(F("Square")); break;
            case 3: display.print(F("Sawtooth")); break;
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

        if (is_note_on) {
            display.setCursor(70, 36);
            display.print(F("*ON*"));
        }

        // Instructions
        display.setCursor(0, 52);
        display.print(F("CC"));
        display.print(webManager.getCcVol());
        display.print(F(":Vol CC"));
        display.print(webManager.getCcWave());
        display.print(F(":Wave"));

        display.display();

        // Update at ~10 FPS
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);

    Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

    // AudioKit setup
    auto cfg = kit.defaultConfig(KitOutput);
    cfg.sample_rate = AUDIO_HAL_44K_SAMPLES;
    cfg.bits_per_sample = AUDIO_HAL_BIT_LENGTH_16BITS;

    kit.begin(cfg);
    kit.setVolume(80); // Hardware volume fixed at 80, we do software scaling in Synth

    // Web/WiFi Setup
    webManager.begin();

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

    Serial.println("Audio Synth Ready. Waiting for MIDI...");
}

void loop() {
    // 0. Handle Web Server
    webManager.handle();

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
        }
    }

    // 2. Generate Audio
    // Write a small chunk to keep latency low but efficient
    const int SAMPLES_PER_CHUNK = 64;
    int16_t stereo_buffer[SAMPLES_PER_CHUNK * 2];

    for (int i = 0; i < SAMPLES_PER_CHUNK; i++) {
        int16_t sample = synth.getSample();
        stereo_buffer[i * 2] = sample;     // Left
        stereo_buffer[i * 2 + 1] = sample; // Right
    }

    kit.write((uint8_t*)stereo_buffer, sizeof(stereo_buffer));
}