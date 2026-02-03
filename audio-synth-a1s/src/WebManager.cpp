#include "WebManager.h"

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

WebManager::WebManager(Synth* synthInstance) : synth(synthInstance), server(80) {}

void WebManager::begin() {
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

void WebManager::handle() {
    server.handleClient();
}

void WebManager::handleRoot() {
    server.send(200, "text/html", index_html);
}

void WebManager::handleConfigGet() {
    StaticJsonDocument<200> doc;
    doc["volume"] = synth->getVolume();
    doc["waveform"] = synth->getWaveform();
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void WebManager::handleConfigPost() {
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
        synth->setVolume(vol);
    }
    if (doc.containsKey("waveform")) {
        int wave = doc["waveform"];
        synth->setWaveform(wave);
    }

    server.send(200, "application/json", "{\"status\":\"ok\"}");
}
