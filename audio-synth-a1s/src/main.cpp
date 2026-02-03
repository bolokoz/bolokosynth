#include <Arduino.h>
#include "AudioKit.h"
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "Synth.h"

// Define UART pins for MIDI communication
// Adjust these pins according to your specific wiring and AudioKit board version
#define MIDI_RX_PIN 22
#define MIDI_TX_PIN 23

AudioKit kit;
Synth synth(44100);
WebServer server(80);

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

const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Synth Config</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }
        .slidecontainer { width: 80%; margin: 20px auto; }
        .slider { -webkit-appearance: none; width: 100%; height: 15px; border-radius: 5px; background: #d3d3d3; outline: none; opacity: 0.7; transition: .2s; }
        .slider:hover { opacity: 1; }
        .slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 25px; height: 25px; border-radius: 50%; background: #4CAF50; cursor: pointer; }
        select { padding: 10px; font-size: 16px; margin: 20px; }
    </style>
</head>
<body>
    <h1>Synth Configuration</h1>
    <div class="slidecontainer">
        <label for="volume">Volume: <span id="volVal"></span></label>
        <input type="range" min="0" max="100" value="80" class="slider" id="volume">
    </div>
    <div>
        <label for="waveform">Waveform:</label>
        <select id="waveform">
            <option value="0">Triangle</option>
            <option value="1">Sine</option>
            <option value="2">Square</option>
            <option value="3">Sawtooth</option>
        </select>
    </div>
    <script>
        function updateState() {
            var vol = document.getElementById("volume").value;
            var wave = document.getElementById("waveform").value;
            document.getElementById("volVal").innerText = vol;

            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/api/config", true);
            xhr.setRequestHeader('Content-Type', 'application/json');
            xhr.send(JSON.stringify({ volume: parseInt(vol), waveform: parseInt(wave) }));
        }

        document.getElementById("volume").onchange = updateState;
        document.getElementById("waveform").onchange = updateState;

        // Load initial state
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function() {
            if (this.readyState == 4 && this.status == 200) {
                var data = JSON.parse(this.responseText);
                document.getElementById("volume").value = data.volume;
                document.getElementById("volVal").innerText = data.volume;
                document.getElementById("waveform").value = data.waveform;
            }
        };
        xhr.open("GET", "/api/config", true);
        xhr.send();
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", index_html);
}

void handleConfigGet() {
    StaticJsonDocument<200> doc;
    doc["volume"] = synth.getVolume();
    doc["waveform"] = synth.getWaveform();
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleConfigPost() {
    if (server.hasArg("plain") == false) {
        server.send(400, "text/plain", "Body not received");
        return;
    }
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    if (doc.containsKey("volume")) {
        int vol = doc["volume"];
        synth.setVolume(vol);
    }
    if (doc.containsKey("waveform")) {
        int wave = doc["waveform"];
        synth.setWaveform(wave);
    }

    server.send(200, "application/json", "{\"status\":\"ok\"}");
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

    // WiFi Setup
    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);

    if(!wifiManager.autoConnect("ESP32-Synth-AP")) {
        Serial.println("Failed to connect and hit timeout");
    } else {
        Serial.println("WiFi connected");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    }

    // Web Server Setup
    server.on("/", handleRoot);
    server.on("/api/config", HTTP_GET, handleConfigGet);
    server.on("/api/config", HTTP_POST, handleConfigPost);
    server.begin();

    Serial.println("Audio Synth Ready. Waiting for MIDI...");
}

void loop() {
    // 0. Handle Web Server
    server.handleClient();

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
