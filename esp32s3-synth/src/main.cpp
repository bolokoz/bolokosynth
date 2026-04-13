#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s_std.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "EspUsbHost.h"

// ── I2S Pins (to PCM5102A) ──────────────────────────────
#define I2S_BCLK   GPIO_NUM_4
#define I2S_WS     GPIO_NUM_5
#define I2S_DOUT   GPIO_NUM_6

// ── Synth Parameters ────────────────────────────────────
static const int SAMPLE_RATE     = 44100;
static const int SAMPLES_PER_BUF = 64;
static const i2s_port_t I2S_PORT = I2S_NUM_0;

// ── OLED ─────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDR     0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── Global Synth State ───────────────────────────────────
static volatile int   current_volume   = 80;
static volatile int   current_waveform = 0;   // 0:Triangle 1:Square 2:Saw
static volatile int   last_note        = -1;
static volatile bool  note_on          = false;
static volatile float frequency       = 440.0f;
static float phase     = 0.0f;
static float phase_inc = 0.0f;

static void updatePhaseInc() {
    phase_inc = (frequency * 2.0f) / (float)SAMPLE_RATE;
}

// ── I2S Setup (ESP-IDF driver, standard mode) ────────────
static i2s_chan_handle_t tx_handle = nullptr;

static void i2sSetup() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = SAMPLES_PER_BUF;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk  = I2S_GPIO_UNUSED,
            .bclk  = I2S_BCLK,
            .ws    = I2S_WS,
            .dout  = I2S_DOUT,
            .din   = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

// ── USB Host (MIDI) ──────────────────────────────────────
class MidiHost : public EspUsbHost {
public:
    void onData(const usb_transfer_t *transfer) override {
        if (transfer->status != 0 || transfer->actual_num_bytes == 0) return;

        uint8_t *data = transfer->data_buffer;
        size_t len = transfer->actual_num_bytes;

        for (size_t i = 0; i + 4 <= len; i += 4) {
            uint8_t cin = data[i] & 0x0F;
            uint8_t m0  = data[i + 1];
            uint8_t m1  = data[i + 2];
            uint8_t m2  = data[i + 3];

            if (cin == 0x8 || cin == 0x9 || cin == 0xB) {
                handleMidi(m0, m1, m2);
            }
        }
    }
};

static MidiHost usbHost;

static void handleMidi(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t cmd = status & 0xF0;

    if (cmd == 0x90) {
        if (d2 > 0) {
            frequency = 440.0f * powf(2.0f, (d1 - 69.0f) / 12.0f);
            updatePhaseInc();
            note_on = true;
            last_note = d1;
        } else {
            note_on = false;
        }
    } else if (cmd == 0x80) {
        note_on = false;
    } else if (cmd == 0xB0) {
        if (d1 == 7) {
            current_volume = map(d2, 0, 127, 0, 100);
        } else if (d1 == 70) {
            if (d2 < 42)      current_waveform = 0;
            else if (d2 < 84) current_waveform = 1;
            else              current_waveform = 2;
        }
    }
}

// ── Audio Generation ─────────────────────────────────────
static void generateAudio(int16_t *buf, int count) {
    float vol = current_volume / 100.0f;

    for (int i = 0; i < count; i++) {
        int16_t sample = 0;

        if (note_on) {
            float s = 0.0f;
            int wf = current_waveform;

            if (wf == 0) {
                s = (phase < 1.0f) ? (-1.0f + 2.0f * phase) : (1.0f - 2.0f * (phase - 1.0f));
            } else if (wf == 1) {
                s = (phase < 1.0f) ? -1.0f : 1.0f;
            } else if (wf == 2) {
                s = -1.0f + phase;
            }

            phase += phase_inc;
            if (phase >= 2.0f) phase -= 2.0f;

            sample = (int16_t)(s * 10000.0f * vol);
        } else {
            phase = 0.0f;
        }

        buf[i * 2]     = sample;
        buf[i * 2 + 1] = sample;
    }
}

// ── Display Task (Core 0) ────────────────────────────────
static void displayTask(void *) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED init failed");
        vTaskDelete(nullptr);
    }
    display.clearDisplay();
    display.display();

    for (;;) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        display.setCursor(0, 0);
        display.println(F("S3 Synth"));

        display.setCursor(0, 12);
        display.print(F("Vol: "));
        display.print(current_volume);

        display.setCursor(0, 24);
        display.print(F("Wave: "));
        switch (current_waveform) {
            case 0: display.print(F("Triangle")); break;
            case 1: display.print(F("Square"));   break;
            case 2: display.print(F("Saw"));      break;
            default: display.print(F("???"));      break;
        }

        display.setCursor(0, 36);
        display.print(F("Note: "));
        if (last_note != -1) {
            display.print(last_note);
            if (note_on) display.print(F(" *ON*"));
        } else {
            display.print(F("--"));
        }

        display.setCursor(0, 52);
        display.print(F("CC7:Vol CC70:Wave"));

        display.display();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── Setup ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("ESP32-S3 Synth starting...");

    Wire.begin();
    i2sSetup();
    updatePhaseInc();

    usbHost.begin();
    Serial.println("USB Host started");

    xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 1, nullptr, 0);
    Serial.println("Ready. Waiting for MIDI...");
}

// ── Main Loop (Core 1) ──────────────────────────────────
void loop() {
    usbHost.task();

    int16_t stereo_buf[SAMPLES_PER_BUF * 2];
    generateAudio(stereo_buf, SAMPLES_PER_BUF);

    size_t bytes_written = 0;
    i2s_channel_write(tx_handle, stereo_buf, sizeof(stereo_buf), &bytes_written, portMAX_DELAY);
}
