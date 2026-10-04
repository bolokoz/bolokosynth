/*
 * Bare-board S3 reset investigation: reset reason, BOOT level and RGB heartbeat.
 * Does not initialize USB host, I2S or external modules, and never waits for a
 * serial monitor. Reports to native Serial and physical Serial0 to distinguish
 * missing logs from a stalled app. A heartbeat proves execution, not absence of
 * solder faults. This alternate environment replaces, rather than joins, main.
 */

#include <Arduino.h>
#include <esp_system.h>

// This diagnostic deliberately avoids USB host, I2S, WiFi, and external modules.
// Never wait for a serial monitor: RGB heartbeat shows startup without a laptop.
static const char *resetName(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
    }
}

static esp_reset_reason_t bootReason;
static unsigned long heartbeats = 0;

static void report(Print &port)
{
    port.printf("BOOT-DIAGNOSTIC uptime=%lu ms reset=%s (%d) GPIO0=%d heartbeat=%lu\n",
                millis(), resetName(bootReason), static_cast<int>(bootReason),
                digitalRead(0), heartbeats);
}

void setup()
{
    bootReason = esp_reset_reason();
    pinMode(0, INPUT); // Observe BOOT without changing its pull-up configuration.
    rgbLedWrite(RGB_BUILTIN, 16, 0, 16); // Purple: application entered setup.
    Serial0.begin(115200); // Physical UART bridge, if present.
    Serial.begin(115200); // Native USB JTAG/serial in this diagnostic environment.
    report(Serial0);
    report(Serial);
}

void loop()
{
    // Low brightness green pulse once a second means the application is running.
    static unsigned long lastHeartbeat = 0;
    const unsigned long now = millis();
    rgbLedWrite(RGB_BUILTIN, 0, now % 1000 < 150 ? 16 : 0, 0);
    if (now - lastHeartbeat >= 1000) {
        lastHeartbeat = now;
        ++heartbeats;
        report(Serial0);
        report(Serial);
    }
    delay(10);
}
