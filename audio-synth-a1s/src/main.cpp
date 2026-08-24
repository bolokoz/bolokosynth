#include <Arduino.h>
#include "AudioTools.h"
#include "AudioTools/AudioLibs/AudioBoardStream.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Synth.h"
#include "WebManager.h"
#include "PitchDetector.h"

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
// AudioBoardStream uses arduino-audio-driver under the hood (replaces the
// archived arduino-audiokit HAL). AudioKitAC101 selects the AI-Thinker V2.2
// board with the AC101 codec.
AudioBoardStream out(AudioKitAC101);
Synth synth(44100);
WebManager webManager(&synth);
// StreamCopy pulls samples from the synth effect chain into the codec sink.
// Constructed after `synth` so getOutputStream() returns a valid reference.
StreamCopy copier(out, synth.getOutputStream());

// --- Global State for Display ---
int last_note = -1;
PitchDetector::Result detected_key;

// Live state shared with PitchDetector + displayTask.
// volatile: written by capture loop / MIDI handler, read by displayTask on Core 0.
volatile float live_ratio   = 3.0f;
volatile bool  capturing    = false;

// --- MIDI Parser State ---
int midi_state = 0; // 0: status, 1: data1, 2: data2
uint8_t status_byte = 0;
uint8_t data1_byte = 0;

// Processes individual MIDI messages and updates the synthesizer state or configuration.
// Polyphonic: each Note On allocates a voice, each Note Off releases the matching voice.
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
            // d1 = MIDI note number, freq = equivalent Hz for the oscillator
            float freq = 440.0f * pow(2.0f, (d1 - 69.0f) / 12.0f);
            synth.noteOn(d1, freq);
            last_note = d1;
        } else {
            // Note On with velocity 0 is Note Off
            synth.noteOff(d1);
        }
    } else if (cmd == 0x80) { // Note Off
        synth.noteOff(d1);
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
        } else if (d1 == webManager.getCcPitch()) { // Pitch Detect trigger
             // Any non-zero value kicks off a 2s mic capture followed by reboot.
             if (d2 > 0) {
                 capturing   = true;
                 live_ratio  = webManager.getSignalRatio();
                 PitchDetector::detectPersistAndReboot(
                     out, synth.getInfo(),
                     webManager.getCcRatio(),
                     live_ratio);
                 // never returns (reboots)
             }
        } else if (d1 == webManager.getCcRatio()) { // Live ratio knob
             // Map 0-127 to 1.0-10.0
             live_ratio = 1.0f + (d2 / 127.0f) * 9.0f;
             // Persist so the value survives next boot
             Preferences prefs;
             prefs.begin("synth-config", false);
             prefs.putFloat("signal_ratio", live_ratio);
             prefs.end();
        }
    }
}

// FreeRTOS task responsible for updating the OLED display at a fixed rate.
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

        // ---- Capture overlay: shown only while pitch detector is running ----
        if (capturing) {
            display.println(F("CAPTURING..."));
            display.setCursor(0, 16);
            display.println(F("Play radio"));
            display.setCursor(0, 28);
            display.println(F("Turn CC"));
            display.print  (webManager.getCcRatio());
            display.println(F(" knob:"));
            display.setCursor(0, 44);
            display.setTextSize(2);
            display.print  (live_ratio, 1);
            display.setTextSize(1);
            display.display();
            vTaskDelay(50 / portTICK_PERIOD_MS);  // 20 FPS for snappy knob feedback
            continue;
        }

        // ---- Normal layout ----
        display.println(F("ESP32 Synth"));

        // Vol + Wave on one line, ratio on right side
        display.setCursor(0, 12);
        display.print(F("V:"));
        display.print(synth.getVolume());
        display.print(F(" W:"));
        switch(synth.getWaveform()) {
            case 0: display.print(F("Tri")); break;
            case 1: display.print(F("Sin")); break;
            case 2: display.print(F("Sqr")); break;
            case 3: display.print(F("Saw")); break;
            default: display.print(F("?")); break;
        }
        // Live ratio indicator (small, top-right corner)
        display.setCursor(96, 12);
        display.print(F("R"));
        display.print(live_ratio, 1);

        // Note + active voice count
        display.setCursor(0, 24);
        display.print(F("Note:"));
        if (last_note != -1) {
            display.print(last_note);
        } else {
            display.print(F("--"));
        }

        int voices = synth.getActiveVoices();
        if (voices > 0) {
            display.setCursor(90, 24);
            display.print(voices);
            display.print(F("v"));
        }

        // Detected tone from mic capture (persisted across reboots).
        display.setCursor(0, 36);
        display.print(F("Key:"));
        display.print(detected_key.name);
        if (detected_key.valid) {
            display.setCursor(90, 36);
            display.print((int)detected_key.frequency);
            display.print(F("Hz"));
        }

        // CC mapping line — compact format to fit 4 mappings in 128px
        display.setCursor(0, 52);
        display.print(webManager.getCcVol());
        display.print(F(":V "));
        display.print(webManager.getCcWave());
        display.print(F(":W "));
        display.print(webManager.getCcPitch());
        display.print(F(":K "));
        display.print(webManager.getCcRatio());
        display.print(F(":R"));

        display.display();

        // Update at ~10 FPS
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// Initial configuration for peripherals, hardware, and system services.
void setup() {
    Serial.begin(115200);

    Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

    // Initialize synth engine (generators + ADSR + effect chain).
    synth.begin();

    // Configure codec board: sample rate / bits / channels come from synth.
    auto cfg = out.defaultConfig(TX_MODE);
    cfg.copyFrom(synth.getInfo());
    cfg.sd_active = false;
    out.begin(cfg);
    // Codec HW volume (0.0..1.0). Cheaper than software scaling per sample.
    out.setVolume(synth.getVolume() / 100.0f);

    // Web/WiFi Setup
    webManager.begin();

    // Sync live_ratio from persisted value so display shows correct number
    // on boot (in case user changed it via web UI or last capture).
    live_ratio = webManager.getSignalRatio();

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

    // Load last pitch-detection result (set by PitchDetector before reboot).
    detected_key = PitchDetector::loadFromNVS();
    if (detected_key.valid) {
        Serial.printf("Detected key: %s (%.1f Hz)\n",
                      detected_key.name.c_str(), detected_key.frequency);
    } else if (detected_key.name == "Low signal") {
        Serial.println("Last capture: low signal");
    }
}

// Main execution loop: handles web requests, processes MIDI input, and generates audio samples.
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
    // StreamCopy pulls from the synth effect chain and pushes to the codec.
    // It copies as much as the codec can accept in this iteration.
    copier.copy();

    // Sync software volume (set via Web/MIDI) to codec HW volume.
    static int lastVol = -1;
    int v = synth.getVolume();
    if (v != lastVol) {
        out.setVolume(v / 100.0f);
        lastVol = v;
    }
}