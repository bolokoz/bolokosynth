/*
 * Verification evidence, presence debounce, wraparound and LED precedence.
 * Native assert-based executable; run without NDEBUG so checks remain enabled.
 * No USB/display hardware is exercised; on-board listening/UI checks are separate.
 */

#include "../src/ModuleHealth.h"
#include <cassert>
#include <cstdint>
#include <iostream>

static bool equal(StatusColor a, StatusColor b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}
static unsigned pulses(ModuleState max, ModuleState tft, unsigned start, unsigned end) {
    unsigned count = 0;
    bool previous = false;
    for (unsigned ms = start; ms < end; ++ms) {
        const auto color = diagnosticColor(ms, max, tft, false, true, true);
        const bool on = color.red != 0;
        if (on && !previous) ++count;
        previous = on;
    }
    return count;
}
int main() {
    ModuleHealth max;
    assert(max.state(false) == ModuleState::Unverified);
    assert(!max.confirm(true));
    assert(max.confirm(false));
    assert(max.state(false) == ModuleState::UserConfirmed);
    assert(max.state(true) == ModuleState::Fault);
    max.reportProblem();
    assert(max.state(false) == ModuleState::Fault);
    assert(max.confirm(false));
    max.enablePresence(true);
    assert(max.state(false) == ModuleState::Unverified);
    assert(!max.confirm(false));
    max.sample(100, true);
    max.sample(299, true);
    assert(max.state(false) == ModuleState::Unverified);
    max.sample(300, true);
    assert(max.state(false) == ModuleState::Missing);
    assert(!max.confirm(false));
    max.sample(400, false);
    max.sample(599, false);
    assert(max.state(false) == ModuleState::Missing);
    max.sample(600, false);
    assert(max.state(false) == ModuleState::GroundPresent);
    assert(max.confirm(false));
    max.sample(700, true);
    max.sample(800, false); // Brief bounce must not erase confirmation.
    max.sample(1000, false);
    assert(max.state(false) == ModuleState::UserConfirmed);
    max.sample(1100, true);
    max.sample(1300, true);
    assert(max.state(false) == ModuleState::Missing);
    max.sample(1400, false);
    max.sample(1600, false);
    assert(max.state(false) == ModuleState::GroundPresent); // Replug requires a new check.
    max.resetVerification();
    max.enablePresence(false);
    assert(max.state(false) == ModuleState::Unverified);
    max.sample(2000, true);
    assert(max.state(false) == ModuleState::Unverified);

    ModuleHealth wrapped;
    wrapped.enablePresence(true);
    wrapped.sample(UINT32_MAX - 99, false);
    wrapped.sample(100, false);
    assert(wrapped.state(false) == ModuleState::GroundPresent);

    const auto ok = ModuleState::UserConfirmed;
    const auto unknown = ModuleState::Unverified;
    const auto missing = ModuleState::Missing;
    assert(pulses(unknown, unknown, 0, 2000) == 2);
    assert(pulses(unknown, unknown, 2000, 4000) == 3);
    assert(equal(diagnosticColor(0, unknown, ok, false, true, true), {12, 6, 0}));
    assert(equal(diagnosticColor(2000, ok, unknown, false, true, true), {12, 0, 12}));
    assert(pulses(missing, ok, 0, 2000) == 2);
    assert(pulses(ok, missing, 0, 2000) == 3);
    assert(pulses(missing, missing, 0, 2000) == 2);
    assert(pulses(missing, missing, 2000, 4000) == 3);
    assert(equal(diagnosticColor(0, missing, ok, false, true, true), {48, 24, 0}));
    assert(equal(diagnosticColor(0, ok, missing, false, true, true), {48, 0, 48}));
    assert(equal(diagnosticColor(0, ok, ok, true, true, true), {48, 0, 0}));
    assert(equal(diagnosticColor(150, ok, ok, true, true, true), {}));
    assert(equal(diagnosticColor(4500, unknown, unknown, false, true, true), {0, 0, 64}));
    assert(equal(diagnosticColor(4500, ok, ok, false, false, true), {0, 8, 0}));
    assert(equal(diagnosticColor(4000, ok, ok, false, false, false), {32, 16, 0}));
    assert(equal(diagnosticColor(4500, ok, ok, false, false, false), {}));
    std::cout << "Module states, presence debounce/unplug/replug/wrap, confirmation and LED pattern tests passed.\n";
}
