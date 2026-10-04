/*
 * Portable parameter editing and MIDI Learn shared by menu, profiles and tests.
 * Absolute controls use soft pickup to prevent jumps after learning or preset
 * changes. Relative controllers need the correct encoding; neutral values are
 * ignored. Assignments include MIDI channel, and reassignment moves duplicates.
 * Normalized values are converted to display units/ranges in one place.
 * Patches preserve volume and mappings, rearm pickup, and mark edits custom.
 * Bindings/settings are RAM-only in v1; this layer does not touch USB or SPI.
 */

#pragma once
#include "SynthConfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

// The three relative encodings are deliberately explicit: hardware defaults vary.
enum class MidiControlMode : uint8_t { Absolute, RelativeOffset, RelativeTwosComplement, RelativeOffset16, RelativeSignMagnitude };
constexpr int MIDI_MODE_COUNT = 5;
enum class ControlResult : uint8_t { Ignored, Learned, Changed };

struct MidiBinding {
    bool assigned = false;
    uint8_t channel = 0; // MIDI wire channel: 0..15, displayed as 1..16.
    uint8_t cc = 0;
    MidiControlMode mode = MidiControlMode::Absolute;
    bool pickedUp = false;
};

// All calls belong to the application/audio loop; USB callbacks only queue MIDI.
class SynthControls {
public:
    SynthControls() { lastValues_.fill(-1); }

    SynthConfig &config() { return config_; }
    const SynthConfig &config() const { return config_; }

    bool setConfig(const SynthConfig &settings) {
        if (!std::isfinite(settings.attackMs) || settings.attackMs < 0 || settings.attackMs > MAX_ATTACK_MS ||
            !std::isfinite(settings.decayMs) || settings.decayMs < 0 || settings.decayMs > MAX_DECAY_MS ||
            !std::isfinite(settings.releaseMs) || settings.releaseMs < 0 || settings.releaseMs > MAX_RELEASE_MS ||
            !std::isfinite(settings.sustain) || settings.sustain < 0 || settings.sustain > 1 ||
            !std::isfinite(settings.volume) || settings.volume < 0 || settings.volume > 1 ||
            static_cast<unsigned>(settings.waveform) > 3) return false;
        if (settings.preset >= SOUND_COUNT) return false;
        for (unsigned i = 0; i < 3; ++i) {
            const auto o = oscillatorConfig(settings, i);
            if (static_cast<unsigned>(o.waveform) > 3 || !std::isfinite(o.level) ||
                o.level < 0 || o.level > 1 || o.coarse < -24 || o.coarse > 24 ||
                !std::isfinite(o.fine) || o.fine < -100 || o.fine > 100) return false;
        }
        config_ = settings;
        for (size_t i = 0; i < bindings_.size(); ++i) invalidatePickup(static_cast<Parameter>(i));
        lastValues_.fill(-1);
        return true;
    }

    bool adjust(Parameter parameter, int steps) {
        if (!valid(parameter) || steps == 0) return false;
        bool changed;
        const int field = oscillatorField(parameter);
        if (field == 0 || parameter == Parameter::Preset) {
            const int count = field == 0 ? 4 : SOUND_COUNT;
            const int current = field == 0 ? static_cast<int>(oscillatorConfig(config_, oscillatorIndex(parameter)).waveform)
                                          : config_.preset;
            const int next = ((current + static_cast<int64_t>(steps)) % count + count) % count;
            changed = setFromNormalized(parameter, (next + 0.5f) / count);
        } else {
            const float step = field == 2 ? 1.0f / 48 : field == 3 ? 1.0f / 200 : 0.01f;
            changed = setFromNormalized(parameter, normalized(parameter) + steps * step);
        }
        // An encoder change moves the pickup target, so the remote knob must catch it.
        if (changed) invalidatePickup(parameter);
        return changed;
    }

    bool setNormalized(Parameter parameter, float value) {
        if (!valid(parameter) || !std::isfinite(value)) return false;
        const bool changed = setFromNormalized(parameter, value);
        if (changed) invalidatePickup(parameter);
        return changed;
    }

    float normalized(Parameter parameter) const {
        if (parameter == Parameter::Preset) return (config_.preset + 0.5f) / SOUND_COUNT;
        const int osc = oscillatorIndex(parameter);
        if (osc >= 0) {
            const auto o = oscillatorConfig(config_, osc);
            switch (oscillatorField(parameter)) {
                case 0: return (static_cast<unsigned>(o.waveform) + 0.5f) / 4.0f;
                case 1: return o.level;
                case 2: return (o.coarse + 24) / 48.0f;
                default: return (o.fine + 100) / 200.0f;
            }
        }
        switch (parameter) {
            case Parameter::Attack: return timeNormalized(config_.attackMs, MAX_ATTACK_MS);
            case Parameter::Decay: return timeNormalized(config_.decayMs, MAX_DECAY_MS);
            case Parameter::Sustain: return unit(config_.sustain);
            case Parameter::Release: return timeNormalized(config_.releaseMs, MAX_RELEASE_MS);
            case Parameter::Waveform: return (static_cast<unsigned>(config_.waveform) + 0.5f) / 4.0f;
            case Parameter::Volume: return unit(config_.volume);
            default: return 0.0f;
        }
    }

    void beginLearn(Parameter parameter, MidiControlMode mode = MidiControlMode::Absolute) {
        learnParameter_ = valid(parameter) ? parameter : Parameter::Count;
        setLearnMode(mode);
    }
    // Faders/touch modulation strips send positions, not relative steps.
    void beginSliderLearn(Parameter parameter = Parameter::Volume) {
        beginLearn(parameter, MidiControlMode::Absolute);
    }
    void cancelLearn() { learnParameter_ = Parameter::Count; }
    bool learning() const { return valid(learnParameter_); }
    Parameter learningParameter() const { return learnParameter_; }
    void setLearnMode(MidiControlMode mode) {
        switch (mode) {
            case MidiControlMode::RelativeOffset:
            case MidiControlMode::RelativeTwosComplement:
            case MidiControlMode::RelativeOffset16:
            case MidiControlMode::RelativeSignMagnitude: learnMode_ = mode; break;
            default: learnMode_ = MidiControlMode::Absolute; break;
        }
    }
    MidiControlMode learnMode() const { return learnMode_; }
    void cycleLearnMode() {
        setLearnMode(static_cast<MidiControlMode>((static_cast<unsigned>(learnMode_) + 1) % MIDI_MODE_COUNT));
    }

    const MidiBinding &binding(Parameter parameter) const {
        static const MidiBinding unassigned{};
        return valid(parameter) ? bindings_[index(parameter)] : unassigned;
    }
    bool setBinding(Parameter parameter, const MidiBinding &mapping) {
        if (!valid(parameter)) return false;
        if (!mapping.assigned) {
            clearBinding(parameter);
            return true;
        }
        if (mapping.channel >= 16 || mapping.cc >= 120 ||
            static_cast<unsigned>(mapping.mode) > static_cast<unsigned>(MidiControlMode::RelativeSignMagnitude)) return false;
        const MidiBinding restored = mapping; // The input may alias a binding we clear below.
        for (size_t i = 0; i < bindings_.size(); ++i) {
            if (bindings_[i].assigned && bindings_[i].channel == restored.channel && bindings_[i].cc == restored.cc) {
                clearBinding(static_cast<Parameter>(i));
            }
        }
        bindings_[index(parameter)] = restored;
        bindings_[index(parameter)].pickedUp = restored.mode != MidiControlMode::Absolute;
        lastValues_[index(parameter)] = -1;
        return true;
    }
    bool hasBinding(uint8_t channel, uint8_t cc) const {
        for (const auto &mapping : bindings_)
            if (mapping.assigned && mapping.channel == channel && mapping.cc == cc) return true;
        return false;
    }
    bool setBindingMode(Parameter parameter, MidiControlMode mode) {
        const MidiBinding old = binding(parameter);
        if (!old.assigned) return false;
        MidiBinding updated = old;
        updated.mode = mode;
        return setBinding(parameter, updated);
    }
    void clearBinding(Parameter parameter) {
        if (!valid(parameter)) return;
        bindings_[index(parameter)] = MidiBinding{};
        lastValues_[index(parameter)] = -1;
    }
    void clearBindings() {
        bindings_.fill(MidiBinding{});
        lastValues_.fill(-1);
        cancelLearn();
    }

    ControlResult applyCC(uint8_t channel, uint8_t cc, uint8_t value) {
        // 120..127 are channel-mode messages, never learnable synth controls.
        if (channel >= 16 || cc >= 120 || value >= 128) return ControlResult::Ignored;
        if (learning()) {
            const size_t learnedIndex = index(learnParameter_);
            for (size_t i = 0; i < bindings_.size(); ++i) {
                if (bindings_[i].assigned && bindings_[i].channel == channel && bindings_[i].cc == cc) {
                    clearBinding(static_cast<Parameter>(i));
                }
            }
            MidiBinding &learned = bindings_[learnedIndex];
            learned = MidiBinding{true, channel, cc, learnMode_, learnMode_ != MidiControlMode::Absolute};
            lastValues_[learnedIndex] = value;
            cancelLearn();
            // Learning only stores the assignment; it never changes the sound.
            return ControlResult::Learned;
        }

        for (size_t i = 0; i < bindings_.size(); ++i) {
            MidiBinding &mapped = bindings_[i];
            if (!mapped.assigned || mapped.channel != channel || mapped.cc != cc) continue;
            const Parameter parameter = static_cast<Parameter>(i);
            if (mapped.mode != MidiControlMode::Absolute) {
                // MiniLab relative encoders send an interleaved 0 neutral message.
                if (value == 0) return ControlResult::Ignored;
                int delta = 0;
                switch (mapped.mode) {
                    case MidiControlMode::RelativeOffset: delta = static_cast<int>(value) - 64; break;
                    case MidiControlMode::RelativeOffset16: delta = static_cast<int>(value) - 16; break;
                    case MidiControlMode::RelativeSignMagnitude:
                        delta = value < 64 ? value : -static_cast<int>(value & 63); break;
                    default:
                        if (value == 64) return ControlResult::Ignored;
                        delta = value < 64 ? static_cast<int>(value) : static_cast<int>(value) - 128;
                        break;
                }
                return adjust(parameter, delta) ? ControlResult::Changed : ControlResult::Ignored;
            }

            const int previous = lastValues_[i];
            lastValues_[i] = value;
            const float input = value / 127.0f;
            if (!mapped.pickedUp) {
                const float target = normalized(parameter);
                const bool close = oscillatorField(parameter) == 0
                                       ? waveformAt(input) == oscillatorConfig(config_, oscillatorIndex(parameter)).waveform
                                       : parameter == Parameter::Preset
                                       ? std::min(SOUND_COUNT - 1, static_cast<unsigned>(input * SOUND_COUNT)) == config_.preset
                                       : std::fabs(input - target) <= 1.0f / 127.0f;
                const bool crossed = previous >= 0 &&
                    ((previous / 127.0f <= target && input >= target) ||
                     (previous / 127.0f >= target && input <= target));
                if (!close && !crossed) return ControlResult::Ignored;
                mapped.pickedUp = true;
            }
            return setFromNormalized(parameter, input) ? ControlResult::Changed : ControlResult::Ignored;
        }
        return ControlResult::Ignored;
    }

    static const char *parameterName(Parameter parameter) {
        static constexpr const char *extra[] = {
            "Preset", "OSC1 Level", "OSC1 Coarse", "OSC1 Fine",
            "OSC2 Shape", "OSC2 Level", "OSC2 Coarse", "OSC2 Fine",
            "OSC3 Shape", "OSC3 Level", "OSC3 Coarse", "OSC3 Fine"
        };
        const unsigned id = static_cast<unsigned>(parameter);
        if (id >= 6 && id < PARAMETER_COUNT) return extra[id - 6];
        switch (parameter) {
            case Parameter::Attack: return "Attack";
            case Parameter::Decay: return "Decay";
            case Parameter::Sustain: return "Sustain";
            case Parameter::Release: return "Release";
            case Parameter::Waveform: return "Waveform";
            case Parameter::Volume: return "Volume";
            default: return "Unknown";
        }
    }
    static const char *waveformName(Waveform waveform) {
        switch (waveform) {
            case Waveform::Square: return "Square";
            case Waveform::Saw: return "Saw";
            case Waveform::Triangle: return "Triangle";
            case Waveform::Sine: return "Sine";
            default: return "Unknown";
        }
    }
    static const char *modeName(MidiControlMode mode) {
        switch (mode) {
            case MidiControlMode::RelativeOffset: return "REL OFFSET";
            case MidiControlMode::RelativeTwosComplement: return "REL 2'S";
            case MidiControlMode::RelativeOffset16: return "REL 16";
            case MidiControlMode::RelativeSignMagnitude: return "REL SIGN";
            default: return "ABS";
        }
    }
    void formatValue(Parameter parameter, char *buffer, size_t size) const {
        if (!buffer || size == 0) return;
        if (parameter == Parameter::Preset) {
            std::snprintf(buffer, size, "%s%s", soundName(config_.preset), config_.custom ? " *" : "");
            return;
        }
        const int osc = oscillatorIndex(parameter);
        if (osc >= 0) {
            const auto o = oscillatorConfig(config_, osc);
            switch (oscillatorField(parameter)) {
                case 0: std::snprintf(buffer, size, "%s", waveformName(o.waveform)); break;
                case 1: std::snprintf(buffer, size, "%.0f%%", o.level * 100); break;
                case 2: std::snprintf(buffer, size, "%+d st", o.coarse); break;
                default: std::snprintf(buffer, size, "%+.0f ct", o.fine); break;
            }
            return;
        }
        switch (parameter) {
            case Parameter::Attack: formatTime(config_.attackMs, buffer, size); break;
            case Parameter::Decay: formatTime(config_.decayMs, buffer, size); break;
            case Parameter::Release: formatTime(config_.releaseMs, buffer, size); break;
            case Parameter::Sustain: std::snprintf(buffer, size, "%d%%", static_cast<int>(std::lround(unit(config_.sustain) * 100))); break;
            case Parameter::Volume: std::snprintf(buffer, size, "%d%%", static_cast<int>(std::lround(unit(config_.volume) * 100))); break;
            case Parameter::Waveform: std::snprintf(buffer, size, "%s", waveformName(config_.waveform)); break;
            default: std::snprintf(buffer, size, "--"); break;
        }
    }
    void formatBinding(Parameter parameter, char *buffer, size_t size) const {
        if (!buffer || size == 0) return;
        const MidiBinding &mapped = binding(parameter);
        if (!mapped.assigned) {
            std::snprintf(buffer, size, "Unassigned");
        } else {
            std::snprintf(buffer, size, "Ch%u CC%u %s%s", static_cast<unsigned>(mapped.channel + 1),
                          static_cast<unsigned>(mapped.cc), modeName(mapped.mode),
                          mapped.mode == MidiControlMode::Absolute && !mapped.pickedUp ? " pickup" : "");
        }
    }

    static constexpr float MAX_ATTACK_MS = 2000.0f;
    static constexpr float MAX_DECAY_MS = 3000.0f;
    static constexpr float MAX_RELEASE_MS = 5000.0f;

private:
    static bool valid(Parameter parameter) { return static_cast<size_t>(parameter) < PARAMETER_COUNT; }
    static size_t index(Parameter parameter) { return static_cast<size_t>(parameter); }
    static float unit(float value) {
        if (!std::isfinite(value)) return 0.0f;
        return std::max(0.0f, std::min(1.0f, value));
    }
    static float timeNormalized(float milliseconds, float maximum) {
        return unit(std::log1p(std::max(0.0f, milliseconds)) / std::log1p(maximum));
    }
    static float timeAt(float normalizedValue, float maximum) {
        const float value = unit(normalizedValue);
        if (value == 0.0f) return 0.0f;
        if (value == 1.0f) return maximum;
        return std::expm1(std::log1p(maximum) * value);
    }
    static Waveform waveformAt(float normalizedValue) {
        return static_cast<Waveform>(std::min(3, static_cast<int>(unit(normalizedValue) * 4)));
    }
    static void formatTime(float milliseconds, char *buffer, size_t size) {
        if (milliseconds >= 1000.0f) std::snprintf(buffer, size, "%.2f s", milliseconds / 1000.0f);
        else if (milliseconds < 10.0f) std::snprintf(buffer, size, "%.1f ms", milliseconds);
        else std::snprintf(buffer, size, "%.0f ms", milliseconds);
    }
    void invalidatePickup(Parameter parameter) {
        MidiBinding &mapped = bindings_[index(parameter)];
        if (mapped.assigned && mapped.mode == MidiControlMode::Absolute) mapped.pickedUp = false;
    }
    bool setFromNormalized(Parameter parameter, float normalizedValue) {
        const float value = unit(normalizedValue);
        if (parameter == Parameter::Preset) {
            const unsigned next = std::min(SOUND_COUNT - 1, static_cast<unsigned>(value * SOUND_COUNT));
            if (next == config_.preset && !config_.custom) return false;
            config_ = soundPreset(next, config_.volume);
            for (size_t i = 0; i < PARAMETER_COUNT; ++i) invalidatePickup(static_cast<Parameter>(i));
            lastValues_.fill(-1);
            return true;
        }
        const int osc = oscillatorIndex(parameter);
        if (osc >= 0) {
            auto o = oscillatorConfig(config_, osc);
            const auto before = o;
            switch (oscillatorField(parameter)) {
                case 0: o.waveform = waveformAt(value); break;
                case 1: o.level = value; break;
                case 2: o.coarse = static_cast<int>(std::lround(value * 48)) - 24; break;
                default: o.fine = std::round(value * 200) - 100; break;
            }
            const bool changed = before.waveform != o.waveform || before.level != o.level ||
                                 before.coarse != o.coarse || before.fine != o.fine;
            if (changed) { setOscillatorConfig(config_, osc, o); config_.custom = true; }
            return changed;
        }
        float *field = nullptr;
        float next = value;
        switch (parameter) {
            case Parameter::Attack: field = &config_.attackMs; next = timeAt(value, MAX_ATTACK_MS); break;
            case Parameter::Decay: field = &config_.decayMs; next = timeAt(value, MAX_DECAY_MS); break;
            case Parameter::Sustain: field = &config_.sustain; break;
            case Parameter::Release: field = &config_.releaseMs; next = timeAt(value, MAX_RELEASE_MS); break;
            case Parameter::Volume: field = &config_.volume; break;
            case Parameter::Waveform: {
                const Waveform nextWaveform = waveformAt(value);
                const bool changed = config_.waveform != nextWaveform;
                config_.waveform = nextWaveform;
                return changed;
            }
            default: return false;
        }
        const bool changed = *field != next;
        *field = next;
        if (changed && parameter != Parameter::Volume) config_.custom = true;
        return changed;
    }

    SynthConfig config_{};
    std::array<MidiBinding, PARAMETER_COUNT> bindings_{};
    std::array<int16_t, PARAMETER_COUNT> lastValues_{};
    Parameter learnParameter_ = Parameter::Count;
    MidiControlMode learnMode_ = MidiControlMode::Absolute;
};
