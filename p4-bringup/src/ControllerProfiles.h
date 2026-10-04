/*
 * Controller identities and defaults, separate from generic synthesis/MIDI Learn.
 * MiniLab MkII matches exact VID/PID and assumes factory Preset 1. Other Arturia
 * devices or custom memories must use Generic/Learn or a new explicit profile.
 * Sixteen rotations and two click messages are distinct, not eighteen rotations.
 * Click actions trigger on press edges; defaults are replaceable by learned CCs.
 */

#pragma once
#include "SynthControls.h"
#include <array>

enum class ControllerProfile : uint8_t { Generic, MiniLabMkII };
struct ControllerKnob {
    unsigned number;
    uint8_t cc;
    Parameter parameter;
    MidiControlMode mode;
};

// Preset 1 CC identifiers/modes cross-checked against Apple's bundled
// Arturia MiniLab mkII.device/config.lua; synth target choices are ours.
inline const std::array<ControllerKnob,16> &miniLabKnobs() {
    static const std::array<ControllerKnob,16> knobs{{
        {1,112,Parameter::Preset,MidiControlMode::RelativeSignMagnitude},
        {2,74,Parameter::Attack,MidiControlMode::Absolute},
        {3,71,Parameter::Decay,MidiControlMode::Absolute},
        {4,76,Parameter::Sustain,MidiControlMode::Absolute},
        {5,77,Parameter::Release,MidiControlMode::Absolute},
        {6,93,Parameter::Waveform,MidiControlMode::Absolute},
        {7,73,Parameter::Osc1Level,MidiControlMode::Absolute},
        {8,75,Parameter::Osc1Fine,MidiControlMode::Absolute},
        {9,114,Parameter::Osc3Wave,MidiControlMode::RelativeSignMagnitude},
        {10,18,Parameter::Osc2Wave,MidiControlMode::Absolute},
        {11,19,Parameter::Osc2Level,MidiControlMode::Absolute},
        {12,16,Parameter::Osc2Coarse,MidiControlMode::Absolute},
        {13,17,Parameter::Osc2Fine,MidiControlMode::Absolute},
        {14,91,Parameter::Osc3Level,MidiControlMode::Absolute},
        {15,79,Parameter::Osc3Fine,MidiControlMode::Absolute},
        {16,72,Parameter::Volume,MidiControlMode::Absolute}
    }};
    return knobs;
}
inline const char *profileName(ControllerProfile profile) {
    return profile == ControllerProfile::MiniLabMkII ? "MiniLab MkII" : "Generic / Learn";
}
inline ControllerProfile profileForUsb(uint16_t vid, uint16_t pid) {
    // Confirmed on the user's MiniLab MkII; do not match every Arturia product.
    return vid == 0x1C75 && pid == 0x0289 ? ControllerProfile::MiniLabMkII : ControllerProfile::Generic;
}
inline void loadControllerProfile(SynthControls &controls, ControllerProfile profile) {
    controls.clearBindings();
    controls.setLearnMode(MidiControlMode::Absolute);
    if (profile == ControllerProfile::MiniLabMkII)
        for (const auto &knob : miniLabKnobs())
            controls.setBinding(knob.parameter, MidiBinding{true,0,knob.cc,knob.mode,false});
}
inline MidiControlMode profileMode(ControllerProfile profile, uint8_t cc, MidiControlMode fallback) {
    if (profile == ControllerProfile::MiniLabMkII)
        for (const auto &knob : miniLabKnobs()) if (knob.cc == cc) return knob.mode;
    return fallback;
}
enum class ControllerAction : uint8_t { None, ReloadSound, Panic };
class ControllerButtons {
public:
    ControllerAction apply(ControllerProfile profile, uint8_t channel, uint8_t cc, uint8_t value) {
        if (profile != ControllerProfile::MiniLabMkII || channel != 0 || value >= 128) return ControllerAction::None;
        const int index = cc == 113 ? 0 : cc == 115 ? 1 : -1;
        if (index < 0) return ControllerAction::None;
        const bool pressed = value != 0, rising = pressed && !pressed_[index];
        pressed_[index] = pressed;
        return !rising ? ControllerAction::None : index == 0 ? ControllerAction::ReloadSound : ControllerAction::Panic;
    }
    void reset() { pressed_.fill(false); }
private:
    std::array<bool,2> pressed_{};
};
