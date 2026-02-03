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

private:
    Synth* synth;
    WebServer server;

    // Handlers
    void handleRoot();
    void handleConfigGet();
    void handleConfigPost();

    // HTML Content
    static const char* index_html;
};

#endif
