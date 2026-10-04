/*
 * Portable single-voice DSP used by the v1 three-voice wrapper and native tests.
 * ADSR durations round to at least one sample, including UI values of zero,
 * to avoid division by zero. Live sustain moves use a short smoothing ramp.
 * Three oscillators share one envelope, normalize their levels and smooth
 * volume/shape/pitch edits; PolyBLEP reduces square/saw aliasing, while
 * triangle remains naive. Oscillator increments are capped below Nyquist.
 * Its last-held-note stack is retained for legacy mono tests; PolySynth sends
 * only one assigned key to each instance. No Arduino or heap dependency.
 */

#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include "SynthConfig.h"

class AdsrEnvelope {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    explicit AdsrEnvelope(uint32_t rate = 48000)
        : rate_(rate), attackSamples_(rate * 5 / 1000), decaySamples_(rate * 50 / 1000),
          releaseSamples_(rate * 200 / 1000) {}

    void configure(const SynthConfig &config) {
        attackSamples_ = samples(config.attackMs);
        decaySamples_ = samples(config.decayMs);
        releaseSamples_ = samples(config.releaseMs);
        const float sustain = config.sustain < 0 ? 0 : (config.sustain > 1 ? 1 : config.sustain);
        if (sustain != sustainLevel_) {
            sustainLevel_ = sustain;
            if (stage_ == Stage::Decay) {
                // Retarget the active decay without shortening its remaining time.
                ramp(Stage::Decay, sustainLevel_, duration_ - position_);
            } else if (stage_ == Stage::Sustain) {
                ramp(Stage::Decay, sustainLevel_, rate_ * 5 / 1000);
            }
        }
    }

    void noteOn() { ramp(Stage::Attack, 1.0f, attackSamples_); }
    void noteOff() {
        if (stage_ != Stage::Idle) {
            ramp(Stage::Release, 0.0f, releaseSamples_);
        }
    }
    void reset() { stage_ = Stage::Idle; level_ = 0.0f; }
    float level() const { return level_; }
    Stage stage() const { return stage_; }
    const char *stageName() const {
        switch (stage_) {
            case Stage::Attack: return "attack";
            case Stage::Decay: return "decay";
            case Stage::Sustain: return "sustain";
            case Stage::Release: return "release";
            default: return "idle";
        }
    }

    float next() {
        if (stage_ == Stage::Idle) {
            return 0.0f;
        }
        if (stage_ == Stage::Sustain) {
            return level_;
        }
        ++position_;
        level_ = start_ + (target_ - start_) * static_cast<float>(position_) / duration_;
        if (position_ >= duration_) {
            level_ = target_;
            if (stage_ == Stage::Attack) {
                ramp(Stage::Decay, sustainLevel_, decaySamples_);
            } else if (stage_ == Stage::Decay) {
                stage_ = Stage::Sustain;
            } else {
                reset();
            }
        }
        return level_;
    }

private:
    uint32_t samples(float ms) const {
        if (!std::isfinite(ms) || ms < 0) ms = 0;
        if (ms > 5000) ms = 5000;
        const uint32_t count = static_cast<uint32_t>(std::lround(rate_ * ms / 1000.0f));
        return count ? count : 1;
    }
    uint32_t rate_;
    float sustainLevel_ = 0.7f;
    void ramp(Stage stage, float target, uint32_t duration) {
        stage_ = stage;
        start_ = level_;
        target_ = target;
        duration_ = duration ? duration : 1;
        position_ = 0;
    }
    uint32_t attackSamples_, decaySamples_, releaseSamples_;
    uint32_t position_ = 0, duration_ = 1;
    float level_ = 0.0f, start_ = 0.0f, target_ = 0.0f;
    Stage stage_ = Stage::Idle;
};

// Monophonic, last-held-note priority, with MIDI channels kept independent.
class MonoSynth {
public:
    explicit MonoSynth(uint32_t rate = 48000, int16_t amplitude = DEFAULT_SIGNAL_AMPLITUDE)
        : rate_(rate), amplitude_(amplitude), envelope_(rate) { pitchBends_.fill(8192); }

    void configure(const SynthConfig &config) {
        envelope_.configure(config);
        volumeTarget_ = config.volume < 0 ? 0 : (config.volume > 1 ? 1 : config.volume);
        for (unsigned i = 0; i < 3; ++i) {
            const auto settings = oscillatorConfig(config, i);
            auto &o = oscillators_[i];
            if (settings.waveform != o.waveform) {
                for (size_t j = 0; j < o.from.size(); ++j)
                    o.from[j] = o.from[j] * (1 - o.blend) +
                                (j == static_cast<size_t>(o.waveform) ? o.blend : 0);
                o.waveform = settings.waveform;
                o.blend = 0;
            }
            o.levelTarget = std::fmax(0, std::fmin(1, settings.level));
            o.ratio = std::pow(2.0f, (settings.coarse + settings.fine / 100.0f) / 12.0f);
            o.incrementTarget = std::fmin(0.45f, frequency_ * o.ratio / rate_);
        }
    }

    // MIDI pitch bend is two 7-bit bytes (LSB first), center 8192.
    // Keep channel state even before Note On; never restart the ADSR.
    bool pitchBend(uint8_t channel, uint8_t lsb, uint8_t msb) {
        if (channel >= 16 || lsb >= 128 || msb >= 128) return false;
        pitchBends_[channel] = static_cast<uint16_t>(lsb | (uint16_t(msb) << 7));
        if (channel == voiceChannel_) updateBentPitch(false);
        return true;
    }
    void resetPitchBend(uint8_t channel) {
        if (channel >= 16) return;
        pitchBends_[channel] = 8192;
        if (channel == voiceChannel_) updateBentPitch(false);
    }
    float bendSemitones(uint8_t channel) const {
        if (channel >= 16) return 0;
        const int value = static_cast<int>(pitchBends_[channel]) - 8192;
        return 2.0f * value / (value < 0 ? 8192.0f : 8191.0f);
    }

    bool modulation(uint8_t channel, uint8_t value) {
        if (channel >= 16 || value >= 128) return false;
        modulation_[channel] = value;
        return true;
    }
    uint8_t modulationValue(uint8_t channel) const { return channel < 16 ? modulation_[channel] : 0; }
    float vibratoSemitones() const { return 12 * std::log2(vibratoFactor_); }
    void resetControllers(uint8_t channel) {
        resetPitchBend(channel);
        if (channel < 16) modulation_[channel] = 0;
    }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        if (channel >= 16 || note >= 128 || velocity >= 128) return;
        if (velocity == 0) {
            noteOff(channel, note);
            return;
        }
        const size_t index = channel * 128 + note;
        if (order_[index] == 0) ++heldCount_;
        order_[index] = ++sequence_;
        velocity_[index] = velocity;
        select(index);
        envelope_.noteOn();
    }

    void noteOff(uint8_t channel, uint8_t note) {
        if (channel >= 16 || note >= 128) return;
        const size_t index = channel * 128 + note;
        if (order_[index] == 0) return;
        order_[index] = 0;
        --heldCount_;
        if (current_ == static_cast<int>(index)) {
            if (!selectNewest()) envelope_.noteOff();
        }
    }

    void allNotesOff(uint8_t channel, bool immediate = false) {
        if (channel >= 16) return;
        for (size_t note = 0; note < 128; ++note) {
            const size_t index = channel * 128 + note;
            if (order_[index]) {
                order_[index] = 0;
                --heldCount_;
            }
        }
        if (current_ >= 0 && current_ / 128 == channel) {
            if (selectNewest()) {
                if (immediate) {
                    envelope_.reset();
                    envelope_.noteOn();
                }
            } else if (immediate) {
                envelope_.reset();
            } else {
                envelope_.noteOff();
            }
        } else if (heldCount_ == 0 && immediate && voiceChannel_ == channel) {
            // All Sound Off must also cut a release tail.
            envelope_.reset();
        }
    }

    void panic() {
        order_.fill(0);
        heldCount_ = 0;
        current_ = -1;
        envelope_.reset();
        pitchBends_.fill(8192);
        modulation_.fill(0);
        vibratoDepth_ = 0; vibratoFactor_ = vibratoTarget_ = 1;
        updateBentPitch(false);
    }

    int16_t nextSample() {
        const float volumeStep = 1.0f / (rate_ * 0.01f);
        if (volume_ < volumeTarget_) {
            volume_ = std::fmin(volumeTarget_, volume_ + volumeStep);
        } else if (volume_ > volumeTarget_) {
            volume_ = std::fmax(volumeTarget_, volume_ - volumeStep);
        }
        const float depthTarget = modulation_[voiceChannel_] / 127.0f;
        const float depthStep = 1.0f / (rate_ * 0.01f);
        vibratoDepth_ += std::fmax(-depthStep, std::fmin(depthStep, depthTarget - vibratoDepth_));
        vibratoPhase_ += 5.0f / rate_;
        if (vibratoPhase_ >= 1) vibratoPhase_ -= 1;
        // Evaluate the LFO every 32 frames, smoothing between updates.
        if (++vibratoFrames_ >= 32) {
            vibratoFrames_ = 0;
            vibratoTarget_ = std::pow(2.0f, 0.5f * vibratoDepth_ *
                std::sin(6.28318530718f * vibratoPhase_) / 12.0f);
        }
        vibratoFactor_ += (vibratoTarget_ - vibratoFactor_) / 8.0f;
        float wave = 0, levelSum = 0;
        for (auto &o : oscillators_) {
            const float step = 1.0f / (rate_ * 0.01f);
            o.level += std::fmax(-step, std::fmin(step, o.levelTarget - o.level));
            // Pitch edits glide briefly; MIDI note selection itself is immediate.
            o.increment += (o.incrementTarget - o.increment) / (rate_ * 0.002f);
            const float increment = std::fmin(0.45f, o.increment * vibratoFactor_);
            float value = oscillator(o.waveform, o.phase, increment);
            if (o.blend < 1) {
                float previous = 0;
                for (size_t j = 0; j < o.from.size(); ++j)
                    if (o.from[j] != 0)
                        previous += o.from[j] * oscillator(static_cast<Waveform>(j), o.phase, increment);
                value = previous + (value - previous) * o.blend;
                o.blend = std::fmin(1.0f, o.blend + 1.0f / (rate_ * 0.005f));
            }
            wave += value * o.level;
            levelSum += o.level;
            o.phase += increment;
            if (o.phase >= 1.0f) o.phase -= 1.0f;
        }
        // Normalize the mix so three oscillators never triple the output level.
        wave /= std::fmax(1.0f, levelSum);
        const float gain = envelope_.next() * velocityGain_ * volume_;
        const float sample = wave * amplitude_ * gain;
        return static_cast<int16_t>(std::lround(sample));
    }

    float oscillatorFrequency(unsigned index) const {
        return index < 3 ? oscillators_[index].incrementTarget * rate_ : 0;
    }

    float frequency() const { return frequency_; }
    float envelopeLevel() const { return envelope_.level(); }
    AdsrEnvelope::Stage stage() const { return envelope_.stage(); }
    const char *stageName() const { return envelope_.stageName(); }
    uint16_t heldCount() const { return heldCount_; }
    int currentNote() const { return current_ < 0 ? -1 : current_ % 128; }

private:
    static float blep(float phase, float increment) {
        if (phase < increment) {
            const float x = phase / increment;
            return x + x - x * x - 1;
        }
        if (phase > 1 - increment) {
            const float x = (phase - 1) / increment;
            return x * x + x + x + 1;
        }
        return 0;
    }
    static float oscillator(Waveform waveform, float phase, float increment) {
        switch (waveform) {
            case Waveform::Saw:
                return 2 * phase - 1 - blep(phase, increment);
            case Waveform::Triangle:
                return 1 - 4 * std::fabs(phase - 0.5f);
            case Waveform::Sine:
                return std::sin(6.28318530718f * phase);
            default: {
                float shifted = phase + 0.5f;
                if (shifted >= 1) shifted -= 1;
                return (phase < 0.5f ? 1.0f : -1.0f) + blep(phase, increment) - blep(shifted, increment);
            }
        }
    }
    void select(size_t index) {
        current_ = static_cast<int>(index);
        voiceChannel_ = static_cast<uint8_t>(index / 128);
        baseFrequency_ = 440.0f * std::pow(2.0f, (static_cast<int>(index % 128) - 69) / 12.0f);
        velocityGain_ = velocity_[index] / 127.0f;
        updateBentPitch(true);
    }
    void updateBentPitch(bool immediate) {
        frequency_ = baseFrequency_ * std::pow(2.0f, bendSemitones(voiceChannel_) / 12.0f);
        for (auto &o : oscillators_) {
            o.incrementTarget = std::fmin(0.45f, frequency_ * o.ratio / rate_);
            if (immediate) o.increment = o.incrementTarget;
        }
    }
    bool selectNewest() {
        uint32_t newest = 0;
        int selected = -1;
        for (size_t index = 0; index < order_.size(); ++index) {
            if (order_[index] > newest) {
                newest = order_[index];
                selected = static_cast<int>(index);
            }
        }
        if (selected < 0) {
            current_ = -1;
            return false;
        }
        select(static_cast<size_t>(selected));
        return true;
    }

    uint32_t rate_;
    int16_t amplitude_;
    AdsrEnvelope envelope_;
    std::array<uint32_t, 16 * 128> order_{};
    std::array<uint8_t, 16 * 128> velocity_{};
    uint32_t sequence_ = 0;
    uint16_t heldCount_ = 0;
    int current_ = -1;
    uint8_t voiceChannel_ = 0;
    std::array<uint16_t, 16> pitchBends_{};
    std::array<uint8_t, 16> modulation_{};
    float vibratoDepth_ = 0, vibratoPhase_ = 0, vibratoFactor_ = 1, vibratoTarget_ = 1;
    unsigned vibratoFrames_ = 0;
    float baseFrequency_ = 440.0f;
    struct Oscillator {
        Waveform waveform = Waveform::Square;
        std::array<float, 4> from{1, 0, 0, 0};
        float blend = 1;
        float level = 1, levelTarget = 1;
        float ratio = 1, increment = 440.0f / 48000, incrementTarget = 440.0f / 48000;
        float phase = 0;
    };
    std::array<Oscillator, 3> oscillators_{{
        {},
        {Waveform::Saw, {0, 1, 0, 0}, 1, 0, 0},
        {Waveform::Sine, {0, 0, 0, 1}, 1, 0, 0}
    }};
    float volume_ = DEFAULT_VOLUME, volumeTarget_ = DEFAULT_VOLUME;
    float frequency_ = 440.0f, velocityGain_ = 0.0f;
};
