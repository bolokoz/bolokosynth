/*
 * White-theme TFT menus, ADSR visualization and right-side knob feedback.
 * Render one dirty region per call and split full clears into stripes so the UI
 * does not stall control processing. Popup-covered jobs are deferred until expiry;
 * header/status/footer stay live. Current values use the control-layer formatter.
 * Zero-time envelope stages have zero graph width despite the one-sample DSP floor.
 * Use ST7789 SPI mode 0, INVOFF and 10 MHz as physically tested. BLK stays open.
 * Brightness was resolved through power wiring; a library change was not required.
 */

#include "SynthUi.h"
#include "SynthControls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint16_t BACKGROUND = ST77XX_WHITE;
constexpr uint16_t PANEL = 0xEF7D;
constexpr uint16_t FOREGROUND = ST77XX_BLACK;
constexpr uint16_t MUTED = 0x5AEB;
constexpr uint16_t RULE = 0xBDF7;
constexpr uint16_t ACCENT = 0x0253;
constexpr uint16_t EDIT = 0xA300;
constexpr uint16_t GOOD = 0x03A0;
constexpr uint16_t BAD = 0xB800;
constexpr uint16_t HEADER_DIRTY = 1U << 0;
constexpr uint16_t STATUS_DIRTY = 1U << 1;
constexpr uint16_t GRAPH_DIRTY = 1U << 2;
constexpr uint16_t FOOTER_DIRTY = 1U << 3;
constexpr uint16_t ITEM_DIRTY(unsigned index) { return 1U << (4 + index); }
constexpr uint16_t ALL_DIRTY = 0x00FF;

const char *parameterName(Parameter parameter) {
    return SynthControls::parameterName(parameter);
}

float parameterValue(const SynthConfig &config, Parameter parameter) {
    switch (parameter) {
        case Parameter::Attack: return config.attackMs;
        case Parameter::Decay: return config.decayMs;
        case Parameter::Sustain: return config.sustain;
        case Parameter::Release: return config.releaseMs;
        case Parameter::Volume: return config.volume;
        default: return 0.0f;
    }
}

void formatValue(const SynthConfig &config, Parameter parameter,
                 char *value, size_t size, const char *&unit) {
    if (parameter == Parameter::Waveform) {
        snprintf(value, size, "%s", SynthControls::waveformName(config.waveform));
        unit = "";
    } else if (parameter == Parameter::Sustain || parameter == Parameter::Volume) {
        snprintf(value, size, "%.0f", parameterValue(config, parameter) * 100);
        unit = "%";
    } else if (static_cast<unsigned>(parameter) < 4) {
        const float ms = parameterValue(config, parameter);
        if (ms >= 1000) { snprintf(value, size, "%.2g", ms / 1000); unit = "seconds"; }
        else { snprintf(value, size, "%.0f", ms); unit = "ms"; }
    } else {
        SynthControls controls;
        controls.config() = config;
        controls.formatValue(parameter, value, size);
        unit = "";
    }
}



bool envelopeChanged(const SynthConfig &a, const SynthConfig &b) {
    return a.attackMs != b.attackMs || a.decayMs != b.decayMs ||
           a.sustain != b.sustain || a.releaseMs != b.releaseMs;
}
} // namespace

bool SynthUi::begin() {
    // The panel is write-only: successful initialization cannot identify an
    // unplugged display. BLK is intentionally left open (module driver pull-up).
    spi_.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
    display_.init(240, 320, SPI_MODE0);
    display_.setRotation(1);
    display_.setSPISpeed(10000000);
    display_.invertDisplay(inverted_);
    display_.setTextWrap(false);
    pinMode(ENCODER_A_PIN, INPUT_PULLUP);
    pinMode(ENCODER_B_PIN, INPUT_PULLUP);
    pinMode(PUSH_PIN, INPUT_PULLUP);
    pinMode(BACK_PIN, INPUT_PULLUP);
    encoderState_ = (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
    push_.raw = push_.stable = digitalRead(PUSH_PIN);
    back_.raw = back_.stable = digitalRead(BACK_PIN);
    push_.changedAt = back_.changedAt = millis();
    push_.pressedAt = back_.pressedAt = millis();
    attachInterruptArg(ENCODER_A_PIN, encoderInterrupt, this, CHANGE);
    attachInterruptArg(ENCODER_B_PIN, encoderInterrupt, this, CHANGE);
    begun_ = true;
    requestPageRedraw();
    return true;
}

void ARDUINO_ISR_ATTR SynthUi::encoderInterrupt(void *argument) {
    static_cast<SynthUi *>(argument)->sampleEncoder();
}

void ARDUINO_ISR_ATTR SynthUi::sampleEncoder() {
    // Invalid two-bit transitions are ignored; contact bounce reverses and
    // cancels a partial movement instead of producing additional detents.
    static const DRAM_ATTR int8_t transitions[16] = {
        0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0
    };
    const uint8_t state = (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
    portENTER_CRITICAL_ISR(&encoderMux_);
    const int8_t movement = transitions[(encoderState_ << 2) | state];
    if (!movement && state != encoderState_) encoderQuarters_ = 0;
    encoderState_ = state;
    encoderQuarters_ += movement;
    if (encoderQuarters_ >= 4) {
        if (encoderDetents_ < 127) encoderDetents_ = encoderDetents_ + 1;
        encoderQuarters_ = 0;
    } else if (encoderQuarters_ <= -4) {
        if (encoderDetents_ > -127) encoderDetents_ = encoderDetents_ - 1;
        encoderQuarters_ = 0;
    }
    portEXIT_CRITICAL_ISR(&encoderMux_);
}

void SynthUi::enqueue(UiActionType type, int delta) {
    const uint8_t next = (actionWrite_ + 1) % 8;
    if (next == actionRead_) actionRead_ = (actionRead_ + 1) % 8;
    actions_[actionWrite_] = {type, selected_, delta};
    actionWrite_ = next;
}

void SynthUi::toggleInversion() {
    inverted_ = !inverted_;
    display_.invertDisplay(inverted_);
    clearY_ = 0;
    if (!brightnessTestUntil_) requestPageRedraw();
}

void SynthUi::startBrightnessTest(bool colorPattern) {
    colorPattern_ = colorPattern;
    brightnessTestUntil_ = millis() + 30000;
    if (!brightnessTestUntil_) brightnessTestUntil_ = 1;
    clearY_ = 0;
    actionRead_ = actionWrite_; // Do not replay earlier parameter actions.
}

UiAction SynthUi::poll() {
    if (!begun_) return {};
    const uint32_t now = millis();
    pollButton(back_, now, true);
    pollButton(push_, now, false);
    int motion;
    portENTER_CRITICAL(&encoderMux_);
    motion = encoderDetents_;
    encoderDetents_ = 0;
    portEXIT_CRITICAL(&encoderMux_);
    if (brightnessTestUntil_) {
        // Keep debouncing buttons and draining ISR motion during the test,
        // without queuing actions that could fire when the normal UI returns.
        actionRead_ = actionWrite_;
        return {};
    }
    if (motion) rotate(motion);
    if (actionRead_ == actionWrite_) return {};
    const UiAction action = actions_[actionRead_];
    actionRead_ = (actionRead_ + 1) % 8;
    return action;
}

void SynthUi::pollButton(Button &button, uint32_t now, bool isBack) {
    const bool level = digitalRead(button.pin);
    if (level != button.raw) {
        button.raw = level;
        button.changedAt = now;
    }
    if (button.raw != button.stable && now - button.changedAt >= 30) {
        button.stable = button.raw;
        if (!button.stable) {
            button.pressedAt = now;
            button.longSent = false;
        } else if (!button.longSent) {
            shortPress(isBack);
        }
    }
    const uint32_t holdMs = isBack ? 1000 : 700;
    if (!button.stable && !button.longSent && now - button.pressedAt >= holdMs) {
        button.longSent = true;
        longPress(isBack);
    }
}

void SynthUi::shortPress(bool isBack) {
    if (brightnessTestUntil_) return;
    if (previousStatus_.learning) {
        if (isBack) enqueue(UiActionType::CancelLearn);
        return;
    }
    if (isBack) {
        if (volumeShortcut_) {
            volumeShortcut_ = false;
            editing_ = false;
            homeSelection_ = 2; // Output, where the volume shortcut lives.
            page_ = Page::Home;
        } else {
            volumeShortcut_ = true;
            page_ = Page::Sound;
            selected_ = Parameter::Volume;
            editing_ = true;
            enqueue(UiActionType::SelectParameter);
        }
        requestPageRedraw();
        return;
    }
    if (page_ == Page::Home) {
        static constexpr Page pages[] = {Page::Envelope, Page::Presets, Page::Sound,
                                         Page::Osc1, Page::Osc2, Page::Osc3, Page::Midi};
        page_ = pages[homeSelection_ % 7];
        if (page_ != Page::Midi) selected_ = pageParameter(0);
        editing_ = false;
        enqueue(UiActionType::SelectParameter);
        requestPageRedraw();
    } else if (page_ == Page::Midi) {
        enqueue(UiActionType::Learn);
    } else {
        editing_ = !editing_;
        markParameter(selected_);
        dirty_ |= HEADER_DIRTY | FOOTER_DIRTY;
    }
}

void SynthUi::longPress(bool isBack) {
    if (brightnessTestUntil_) return;
    if (isBack) {
        editing_ = false;
        panicFeedbackUntil_ = millis() + 1400;
        enqueue(UiActionType::Panic);
        dirty_ |= HEADER_DIRTY | FOOTER_DIRTY;
        markParameter(selected_);
    } else if (page_ == Page::Midi && !previousStatus_.learning) {
        enqueue(UiActionType::CycleLearnMode, 1);
    } else if (page_ != Page::Home && !previousStatus_.learning) {
        editing_ = false;
        enqueue(UiActionType::Learn);
        dirty_ |= HEADER_DIRTY | FOOTER_DIRTY;
        markParameter(selected_);
    }
}

void SynthUi::rotate(int delta) {
    if (previousStatus_.learning) {
        enqueue(UiActionType::CycleLearnMode, delta);
        return;
    }
    if (page_ == Page::Home) {
        const int count = 7;
        homeSelection_ = ((static_cast<int>(homeSelection_) + delta) % count + count) % count;
        dirty_ |= ITEM_DIRTY(0) | ITEM_DIRTY(1) | ITEM_DIRTY(2);
        return;
    }
    if (editing_) {
        enqueue(UiActionType::Adjust, delta);
        return;
    }
    const Parameter old = selected_;
    const int count = pageParameterCount();
    int offset = 0;
    for (int i = 0; i < count; ++i) if (pageParameter(i) == selected_) offset = i;
    selected_ = pageParameter(((offset + delta) % count + count) % count);
    markParameter(old);
    markParameter(selected_);
    dirty_ |= FOOTER_DIRTY;
    if (page_ == Page::Midi) dirty_ |= GRAPH_DIRTY | ITEM_DIRTY(0);
    enqueue(UiActionType::SelectParameter);
}

void SynthUi::requestPageRedraw() {
    clearY_ = 0;
    dirty_ = ALL_DIRTY;
}

unsigned SynthUi::pageParameterCount() const {
    if (page_ == Page::Envelope) return 4;
    if (page_ == Page::Sound) return 2;
    if (page_ == Page::Presets) return 1;
    if (page_ == Page::Midi) return PARAMETER_COUNT;
    return 4;
}

Parameter SynthUi::pageParameter(unsigned index) const {
    if (page_ == Page::Envelope || page_ == Page::Midi) return static_cast<Parameter>(index);
    if (page_ == Page::Sound) return index == 0 ? Parameter::Waveform : Parameter::Volume;
    if (page_ == Page::Presets) return Parameter::Preset;
    if (page_ == Page::Osc1) {
        static constexpr Parameter parameters[] = {Parameter::Waveform, Parameter::Osc1Level,
                                                    Parameter::Osc1Coarse, Parameter::Osc1Fine};
        return parameters[index % 4];
    }
    return static_cast<Parameter>((page_ == Page::Osc2 ? 10 : 14) + index % 4);
}

void SynthUi::markParameter(Parameter parameter) {
    if (page_ == Page::Home) return;
    if (page_ == Page::Midi) { dirty_ |= GRAPH_DIRTY | ITEM_DIRTY(0); return; }
    for (unsigned index = 0; index < pageParameterCount(); ++index)
        if (pageParameter(index) == parameter) dirty_ |= ITEM_DIRTY(index);
    if (page_ == Page::Presets) dirty_ |= GRAPH_DIRTY;
}


void SynthUi::textAt(int16_t x, int16_t y, uint8_t size, uint16_t color, const char *text) {
    display_.setTextSize(size);
    display_.setTextColor(color);
    display_.setCursor(x, y);
    display_.print(text);
}

void SynthUi::drawHeader() {
    display_.fillRect(0, 0, 320, 30, BACKGROUND);
    const char *title = previousStatus_.learning ? "MIDI LEARN" :
                        page_ == Page::Home ? "BOLOKO SYNTH" :
                        page_ == Page::Envelope ? "ENVELOPE" :
                        page_ == Page::Sound ? "OUTPUT" :
                        page_ == Page::Presets ? "SOUNDS / 3 OSC" :
                        page_ == Page::Osc1 ? "OSCILLATOR 1" :
                        page_ == Page::Osc2 ? "OSCILLATOR 2" :
                        page_ == Page::Osc3 ? "OSCILLATOR 3" : "MIDI CONTROL";
    textAt(10, 8, 2, FOREGROUND, title);
    textAt(268, 12, 1, editing_ ? EDIT : ACCENT,
           previousStatus_.learning ? "LEARN" : editing_ ? "EDIT" : "BROWSE");
    display_.drawFastHLine(8, 29, 304, RULE);
}

void SynthUi::drawStatus(const SynthUiStatus &status) {
    display_.fillRect(8, 33, 304, 15, BACKGROUND);
    textAt(10, 35, 1, status.audio ? GOOD : BAD, status.audio ? "AUDIO OK" : "AUDIO --");
    textAt(93, 35, 1, status.midi ? GOOD : MUTED, status.midi ? "USB MIDI" : "USB --");
    char notes[24];
    snprintf(notes, sizeof(notes), "V%u/3 H%u", status.voices, status.held);
    textAt(231, 35, 1, status.held ? ACCENT : MUTED, notes);
}

void SynthUi::drawGraph(const SynthConfig &config) {
    display_.fillRect(8, 51, 304, 74, BACKGROUND);
    constexpr int left = 14, right = 306, top = 59, bottom = 106;
    display_.drawFastHLine(left, bottom, right - left, RULE);
    display_.drawFastVLine(left, top, bottom - top, RULE);
    for (int x = left; x < right; x += 8) display_.drawPixel(x, top + 23, RULE);
    // Positive times remain log-scaled for visibility. A zero-time stage
    // occupies no horizontal distance: its level change is vertical.
    const auto stageWidth = [](float ms) {
        return ms <= 0.0f ? 0.0f : 1.0f + std::log1p(ms / 10.0f);
    };
    const float a = stageWidth(config.attackMs);
    const float d = stageWidth(config.decayMs);
    const float r = stageWidth(config.releaseMs);
    const float total = a + d + r;
    const float scale = total > 0.0f ? (right - left - 42.0f) / total : 0.0f;
    const int attackX = left + static_cast<int>(std::lround(a * scale));
    const int decayX = attackX + static_cast<int>(std::lround(d * scale));
    const int releaseX = right - static_cast<int>(std::lround(r * scale));
    const int sustainY = bottom - static_cast<int>(std::clamp(config.sustain, 0.0f, 1.0f) * (bottom - top));
    display_.drawLine(left, bottom, attackX, top, ACCENT);
    display_.drawLine(attackX, top, decayX, sustainY, ACCENT);
    display_.drawLine(decayX, sustainY, releaseX, sustainY, ACCENT);
    display_.drawLine(releaseX, sustainY, right, bottom, ACCENT);
    display_.fillCircle(attackX, top, 2, ACCENT);
    display_.fillCircle(decayX, sustainY, 2, ACCENT);
    // Fixed legend positions avoid overlapping labels when stages collapse.
    textAt(left + 12, 113, 1, MUTED, "A");
    textAt(left + 86, 113, 1, MUTED, "D");
    textAt(left + 160, 113, 1, MUTED, "S");
    textAt(left + 234, 113, 1, MUTED, "R");
}

void SynthUi::drawWaveform(const SynthConfig &config) {
    display_.fillRect(8, 53, 304, 55, BACKGROUND);
    constexpr int left = 12, top = 59, height = 42, width = 296;
    int previousY = top + height / 2;
    display_.startWrite();
    for (int x = 0; x < width; ++x) {
        const float phase = static_cast<float>(x) / (width - 1) * 2.0f;
        const float cycle = phase - std::floor(phase);
        float value = 0.0f;
        switch (config.waveform) {
            case Waveform::Square: value = cycle < 0.5f ? 0.8f : -0.8f; break;
            case Waveform::Saw: value = (2.0f * cycle - 1.0f) * 0.8f; break;
            case Waveform::Triangle: value = (1.0f - 4.0f * std::fabs(cycle - 0.5f)) * 0.8f; break;
            case Waveform::Sine: value = std::sin(phase * 6.283185307f) * 0.8f; break;
        }
        const int y = top + height / 2 - static_cast<int>(value * height / 2);
        if (x) display_.writeLine(left + x - 1, previousY, left + x, y, ACCENT);
        previousY = y;
    }
    display_.endWrite();
}

void SynthUi::drawCard(unsigned index, const SynthConfig &config) {
    const Parameter parameter = static_cast<Parameter>(index);
    const int x = 8 + index * 77;
    const bool selected = selected_ == parameter;
    display_.fillRect(x, 130, 73, 67, PANEL);
    display_.drawRect(x, 130, 73, 67, selected ? editing_ ? EDIT : ACCENT : RULE);
    textAt(x + 5, 139, 1, selected ? FOREGROUND : MUTED, parameterName(parameter));
    char value[24];
    const char *unit;
    formatValue(config, parameter, value, sizeof(value), unit);
    textAt(x + 5, 156, 2, selected ? editing_ ? EDIT : ACCENT : FOREGROUND, value);
    textAt(x + 5, 182, 1, MUTED, unit);
}

void SynthUi::drawHomeItem(unsigned index) {
    if (index >= 3) return;
    const unsigned item = (homeSelection_ / 3) * 3 + index;
    const int y = 57 + index * 47;
    display_.fillRect(8, y, 304, 41, BACKGROUND);
    if (item >= 7) return;
    static constexpr const char *titles[] = {
        "ENVELOPE", "SOUNDS", "OUTPUT", "OSCILLATOR 1", "OSCILLATOR 2", "OSCILLATOR 3", "MIDI CONTROL"
    };
    static constexpr const char *descriptions[] = {
        "Attack / decay / sustain / release", "Choose a starting sound",
        "Main waveform / output volume", "Shape / level / coarse / fine",
        "Shape / level / coarse / fine", "Shape / level / coarse / fine",
        "Assign keyboard knobs to parameters"
    };
    display_.fillRect(8, y, 304, 41, PANEL);
    display_.drawRect(8, y, 304, 41, item == homeSelection_ ? ACCENT : RULE);
    textAt(17, y + 6, 2, item == homeSelection_ ? ACCENT : FOREGROUND, titles[item]);
    textAt(17, y + 27, 1, MUTED, descriptions[item]);
}

void SynthUi::drawPatchSummary(const SynthConfig &config) {
    display_.fillRect(8, 51, 304, 74, BACKGROUND);
    if (page_ != Page::Presets) return;
    char line[48];
    for (unsigned i = 0; i < 3; ++i) {
        const auto o = oscillatorConfig(config, i);
        snprintf(line, sizeof(line), "OSC%u %-8s %3.0f%% %+d st %+.0f ct",
                 i + 1, SynthControls::waveformName(o.waveform), o.level * 100, o.coarse, o.fine);
        textAt(10, 58 + i * 16, 1, o.level > 0 ? FOREGROUND : MUTED, line);
    }
    textAt(10, 111, 1, MUTED, config.custom ? "Edited patch * / reselect to reload" : "Preset keeps your output volume");
}

void SynthUi::drawPatchItem(unsigned index, const SynthConfig &config) {
    if (index >= pageParameterCount()) return;
    const Parameter parameter = pageParameter(index);
    const bool selected = selected_ == parameter;
    const int y = page_ == Page::Presets ? 130 : 55 + index * 36;
    const int height = page_ == Page::Presets ? 65 : 32;
    display_.fillRect(8, y, 304, height, PANEL);
    display_.drawRect(8, y, 304, height, selected ? editing_ ? EDIT : ACCENT : RULE);
    textAt(17, y + 5, 1, selected ? FOREGROUND : MUTED, parameterName(parameter));
    char value[32];
    SynthControls controls;
    controls.config() = config;
    controls.formatValue(parameter, value, sizeof(value));
    textAt(page_ == Page::Presets ? 17 : 158, y + (page_ == Page::Presets ? 29 : 8),
           2, selected ? editing_ ? EDIT : ACCENT : FOREGROUND, value);
}

void SynthUi::drawSoundItem(unsigned index, const SynthConfig &config) {
    if (index >= 2) return;
    const Parameter parameter = index == 0 ? Parameter::Waveform : Parameter::Volume;
    const bool selected = selected_ == parameter;
    const int y = 117 + index * 43;
    display_.fillRect(8, y, 304, 38, PANEL);
    display_.drawRect(8, y, 304, 38, selected ? editing_ ? EDIT : ACCENT : RULE);
    textAt(17, y + 6, 1, selected ? FOREGROUND : MUTED, parameterName(parameter));
    char value[24];
    const char *unit;
    formatValue(config, parameter, value, sizeof(value), unit);
    if (parameter == Parameter::Volume) {
        char percent[24];
        snprintf(percent, sizeof(percent), "%s%%", value);
        textAt(159, y + 11, 2, selected ? editing_ ? EDIT : ACCENT : FOREGROUND, percent);
    } else {
        textAt(159, y + 11, 2, selected ? editing_ ? EDIT : ACCENT : FOREGROUND, value);
    }
}

void SynthUi::drawMidiTarget(const SynthConfig &config, const SynthUiStatus &status) {
    const Parameter target = status.learning ? status.learnTarget : selected_;
    display_.fillRect(8, 53, 304, 58, BACKGROUND);
    textAt(10, 59, 1, MUTED, status.learning ? "TARGET PARAMETER" : "SELECTED PARAMETER");
    textAt(10, 75, 2, ACCENT, parameterName(target));
    char value[24], line[48];
    const char *unit;
    formatValue(config, target, value, sizeof(value), unit);
    snprintf(line, sizeof(line), "VALUE  %s %s", value, unit);
    textAt(10, 98, 1, FOREGROUND, line);
}

void SynthUi::drawMidiDetails(const SynthUiStatus &status) {
    display_.fillRect(8, 113, 304, 84, BACKGROUND);
    char line[52];
    if (status.learning) {
        textAt(10, 117, 1, MUTED, "KNOB MESSAGE MODE");
        snprintf(line, sizeof(line), "%.*s", 23, status.learnMode);
        textAt(10, 134, 2, EDIT, line);
        textAt(10, 162, 1, FOREGROUND, "Move a MIDI knob or slider to assign.");
        textAt(10, 178, 1, MUTED, "Rotation changes mode; BACK cancels.");
    } else {
        if (status.bindingText[0]) {
            snprintf(line, sizeof(line), "%.*s", 47, status.bindingText);
        } else if (status.cc >= 0 && status.channel >= 0) {
            snprintf(line, sizeof(line), "CC %d  /  CHANNEL %d", status.cc, status.channel + 1);
        } else {
            snprintf(line, sizeof(line), "NO MIDI KNOB ASSIGNED");
        }
        textAt(10, 118, 1, status.cc >= 0 ? ACCENT : MUTED, line);
        snprintf(line, sizeof(line), "CONTROL MODE  %.*s", 23, status.learnMode);
        textAt(10, 141, 1, FOREGROUND, line);
        textAt(10, 165, 1, status.pickupPending ? EDIT : MUTED,
               status.pickupPending ? "PICKUP: match the current value first." : "PUSH learns a keyboard knob.");
        snprintf(line, sizeof(line), "%.*s | Hold PUSH: mode", 23, status.controllerName);
        textAt(10, 182, 1, MUTED, line);
    }
}

void SynthUi::drawFooter() {
    display_.fillRect(8, 205, 304, 35, BACKGROUND);
    display_.drawFastHLine(8, 205, 304, RULE);
    if (panicFeedbackUntil_ != 0) {
        textAt(10, 213, 1, EDIT, "ALL NOTES OFF");
        textAt(10, 228, 1, MUTED, "Release KEY0 to continue.");
    } else if (previousStatus_.learning) {
        textAt(10, 213, 1, FOREGROUND, "TURN mode  |  KEY0 cancel");
        textAt(10, 228, 1, MUTED, "Knob/slider captures channel + CC");
    } else if (page_ == Page::Home) {
        textAt(10, 213, 1, FOREGROUND, "TURN browse  |  PUSH open");
        textAt(10, 228, 1, MUTED, "KEY0 volume | HOLD KEY0 panic");
    } else if (page_ == Page::Midi) {
        textAt(10, 213, 1, FOREGROUND, "TURN target  |  PUSH learn");
        textAt(10, 228, 1, MUTED, "HOLD PUSH mode | KEY0 volume");
    } else {
        textAt(10, 213, 1, FOREGROUND,
               editing_ ? "TURN change  |  PUSH done" : "TURN choose  |  PUSH edit");
        textAt(10, 228, 1, MUTED,
               volumeShortcut_ ? "KEY0 menu | HOLD KEY0 panic" : "HOLD PUSH learn | KEY0 volume");
    }
}

void SynthUi::drawFeedback() {
    // High contrast overlay confined to the right; values use the same
    // formatter as the menu, so units and oscillator identities agree.
    display_.fillRoundRect(158, 58, 154, 112, 6, ACCENT);
    display_.drawRoundRect(158, 58, 154, 112, 6, FOREGROUND);
    textAt(168, 69, 1, ST77XX_WHITE, "CONTROL");
    textAt(168, 89, 1, ST77XX_WHITE, previousStatus_.feedbackName);
    const uint8_t size = strlen(previousStatus_.feedbackValue) <= 11 ? 2 : 1;
    textAt(168, 110, size, ST77XX_WHITE, previousStatus_.feedbackValue);
    textAt(168, 148, 1, ST77XX_WHITE, previousStatus_.feedbackHint);
}

void SynthUi::render(const SynthConfig &config, const SynthUiStatus &status) {
    if (!begun_) return;
    const uint32_t now = millis();
    if (status.feedbackSequence != feedbackSequence_) {
        feedbackSequence_ = status.feedbackSequence;
        feedbackUntil_ = now + 1800;
        feedbackDirty_ = true;
    }
    if (feedbackUntil_ && static_cast<int32_t>(now - feedbackUntil_) >= 0) {
        feedbackUntil_ = 0;
        feedbackDirty_ = false;
        // Remove the popup and restore the page through existing region jobs.
        // No blocking full-screen clear is needed.
        display_.fillRect(158, 58, 154, 112, BACKGROUND);
        dirty_ |= ALL_DIRTY;
    }
    if (brightnessTestUntil_) {
        if (static_cast<int32_t>(millis() - brightnessTestUntil_) < 0) {
            if (clearY_ < 240) {
                const int height = std::min(32, 240 - static_cast<int>(clearY_));
                if (colorPattern_) {
                    if (clearY_ < 128) {
                        display_.fillRect(0, clearY_, 160, height, ST77XX_BLACK);
                        display_.fillRect(160, clearY_, 160, height, ST77XX_WHITE);
                    } else {
                        constexpr uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_CYAN, ST77XX_MAGENTA};
                        for (unsigned index = 0; index < 5; ++index)
                            display_.fillRect(index * 64, clearY_, 64, height, colors[index]);
                    }
                } else {
                    display_.fillRect(0, clearY_, 320, height, ST77XX_WHITE);
                }
                clearY_ += height;
                if (clearY_ >= 240 && colorPattern_) {
                    textAt(10, 15, 2, ST77XX_WHITE, "BLACK");
                    textAt(170, 15, 2, ST77XX_BLACK, "WHITE");
                    const char *labels[] = {"RED", "GREEN", "BLUE", "CYAN", "PINK"};
                    for (unsigned index = 0; index < 5; ++index)
                        textAt(index * 64 + 5, 203, 1, ST77XX_BLACK, labels[index]);
                }
            }
            return; // White remains steady; no flashing or BLK pin changes.
        }
        brightnessTestUntil_ = 0;
        requestPageRedraw();
    }
    if (!hasSnapshot_ || (status.learning && status.learnTarget != previousStatus_.learnTarget) ||
        status.learning != previousStatus_.learning ||
        status.soundEnabled != previousStatus_.soundEnabled) {
        requestPageRedraw();
    }
    if (!hasSnapshot_ || status.audio != previousStatus_.audio ||
        status.midi != previousStatus_.midi || status.held != previousStatus_.held ||
        status.voices != previousStatus_.voices) {
        dirty_ |= STATUS_DIRTY;
    }
    if (!hasSnapshot_ || envelopeChanged(config, previousConfig_)) {
        if (page_ == Page::Envelope) dirty_ |= GRAPH_DIRTY;
        for (unsigned index = 0; index < 4; ++index) {
            const Parameter parameter = static_cast<Parameter>(index);
            if (!hasSnapshot_ || parameterValue(config, parameter) != parameterValue(previousConfig_, parameter)) {
                markParameter(parameter);
            }
        }
    }
    if (!hasSnapshot_ || config.waveform != previousConfig_.waveform) {
        markParameter(Parameter::Waveform);
        if (page_ == Page::Sound) dirty_ |= GRAPH_DIRTY;
    }
    if (!hasSnapshot_ || config.volume != previousConfig_.volume) markParameter(Parameter::Volume);
    if (!hasSnapshot_ || status.cc != previousStatus_.cc || status.channel != previousStatus_.channel ||
        status.pickupPending != previousStatus_.pickupPending ||
        strncmp(status.bindingText, previousStatus_.bindingText, sizeof(status.bindingText)) != 0 ||
        strncmp(status.learnMode, previousStatus_.learnMode, sizeof(status.learnMode)) != 0 ||
        strncmp(status.controllerName, previousStatus_.controllerName, sizeof(status.controllerName)) != 0) {
        if (page_ == Page::Midi || status.learning) dirty_ |= ITEM_DIRTY(0);
    }
    if (page_ == Page::Midi || status.learning) {
        if (!hasSnapshot_ || parameterValue(config, selected_) != parameterValue(previousConfig_, selected_) ||
            (selected_ == Parameter::Waveform && config.waveform != previousConfig_.waveform)) dirty_ |= GRAPH_DIRTY;
    }
    SynthControls currentValues, previousValues;
    currentValues.config() = config;
    previousValues.config() = previousConfig_;
    for (unsigned i = 0; i < PARAMETER_COUNT; ++i) {
        const auto parameter = static_cast<Parameter>(i);
        if (!hasSnapshot_ || currentValues.normalized(parameter) != previousValues.normalized(parameter))
            markParameter(parameter);
    }
    if (config.custom != previousConfig_.custom) markParameter(Parameter::Preset);
    if (page_ == Page::Presets && (config.custom != previousConfig_.custom ||
        config.osc1Level != previousConfig_.osc1Level || config.osc1Coarse != previousConfig_.osc1Coarse ||
        config.osc1Fine != previousConfig_.osc1Fine ||
        config.osc2.level != previousConfig_.osc2.level || config.osc2.waveform != previousConfig_.osc2.waveform ||
        config.osc2.coarse != previousConfig_.osc2.coarse || config.osc2.fine != previousConfig_.osc2.fine ||
        config.osc3.level != previousConfig_.osc3.level || config.osc3.waveform != previousConfig_.osc3.waveform ||
        config.osc3.coarse != previousConfig_.osc3.coarse || config.osc3.fine != previousConfig_.osc3.fine))
        dirty_ |= GRAPH_DIRTY;
    previousConfig_ = config;
    previousStatus_ = status;
    previousStatus_.bindingText[sizeof(previousStatus_.bindingText) - 1] = '\0';
    previousStatus_.learnMode[sizeof(previousStatus_.learnMode) - 1] = '\0';
    previousStatus_.controllerName[sizeof(previousStatus_.controllerName) - 1] = '\0';
    hasSnapshot_ = true;
    if (panicFeedbackUntil_ && static_cast<int32_t>(millis() - panicFeedbackUntil_) >= 0) {
        panicFeedbackUntil_ = 0;
        dirty_ |= FOOTER_DIRTY;
    }

    // A clear is split into ~4 ms stripes; subsequent calls draw one region.
    // No full-screen redraw occurs for notes, MIDI activity or status telemetry.
    if (clearY_ < 240) {
        const int height = std::min(32, 240 - static_cast<int>(clearY_));
        display_.fillRect(0, clearY_, 320, height, BACKGROUND);
        clearY_ += height;
        if (feedbackUntil_) feedbackDirty_ = true;
        return;
    }
    if (feedbackUntil_ && feedbackDirty_) {
        drawFeedback();
        feedbackDirty_ = false;
        return;
    }
    // Ignore regions absent from this page and rotate priority. A continuous
    // CC stream cannot keep the graph dirty forever at the expense of values,
    // notes/status or navigation hints.
    if (status.learning || page_ == Page::Midi) {
        dirty_ &= ~(ITEM_DIRTY(1) | ITEM_DIRTY(2) | ITEM_DIRTY(3));
    } else if (page_ == Page::Sound) {
        dirty_ &= ~(ITEM_DIRTY(2) | ITEM_DIRTY(3));
    } else if (page_ == Page::Presets) {
        dirty_ &= ~(ITEM_DIRTY(1) | ITEM_DIRTY(2) | ITEM_DIRTY(3));
    } else if (page_ == Page::Home) {
        dirty_ &= ~(GRAPH_DIRTY | ITEM_DIRTY(3));

    }
    for (unsigned scanned = 0; scanned < 8; ++scanned) {
        const uint8_t job = (nextDrawJob_ + scanned) % 8;
        const uint16_t bit = 1U << job;
        if (!(dirty_ & bit)) continue;
        // Defer content regions beneath the overlay while it is visible.
        // Header/status/footer remain live; pending values restore on expiry.
        if (feedbackUntil_ && job != 0 && job != 1 && job != 3) continue;
        dirty_ &= ~bit;
        nextDrawJob_ = (job + 1) % 8;
        if (job == 0) {
            drawHeader();
        } else if (job == 1) {
            drawStatus(previousStatus_);
        } else if (job == 2) {
            if (status.learning || page_ == Page::Midi) drawMidiTarget(config, previousStatus_);
            else if (page_ == Page::Envelope) drawGraph(config);
            else if (page_ == Page::Sound) drawWaveform(config);
            else drawPatchSummary(config);
        } else if (job == 3) {
            drawFooter();
        } else {
            const unsigned index = job - 4;
            if (status.learning || page_ == Page::Midi) drawMidiDetails(previousStatus_);
            else if (page_ == Page::Envelope) drawCard(index, config);
            else if (page_ == Page::Sound) drawSoundItem(index, config);
            else if (page_ == Page::Home) drawHomeItem(index);
            else drawPatchItem(index, config);
        }
        return;
    }
}
