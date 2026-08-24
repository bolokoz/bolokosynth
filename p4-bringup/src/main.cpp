#include <Arduino.h>
#include "EspUsbHost.h"

EspUsbHost usb;

static const char *speedName(usb_speed_t speed) {
    switch (speed) {
        case USB_SPEED_LOW: return "low-speed";
        case USB_SPEED_FULL: return "full-speed";
        case USB_SPEED_HIGH: return "high-speed";
        default: return "unknown";
    }
}

static void printMidi(const EspUsbHostMidiMessage &message) {
    Serial.printf("MIDI DATA: dev=%u iface=%u cable=%u status=%02X data=%02X %02X\n",
                  message.address,
                  message.interfaceNumber,
                  message.cable,
                  message.status,
                  message.data1,
                  message.data2);
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("BolokoSynth P4 USB host diagnostic");
    Serial.println("FUSB = flash/serial; HUSB = high-speed USB host");
    Serial.println("Waiting for the Arturia MiniLab...");

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
        Serial.printf("USB ATTACHED: address=%u speed=%s VID=%04X PID=%04X\n",
                      device.address,
                      speedName(device.speed),
                      device.vid,
                      device.pid);
        Serial.printf("USB DEVICE: product=\"%s\" manufacturer=\"%s\" max-power=%umA\n",
                      device.product,
                      device.manufacturer,
                      static_cast<unsigned>(device.configurationMaxPower) * 2);
    });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        Serial.printf("USB DETACHED: address=%u VID=%04X PID=%04X\n",
                      device.address, device.vid, device.pid);
    });

    usb.onMidiMessage([](const EspUsbHostMidiMessage &message) {
        printMidi(message);
    });

    EspUsbHostConfig config;
    config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    if (!usb.begin(config)) {
        Serial.printf("ERROR: USB host start failed: %s\n", usb.lastErrorName());
    } else {
        Serial.println("USB host initialized on HUSB");
    }
}

void loop() {
    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 1000) {
        lastHeartbeat = millis();
        Serial.printf("USB host alive; devices=%u heap=%u\n",
                      static_cast<unsigned>(usb.deviceCount()),
                      static_cast<unsigned>(ESP.getFreeHeap()));
    }
    delay(1);
}
