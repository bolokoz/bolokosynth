#include <Arduino.h>
#include "AudioKit.h"

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
        } else {
            // Note On with velocity 0 is Note Off
            note_on = false;
        }
    } else if (cmd == 0x80) { // Note Off
        note_on = false;
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
    kit.setVolume(80);

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
            // Triangle wave generation
            float sample_f = 0.0f;
            if (phase < 1.0f) {
                sample_f = -1.0f + 2.0f * phase;
            } else {
                sample_f = 1.0f - 2.0f * (phase - 1.0f);
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
