/*
 * Module verification states and RGB diagnostic patterns, independent of hardware.
 * I2S MAX and write-only SPI TFT cannot acknowledge connection. Optional input
 * ground-return sensing proves only that return wire, never power/data correctness.
 * Debounce distinguishes unplugging from glitches; unsigned elapsed times handle
 * millis wrap. Human confirmation and reported faults remain separate evidence.
 */

#pragma once
#include <cstdint>

enum class ModuleState : uint8_t { Unverified, GroundPresent, UserConfirmed, Missing, Fault };

// No I2S/SPI acknowledgment exists on these hookups. Optional sensing only
// verifies a separate ground-return wire, never power/data/display correctness.
class ModuleHealth {
public:
    void enablePresence(bool enabled) {
        presenceEnabled_ = enabled;
        sampled_ = stableKnown_ = confirmed_ = false;
    }
    bool presenceEnabled() const { return presenceEnabled_; }
    void sample(uint32_t now, bool high) {
        if (!presenceEnabled_) return;
        if (!sampled_ || high != rawHigh_) {
            sampled_ = true;
            rawHigh_ = high;
            rawSince_ = now;
        }
        if (now - rawSince_ >= 200) {
            stableHigh_ = rawHigh_;
            stableKnown_ = true;
            if (stableHigh_) confirmed_ = false;
        }
    }
    bool confirm(bool softwareError) {
        if (softwareError || (presenceEnabled_ && (!stableKnown_ || stableHigh_))) return false;
        confirmed_ = true;
        reportedProblem_ = false;
        return true;
    }
    void reportProblem() { reportedProblem_ = true; confirmed_ = false; }
    void resetVerification() { confirmed_ = reportedProblem_ = false; }
    ModuleState state(bool softwareError) const {
        if (softwareError || reportedProblem_) return ModuleState::Fault;
        if (presenceEnabled_) {
            if (!stableKnown_) return ModuleState::Unverified;
            if (stableHigh_) return ModuleState::Missing;
        }
        if (confirmed_) return ModuleState::UserConfirmed;
        return presenceEnabled_ ? ModuleState::GroundPresent : ModuleState::Unverified;
    }
    static const char *name(ModuleState state) {
        switch (state) {
            case ModuleState::GroundPresent: return "ground-present/unverified";
            case ModuleState::UserConfirmed: return "user-confirmed";
            case ModuleState::Missing: return "presence-wire-missing";
            case ModuleState::Fault: return "fault";
            default: return "unverified";
        }
    }
private:
    bool presenceEnabled_ = false, sampled_ = false, stableKnown_ = false;
    bool rawHigh_ = true, stableHigh_ = true;
    bool confirmed_ = false, reportedProblem_ = false;
    uint32_t rawSince_ = 0;
};

struct StatusColor { uint8_t red = 0, green = 0, blue = 0; };

inline bool moduleFault(ModuleState state) {
    return state == ModuleState::Fault || state == ModuleState::Missing;
}
inline bool moduleUnverified(ModuleState state) {
    return state == ModuleState::Unverified || state == ModuleState::GroundPresent;
}
inline StatusColor moduleFlashes(uint32_t phase, unsigned count, bool tft, bool bright) {
    const bool on = phase < count * 400 && phase % 400 < 120;
    if (!on) return {};
    const uint8_t level = bright ? 48 : 12;
    return tft ? StatusColor{level, 0, level} : StatusColor{level, uint8_t(level / 2), 0};
}

// Pure, nonblocking LED schedule. Module failures cannot be hidden by MIDI traffic.
inline StatusColor diagnosticColor(uint32_t now, ModuleState maxState, ModuleState tftState,
                                    bool systemError, bool midiRecent, bool usbPresent) {
    const bool maxFault = moduleFault(maxState), tftFault = moduleFault(tftState);
    if (maxFault || tftFault) {
        if (maxFault && tftFault) {
            const uint32_t phase = now % 4000;
            return phase < 2000 ? moduleFlashes(phase, 2, false, true)
                                : moduleFlashes(phase - 2000, 3, true, true);
        }
        return moduleFlashes(now % 2000, tftFault ? 3 : 2, tftFault, true);
    }
    if (systemError) return now % 300 < 150 ? StatusColor{48, 0, 0} : StatusColor{};
    const uint32_t phase = now % 8000;
    if (moduleUnverified(maxState) && phase < 2000)
        return moduleFlashes(phase, 2, false, false);
    if (moduleUnverified(tftState) && phase >= 2000 && phase < 4000)
        return moduleFlashes(phase - 2000, 3, true, false);
    if (midiRecent) return {0, 0, 64};
    if (usbPresent) return {0, uint8_t(now % 1000 < 150 ? 48 : 8), 0};
    return now % 1000 < 500 ? StatusColor{32, 16, 0} : StatusColor{};
}
