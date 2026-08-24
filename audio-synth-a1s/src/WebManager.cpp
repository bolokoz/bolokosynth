#include "WebManager.h"
#include <Preferences.h>

const char* WebManager::index_html = R"rawliteral(
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
    <hr>
    <h3>MIDI Configuration</h3>
    <div>
        <label for="midi_channel">MIDI Channel:</label>
        <select id="midi_channel">
            <option value="-1">Omni</option>
            <option value="0">1</option><option value="1">2</option><option value="2">3</option><option value="3">4</option>
            <option value="4">5</option><option value="5">6</option><option value="6">7</option><option value="7">8</option>
            <option value="8">9</option><option value="9">10</option><option value="10">11</option><option value="11">12</option>
            <option value="12">13</option><option value="13">14</option><option value="14">15</option><option value="15">16</option>
        </select>
    </div>
    <div>
        <label for="cc_vol">Volume CC:</label>
        <input type="number" id="cc_vol" min="0" max="127" style="width: 50px;">
    </div>
    <div>
        <label for="cc_wave">Waveform CC:</label>
        <input type="number" id="cc_wave" min="0" max="127" style="width: 50px;">
    </div>
    <div>
        <label for="cc_pitch">Pitch Detect CC:</label>
        <input type="number" id="cc_pitch" min="0" max="127" style="width: 50px;">
    </div>
    <div>
        <label for="cc_ratio">Ratio Knob CC:</label>
        <input type="number" id="cc_ratio" min="0" max="127" style="width: 50px;">
    </div>
    <div class="slidecontainer">
        <label for="signal_ratio">Signal Ratio: <span id="ratioVal"></span></label>
        <input type="range" min="1.0" max="10.0" step="0.1" value="3.0" class="slider" id="signal_ratio">
    </div>

    <script>
        function updateState() {
            var vol = document.getElementById("volume").value;
            var wave = document.getElementById("waveform").value;
            var ch = document.getElementById("midi_channel").value;
            var cc_vol = document.getElementById("cc_vol").value;
            var cc_wave = document.getElementById("cc_wave").value;
            var cc_pitch = document.getElementById("cc_pitch").value;
            var cc_ratio = document.getElementById("cc_ratio").value;
            var signal_ratio = document.getElementById("signal_ratio").value;

            document.getElementById("volVal").innerText = vol;
            document.getElementById("ratioVal").innerText = signal_ratio;

            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/api/config", true);
            xhr.setRequestHeader('Content-Type', 'application/json');
            xhr.send(JSON.stringify({
                volume: parseInt(vol),
                waveform: parseInt(wave),
                midi_channel: parseInt(ch),
                cc_vol: parseInt(cc_vol),
                cc_wave: parseInt(cc_wave),
                cc_pitch: parseInt(cc_pitch),
                cc_ratio: parseInt(cc_ratio),
                signal_ratio: parseFloat(signal_ratio)
            }));
        }

        document.getElementById("volume").onchange = updateState;
        document.getElementById("waveform").onchange = updateState;
        document.getElementById("midi_channel").onchange = updateState;
        document.getElementById("cc_vol").onchange = updateState;
        document.getElementById("cc_wave").onchange = updateState;
        document.getElementById("cc_pitch").onchange = updateState;
        document.getElementById("cc_ratio").onchange = updateState;
        document.getElementById("signal_ratio").onchange = updateState;

        // Load initial state
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function() {
            if (this.readyState == 4 && this.status == 200) {
                var data = JSON.parse(this.responseText);
                document.getElementById("volume").value = data.volume;
                document.getElementById("volVal").innerText = data.volume;
                document.getElementById("waveform").value = data.waveform;

                if(data.midi_channel !== undefined) document.getElementById("midi_channel").value = data.midi_channel;
                if(data.cc_vol !== undefined) document.getElementById("cc_vol").value = data.cc_vol;
                if(data.cc_wave !== undefined) document.getElementById("cc_wave").value = data.cc_wave;
                if(data.cc_pitch !== undefined) document.getElementById("cc_pitch").value = data.cc_pitch;
                if(data.cc_ratio !== undefined) document.getElementById("cc_ratio").value = data.cc_ratio;
                if(data.signal_ratio !== undefined) {
                    document.getElementById("signal_ratio").value = data.signal_ratio;
                    document.getElementById("ratioVal").innerText = data.signal_ratio;
                }
            }
        };
        xhr.open("GET", "/api/config", true);
        xhr.send();
    </script>
</body>
</html>
)rawliteral";

WebManager::WebManager(Synth* synthInstance) : synth(synthInstance), server(80) {}

// Initializes WiFi connection via WiFiManager and sets up web server routes.
void WebManager::begin() {
    // Load Preferences
    Preferences preferences;
    preferences.begin("synth-config", true); // read-only
    midiChannel = preferences.getInt("midi_channel", -1);
    ccVol = preferences.getInt("cc_vol", 7);
    ccWave = preferences.getInt("cc_wave", 70);
    ccPitch = preferences.getInt("cc_pitch", 80);
    ccRatio = preferences.getInt("cc_ratio", 81);
    signalRatio = preferences.getFloat("signal_ratio", 3.0f);
    preferences.end();

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
    server.on("/", [this](){ handleRoot(); });
    server.on("/api/config", HTTP_GET, [this](){ handleConfigGet(); });
    server.on("/api/config", HTTP_POST, [this](){ handleConfigPost(); });
    server.begin();
}

// Periodically called to handle incoming HTTP client requests.
void WebManager::handle() {
    server.handleClient();
}

// Serves the main HTML configuration page.
void WebManager::handleRoot() {
    server.send(200, "text/html", index_html);
}

// Returns the current synthesizer and MIDI configuration as a JSON object.
void WebManager::handleConfigGet() {
    StaticJsonDocument<300> doc;
    doc["volume"] = synth->getVolume();
    doc["waveform"] = synth->getWaveform();
    doc["midi_channel"] = midiChannel;
    doc["cc_vol"] = ccVol;
    doc["cc_wave"] = ccWave;
    doc["cc_pitch"] = ccPitch;
    doc["cc_ratio"] = ccRatio;
    doc["signal_ratio"] = signalRatio;
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

// Updates the synthesizer and MIDI configuration from a JSON POST request.
void WebManager::handleConfigPost() {
    if (server.hasArg("plain") == false) {
        server.send(400, "text/plain", "Body not received");
        return;
    }
    StaticJsonDocument<300> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    Preferences preferences;
    preferences.begin("synth-config", false);

    if (doc.containsKey("volume")) {
        int vol = doc["volume"];
        synth->setVolume(vol);
    }
    if (doc.containsKey("waveform")) {
        int wave = doc["waveform"];
        synth->setWaveform(wave);
    }
    if (doc.containsKey("midi_channel")) {
        midiChannel = doc["midi_channel"];
        preferences.putInt("midi_channel", midiChannel);
    }
    if (doc.containsKey("cc_vol")) {
        ccVol = doc["cc_vol"];
        preferences.putInt("cc_vol", ccVol);
    }
    if (doc.containsKey("cc_wave")) {
        ccWave = doc["cc_wave"];
        preferences.putInt("cc_wave", ccWave);
    }
    if (doc.containsKey("cc_pitch")) {
        ccPitch = doc["cc_pitch"];
        preferences.putInt("cc_pitch", ccPitch);
    }
    if (doc.containsKey("cc_ratio")) {
        ccRatio = doc["cc_ratio"];
        preferences.putInt("cc_ratio", ccRatio);
    }
    if (doc.containsKey("signal_ratio")) {
        signalRatio = doc["signal_ratio"];
        preferences.putFloat("signal_ratio", signalRatio);
    }

    preferences.end();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}