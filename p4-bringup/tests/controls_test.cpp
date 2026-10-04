/*
 * Parameter ranges, relative encodings, soft pickup and assignment restoration.
 * Native assert-based executable; run without NDEBUG so checks remain enabled.
 * No USB/display hardware is exercised; on-board listening/UI checks are separate.
 */

#include "../src/SynthControls.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

static bool near(float a, float b, float tolerance = 0.00001f) {
    return std::fabs(a - b) <= tolerance;
}

static void testEncoderRanges() {
    SynthControls controls;
    assert(near(controls.config().attackMs, 5));
    assert(near(controls.config().decayMs, 50));
    assert(near(controls.config().sustain, 0.7f));
    assert(near(controls.config().releaseMs, 200));
    assert(near(controls.config().volume, 0.50f));
    assert(controls.config().waveform == Waveform::Square);
    for (size_t i = 0; i < PARAMETER_COUNT; ++i) assert(!controls.binding(static_cast<Parameter>(i)).assigned);
    assert(controls.adjust(Parameter::Attack, 1));
    assert(controls.config().attackMs > 5 && controls.config().attackMs < 6);
    assert(controls.adjust(Parameter::Attack, 10000));
    assert(controls.config().attackMs == SynthControls::MAX_ATTACK_MS);
    assert(!controls.adjust(Parameter::Attack, 1));
    assert(controls.adjust(Parameter::Attack, -10000));
    assert(controls.config().attackMs == 0);
    assert(controls.adjust(Parameter::Decay, 10000));
    assert(controls.config().decayMs == SynthControls::MAX_DECAY_MS);
    assert(controls.adjust(Parameter::Release, 10000));
    assert(controls.config().releaseMs == SynthControls::MAX_RELEASE_MS);
    assert(controls.adjust(Parameter::Sustain, 10));
    assert(near(controls.config().sustain, 0.8f));
    assert(controls.adjust(Parameter::Volume, -10));
    assert(near(controls.config().volume, 0.40f));
    assert(controls.adjust(Parameter::Waveform, -1));
    assert(controls.config().waveform == Waveform::Sine);
    assert(controls.adjust(Parameter::Waveform, 1));
    assert(controls.config().waveform == Waveform::Square);
    assert(!controls.adjust(Parameter::Waveform, 8));
    assert(!controls.adjust(Parameter::Count, 1));
    assert(!controls.setNormalized(Parameter::Volume, std::numeric_limits<float>::quiet_NaN()));
}

static void testLearnAndPickup() {
    SynthControls controls;
    // Explicitly exercise pickup at the maximum, independent of the quiet default.
    assert(controls.setNormalized(Parameter::Volume, 1));
    controls.beginLearn(Parameter::Volume);
    assert(controls.learning() && controls.learningParameter() == Parameter::Volume);
    assert(controls.applyCC(0, 74, 0) == ControlResult::Learned);
    assert(!controls.learning());
    assert(controls.config().volume == 1); // Learning never changes sound.
    assert(!controls.binding(Parameter::Volume).pickedUp);
    assert(controls.applyCC(1, 74, 127) == ControlResult::Ignored); // Wrong channel.
    assert(controls.applyCC(0, 75, 127) == ControlResult::Ignored); // Wrong CC.
    assert(controls.applyCC(0, 74, 1) == ControlResult::Ignored);
    assert(controls.config().volume == 1);
    assert(controls.applyCC(0, 74, 127) == ControlResult::Ignored); // At pickup target, unchanged.
    assert(controls.binding(Parameter::Volume).pickedUp);
    assert(controls.applyCC(0, 74, 64) == ControlResult::Changed);
    assert(near(controls.config().volume, 64.0f / 127));
    assert(controls.adjust(Parameter::Volume, -25));
    const float encoderValue = controls.config().volume;
    assert(!controls.binding(Parameter::Volume).pickedUp);
    assert(controls.applyCC(0, 74, 65) == ControlResult::Ignored);
    assert(controls.config().volume == encoderValue);
    assert(controls.applyCC(0, 74, 31) == ControlResult::Changed); // Crosses new target.
    assert(controls.binding(Parameter::Volume).pickedUp);
    assert(near(controls.config().volume, 31.0f / 127));

    controls.beginLearn(Parameter::Attack);
    controls.cancelLearn();
    assert(controls.applyCC(0, 10, 100) == ControlResult::Ignored);
    assert(!controls.binding(Parameter::Attack).assigned);
    controls.beginLearn(Parameter::Sustain);
    assert(controls.applyCC(16, 119, 0) == ControlResult::Ignored);
    assert(controls.applyCC(0, 120, 0) == ControlResult::Ignored);
    assert(controls.applyCC(0, 119, 128) == ControlResult::Ignored);
    assert(controls.learning());
    assert(controls.applyCC(0, 119, 20) == ControlResult::Learned);
    assert(near(controls.config().sustain, 0.7f));
    controls.beginLearn(Parameter::Attack);
    assert(controls.applyCC(0, 119, 50) == ControlResult::Learned);
    assert(!controls.binding(Parameter::Sustain).assigned); // Duplicate moved to new parameter.
    assert(controls.binding(Parameter::Attack).assigned);
    controls.beginLearn(Parameter::Decay);
    assert(controls.applyCC(1, 119, 50) == ControlResult::Learned);
    assert(controls.binding(Parameter::Attack).assigned); // Same CC, another channel is independent.
}

static void testRelative(MidiControlMode mode, uint8_t center, uint8_t plus, uint8_t minus,
                         uint8_t fastPlus, uint8_t fastMinus) {
    SynthControls controls;
    controls.setNormalized(Parameter::Volume, 0.5f); // Setup may already equal the startup default.
    controls.beginLearn(Parameter::Volume, mode);
    assert(controls.applyCC(0, 10, plus) == ControlResult::Learned);
    assert(near(controls.config().volume, 0.5f));
    assert(controls.binding(Parameter::Volume).pickedUp); // Relative knobs need no pickup.
    assert(controls.applyCC(0, 10, 0) == ControlResult::Ignored); // Arturia interleaved neutral.
    assert(controls.applyCC(0, 10, center) == ControlResult::Ignored);
    assert(near(controls.config().volume, 0.5f));
    assert(controls.applyCC(0, 10, plus) == ControlResult::Changed);
    assert(near(controls.config().volume, 0.51f));
    assert(controls.applyCC(0, 10, minus) == ControlResult::Changed);
    assert(near(controls.config().volume, 0.5f));
    assert(controls.applyCC(0, 10, fastPlus) == ControlResult::Changed);
    assert(near(controls.config().volume, 0.53f));
    assert(controls.applyCC(0, 10, fastMinus) == ControlResult::Changed);
    assert(near(controls.config().volume, 0.5f));
}

static void testAbsoluteMappingAndRestore() {
    SynthControls controls;
    SynthConfig settings;
    settings.attackMs = 0;
    settings.decayMs = 0;
    settings.releaseMs = 0;
    settings.volume = 0.5f;
    assert(controls.setConfig(settings));
    for (Parameter parameter : {Parameter::Attack, Parameter::Decay, Parameter::Release}) {
        controls.beginLearn(parameter);
        const auto cc = static_cast<uint8_t>(static_cast<unsigned>(parameter) + 20);
        assert(controls.applyCC(0, cc, 0) == ControlResult::Learned);
        assert(controls.applyCC(0, cc, 0) == ControlResult::Ignored);
        assert(controls.binding(parameter).pickedUp);
        assert(controls.applyCC(0, cc, 127) == ControlResult::Changed);
        assert(near(controls.normalized(parameter), 1));
    }
    controls.beginLearn(Parameter::Waveform);
    assert(controls.applyCC(0, 40, 0) == ControlResult::Learned);
    assert(controls.applyCC(0, 40, 31) == ControlResult::Ignored); // Square bin picks up without jumping.
    assert(controls.binding(Parameter::Waveform).pickedUp);
    assert(controls.applyCC(0, 40, 32) == ControlResult::Changed);
    assert(controls.config().waveform == Waveform::Saw);
    assert(controls.applyCC(0, 40, 64) == ControlResult::Changed);
    assert(controls.config().waveform == Waveform::Triangle);
    assert(controls.applyCC(0, 40, 96) == ControlResult::Changed);
    assert(controls.config().waveform == Waveform::Sine);

    const MidiBinding restored{true, 2, 74, MidiControlMode::Absolute, true};
    assert(controls.setBinding(Parameter::Volume, restored));
    assert(!controls.binding(Parameter::Volume).pickedUp); // Saved pickup state is not reused.
    assert(controls.applyCC(2, 74, 0) == ControlResult::Ignored);
    assert(controls.applyCC(2, 74, 64) == ControlResult::Changed);
    assert(controls.binding(Parameter::Volume).pickedUp);
    assert(controls.setConfig(settings));
    assert(!controls.binding(Parameter::Volume).pickedUp);

    MidiBinding invalid = restored;
    invalid.cc = 120;
    assert(!controls.setBinding(Parameter::Volume, invalid));
    invalid = restored;
    invalid.channel = 16;
    assert(!controls.setBinding(Parameter::Volume, invalid));
    invalid = restored;
    invalid.mode = static_cast<MidiControlMode>(99);
    assert(!controls.setBinding(Parameter::Volume, invalid));
    assert(controls.binding(Parameter::Volume).cc == 74);
    SynthConfig invalidSettings = settings;
    invalidSettings.releaseMs = -1;
    assert(!controls.setConfig(invalidSettings));
    invalidSettings = settings;
    invalidSettings.volume = std::numeric_limits<float>::infinity();
    assert(!controls.setConfig(invalidSettings));
    invalidSettings = settings;
    invalidSettings.waveform = static_cast<Waveform>(99);
    assert(!controls.setConfig(invalidSettings));
    controls.clearBindings();
    for (size_t i = 0; i < PARAMETER_COUNT; ++i) assert(!controls.binding(static_cast<Parameter>(i)).assigned);
}

int main() {
    testEncoderRanges();
    testLearnAndPickup();
    testRelative(MidiControlMode::RelativeOffset, 64, 65, 63, 67, 61);
    testRelative(MidiControlMode::RelativeTwosComplement, 64, 1, 127, 3, 125);
    testRelative(MidiControlMode::RelativeOffset16, 16, 17, 15, 19, 13);
    testRelative(MidiControlMode::RelativeSignMagnitude, 64, 1, 65, 3, 67);
    testAbsoluteMappingAndRestore();
    SynthControls controls;
    controls.cycleLearnMode();
    assert(controls.learnMode() == MidiControlMode::RelativeOffset);
    controls.cycleLearnMode();
    assert(controls.learnMode() == MidiControlMode::RelativeTwosComplement);
    controls.cycleLearnMode();
    assert(controls.learnMode() == MidiControlMode::RelativeOffset16);
    controls.cycleLearnMode();
    assert(controls.learnMode() == MidiControlMode::RelativeSignMagnitude);
    controls.cycleLearnMode();
    assert(controls.learnMode() == MidiControlMode::Absolute);
    char text[64];
    controls.formatValue(Parameter::Attack, text, sizeof(text));
    assert(std::strcmp(text, "5.0 ms") == 0);
    controls.formatBinding(Parameter::Attack, text, sizeof(text));
    assert(std::strcmp(text, "Unassigned") == 0);
    char tiny[1] = {'x'};
    controls.formatValue(Parameter::Waveform, tiny, sizeof(tiny));
    assert(tiny[0] == '\0');
    std::cout << "Controls encoder ranges, MIDI learn, pickup/rearm, relative modes/neutrals, mapping and restoration tests passed.\n";
}
