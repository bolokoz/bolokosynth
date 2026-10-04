/*
 * Three-note v1 voice allocator around the independently tested MonoSynth core.
 * Every slot has its own ADSR, velocity and three oscillators. Prefer idle,
 * then released, then oldest held slots; a fourth note truncates an old voice.
 * Track physical keys separately so a stolen note's later Note Off is harmless;
 * stolen notes do not resume and repeated Note On retriggers one slot.
 * Fixed divide-by-three headroom preserves the previous worst-case signal
 * ceiling without gain pumping. Solo notes are quieter than the mono test.
 * Patches and master volume are shared; bend/modulation are per MIDI channel.
 */

#pragma once
#include "MonoSynth.h"
#include <algorithm>
#include <array>
#include <cstdint>

// Fixed voice pool: no heap allocation or locks in the audio sample path.
class PolySynth {
public:
    static constexpr unsigned VOICE_COUNT = 3;
    explicit PolySynth(uint32_t rate = 48000, int16_t amplitude = DEFAULT_SIGNAL_AMPLITUDE)
        : voices_{{MonoSynth(rate, amplitude), MonoSynth(rate, amplitude), MonoSynth(rate, amplitude)}} {
        bends_.fill(8192);
    }
    void configure(const SynthConfig &config) {
        config_ = config;
        for (auto &voice : voices_) voice.configure(config);
    }
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        if (channel >= 16 || note >= 128 || velocity >= 128) return;
        if (!velocity) { noteOff(channel, note); return; }
        const unsigned key = channel * 128 + note;
        if (!held_[key]) { held_[key] = true; ++heldCount_; }
        unsigned slot = VOICE_COUNT;
        // Repeated Note On retriggers one slot, rather than creating a voice
        // that would remain stuck after a single matching Note Off.
        for (unsigned i = 0; i < VOICE_COUNT; ++i)
            if (keys_[i] == static_cast<int>(key)) { slot = i; break; }
        if (slot == VOICE_COUNT) {
            for (unsigned i = 0; i < VOICE_COUNT; ++i)
                if (voices_[i].stage() == AdsrEnvelope::Stage::Idle) { slot = i; break; }
        }
        if (slot == VOICE_COUNT) {
            // Preserve held notes while any released slot can be reused.
            for (unsigned i = 0; i < VOICE_COUNT; ++i)
                if (!gates_[i] && (slot == VOICE_COUNT || ages_[i] < ages_[slot])) slot = i;
        }
        if (slot == VOICE_COUNT) {
            slot = 0;
            for (unsigned i = 1; i < VOICE_COUNT; ++i)
                if (ages_[i] < ages_[slot]) slot = i;
        }
        auto &voice = voices_[slot];
        voice.panic(); // Clear the stolen tail and its monophonic note stack.
        voice.configure(config_);
        voice.pitchBend(channel, bends_[channel] & 127, bends_[channel] >> 7);
        voice.modulation(channel, modulation_[channel]);
        keys_[slot] = static_cast<int>(key);
        gates_[slot] = true;
        ages_[slot] = ++sequence_;
        voice.noteOn(channel, note, velocity);
    }
    void noteOff(uint8_t channel, uint8_t note) {
        if (channel >= 16 || note >= 128) return;
        const unsigned key = channel * 128 + note;
        if (!held_[key]) return;
        held_[key] = false; --heldCount_;
        for (unsigned i = 0; i < VOICE_COUNT; ++i) {
            if (keys_[i] != static_cast<int>(key) || !gates_[i]) continue;
            gates_[i] = false;
            voices_[i].noteOff(channel, note);
        }
        // A stolen key remains physically held, but its eventual Note Off
        // cannot release the replacement voice. Stolen notes do not resume.
    }
    void allNotesOff(uint8_t channel, bool immediate = false) {
        if (channel >= 16) return;
        for (unsigned note = 0; note < 128; ++note) {
            const unsigned key = channel * 128 + note;
            if (held_[key]) { held_[key] = false; --heldCount_; }
        }
        for (unsigned i = 0; i < VOICE_COUNT; ++i) {
            if (keys_[i] < 0 || keys_[i] / 128 != channel) continue;
            gates_[i] = false;
            voices_[i].allNotesOff(channel, immediate);
            if (immediate) keys_[i] = -1;
        }
    }
    bool pitchBend(uint8_t channel, uint8_t lsb, uint8_t msb) {
        if (channel >= 16 || lsb >= 128 || msb >= 128) return false;
        bends_[channel] = lsb | (uint16_t(msb) << 7);
        for (auto &voice : voices_) voice.pitchBend(channel, lsb, msb);
        return true;
    }
    bool modulation(uint8_t channel, uint8_t value) {
        if (channel >= 16 || value >= 128) return false;
        modulation_[channel] = value;
        for (auto &voice : voices_) voice.modulation(channel, value);
        return true;
    }
    void resetControllers(uint8_t channel) {
        if (channel >= 16) return;
        bends_[channel] = 8192; modulation_[channel] = 0;
        for (auto &voice : voices_) voice.resetControllers(channel);
    }
    void panic() {
        for (auto &voice : voices_) voice.panic();
        held_.fill(false); heldCount_ = 0;
        keys_.fill(-1); gates_.fill(false);
        bends_.fill(8192); modulation_.fill(0);
    }
    int16_t nextSample() {
        int32_t sum = 0;
        for (auto &voice : voices_) sum += voice.nextSample();
        // Constant headroom avoids clipping and avoids changing a held
        // note's gain when another note starts or its release becomes idle.
        // One note is quieter than the former mono engine; master is shared.
        return static_cast<int16_t>(sum / static_cast<int32_t>(VOICE_COUNT));
    }
    unsigned activeVoiceCount() const {
        unsigned count = 0;
        for (const auto &voice : voices_)
            count += voice.stage() != AdsrEnvelope::Stage::Idle;
        return count;
    }
    bool hasVoice(uint8_t channel, uint8_t note) const {
        if (channel >= 16 || note >= 128) return false;
        for (unsigned i = 0; i < VOICE_COUNT; ++i)
            if (keys_[i] == channel * 128 + note &&
                voices_[i].stage() != AdsrEnvelope::Stage::Idle) return true;
        return false;
    }
    uint16_t heldCount() const { return heldCount_; }
    // Existing telemetry remains representative of the newest sounding voice,
    // while activeVoiceCount explicitly reports total sounding voices/tails.
    float frequency() const { return voices_[representative()].frequency(); }
    float envelopeLevel() const { return voices_[representative()].envelopeLevel(); }
    AdsrEnvelope::Stage stage() const { return voices_[representative()].stage(); }
    const char *stageName() const { return voices_[representative()].stageName(); }
    float bendSemitones(uint8_t channel) const {
        if (channel >= 16) return 0;
        const int value = static_cast<int>(bends_[channel]) - 8192;
        return 2.0f * value / (value < 0 ? 8192.0f : 8191.0f);
    }

private:
    unsigned representative() const {
        unsigned best = 0;
        uint64_t newest = 0;
        for (unsigned i = 0; i < VOICE_COUNT; ++i)
            if (voices_[i].stage() != AdsrEnvelope::Stage::Idle && ages_[i] > newest) {
                best = i; newest = ages_[i];
            }
        return best;
    }
    std::array<MonoSynth, VOICE_COUNT> voices_;
    std::array<int, VOICE_COUNT> keys_{{-1, -1, -1}};
    std::array<bool, VOICE_COUNT> gates_{};
    std::array<uint64_t, VOICE_COUNT> ages_{};
    std::array<bool, 16 * 128> held_{};
    std::array<uint16_t, 16> bends_{};
    std::array<uint8_t, 16> modulation_{};
    uint64_t sequence_ = 0;
    uint16_t heldCount_ = 0;
    SynthConfig config_{};
};
