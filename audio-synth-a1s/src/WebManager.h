#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include <Arduino.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "Synth.h"

class WebManager {
public:
    WebManager(Synth* synthInstance);
    void begin();
    void handle();

    // Configuration accessors
    int getMidiChannel() { return midiChannel; }
    int getCcVol() { return ccVol; }
    int getCcWave() { return ccWave; }

private:
    Synth* synth;
    WebServer server;
    
    int midiChannel = -1;
    int ccVol = 7;
    int ccWave = 70;

    // Handlers
    void handleRoot();
    void handleConfigGet();
    void handleConfigPost();

    // HTML Content
    static const char* index_html;
};

#endif
