#include <Arduino.h>
#include "AudioKit.h"
#include "Synth.h"

// Uncomment the following line to enable WiFi and Web Configuration
#define ENABLE_WIFI

#ifdef ENABLE_WIFI
#include "WebManager.h"
#endif

// Define UART pins for MIDI communication
// Adjust these pins according to your specific wiring and AudioKit board version
#define MIDI_RX_PIN 22
#define MIDI_TX_PIN 23

AudioKit kit;
Synth synth(44100);

#ifdef ENABLE_WIFI
WebManager webManager(&synth);
#endif

// Simple MIDI Parser
int midi_state = 0; // 0: status, 1: data1, 2: data2
uint8_t status_byte = 0;
uint8_t data1_byte = 0;

void handleMidiMessage(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t cmd = status & 0xF0;

    if (cmd == 0x90) { // Note On
        if (d2 > 0) {
            float freq = 440.0f * pow(2.0f, (d1 - 69.0f) / 12.0f);
            synth.noteOn(freq);
        } else {
            // Note On with velocity 0 is Note Off
            synth.noteOff();
        }
    } else if (cmd == 0x80) { // Note Off
        synth.noteOff();
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

    // AudioKit setup
    auto cfg = kit.defaultConfig();
    cfg.sample_rate = 44100;
    cfg.channels = 2; // Stereo output
    cfg.bits_per_sample = 16;

    // Attempt to auto-detect or use default board
    kit.setBoard(AudioKitBoard::AI_THINKER_V2_2);

    kit.begin(cfg);
    kit.setVolume(80); // Hardware volume fixed

    #ifdef ENABLE_WIFI
    webManager.begin();
    #endif

    Serial.println("Audio Synth Ready. Waiting for MIDI...");
}

void loop() {
    // 0. Handle Web Server
    #ifdef ENABLE_WIFI
    webManager.handle();
    #endif

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
