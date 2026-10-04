/*
 * UI/task contract and ST7789/EC11 wiring for the active P4 prototype.
 * All display operations belong to one UI task. Encoder ISR captures quadrature
 * under a critical section; task code handles buttons, navigation and rendering.
 * Snapshots contain copied values only, including right-side control feedback.
 * The small control exposes KEY0 presses; no second rotation signal was verified.
 */

#pragma once

#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "SynthConfig.h"

enum class UiActionType {
    None, SelectParameter, Adjust, Learn, CancelLearn, CycleLearnMode, Panic
};

struct UiAction {
    UiActionType type = UiActionType::None;
    Parameter parameter = Parameter::Attack;
    int delta = 0;
};

// A by-value snapshot: no pointers into controller or USB objects.
struct SynthUiStatus {
    char controllerName[24] = "Generic / Learn";
    bool learning = false;
    Parameter learnTarget = Parameter::Attack;
    int cc = -1;
    int channel = -1; // MIDI channel 0..15; the screen displays 1..16.
    bool pickupPending = false;
    char bindingText[48]{};
    char learnMode[24] = "ABS";
    bool midi = false;
    bool audio = false;
    unsigned held = 0;
    unsigned voices = 0;
    bool soundEnabled = true;
    uint32_t feedbackSequence = 0;
    char feedbackName[32]{};
    char feedbackValue[32]{};
    char feedbackHint[32]{};
};

// Call begin, poll and render from one UI task. Interrupts capture encoder
// motion while SPI drawing runs; no display access occurs in an interrupt.
class SynthUi {
public:
    bool begin();
    UiAction poll();
    // UI-task-only diagnostic; a steady white screen restores after 30 s.
    void startBrightnessTest(bool colorPattern = false);
    void toggleInversion();
    void render(const SynthConfig &config, const SynthUiStatus &status);
    Parameter selectedParameter() const { return selected_; }
    bool editing() const { return editing_; }

private:
    enum class Page : uint8_t { Home, Envelope, Sound, Midi, Presets, Osc1, Osc2, Osc3 };
    struct Button {
        uint8_t pin;
        bool raw = true;
        bool stable = true;
        bool longSent = false;
        uint32_t changedAt = 0;
        uint32_t pressedAt = 0;
    };

    static constexpr uint8_t SCK_PIN = 9, MOSI_PIN = 10;
    static constexpr uint8_t CS_PIN = 14, DC_PIN = 15, RESET_PIN = 16;
    static constexpr uint8_t ENCODER_A_PIN = 18, ENCODER_B_PIN = 19;
    static constexpr uint8_t PUSH_PIN = 20, BACK_PIN = 21;
    static void ARDUINO_ISR_ATTR encoderInterrupt(void *argument);
    void ARDUINO_ISR_ATTR sampleEncoder();
    void pollButton(Button &button, uint32_t now, bool isBack);
    void shortPress(bool isBack);
    void longPress(bool isBack);
    void rotate(int delta);
    void enqueue(UiActionType type, int delta = 0);
    void requestPageRedraw();
    void markParameter(Parameter parameter);
    unsigned pageParameterCount() const;
    Parameter pageParameter(unsigned index) const;
    void drawPatchItem(unsigned index, const SynthConfig &config);
    void drawPatchSummary(const SynthConfig &config);
    void drawHeader();
    void drawStatus(const SynthUiStatus &status);
    void drawGraph(const SynthConfig &config);
    void drawWaveform(const SynthConfig &config);
    void drawCard(unsigned index, const SynthConfig &config);
    void drawHomeItem(unsigned index);
    void drawSoundItem(unsigned index, const SynthConfig &config);
    void drawMidiTarget(const SynthConfig &config, const SynthUiStatus &status);
    void drawMidiDetails(const SynthUiStatus &status);
    void drawFooter();
    void drawFeedback();
    void textAt(int16_t x, int16_t y, uint8_t size, uint16_t color, const char *text);

    SPIClass spi_{FSPI};
    Adafruit_ST7789 display_{&spi_, CS_PIN, DC_PIN, RESET_PIN};
    portMUX_TYPE encoderMux_ = portMUX_INITIALIZER_UNLOCKED;
    volatile uint8_t encoderState_ = 0;
    volatile int8_t encoderQuarters_ = 0;
    volatile int16_t encoderDetents_ = 0;
    Button push_{PUSH_PIN};
    Button back_{BACK_PIN};
    UiAction actions_[8]{};
    uint8_t actionRead_ = 0, actionWrite_ = 0;
    Page page_ = Page::Presets;
    Parameter selected_ = Parameter::Preset;
    uint8_t homeSelection_ = 0;
    bool editing_ = false, begun_ = false, hasSnapshot_ = false;
    bool volumeShortcut_ = false;
    int16_t clearY_ = 0;
    uint16_t dirty_ = 0xFFFF;
    uint8_t nextDrawJob_ = 0;
    uint32_t feedbackSequence_ = 0;
    uint32_t feedbackUntil_ = 0;
    bool feedbackDirty_ = false;
    uint32_t panicFeedbackUntil_ = 0;
    uint32_t brightnessTestUntil_ = 0;
    bool colorPattern_ = false;
    bool inverted_ = false; // INVOFF: alternate mode under physical verification.
    SynthConfig previousConfig_{};
    SynthUiStatus previousStatus_{};
};
