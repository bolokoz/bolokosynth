/*
 * Active v1 firmware for WT9932P4-TINY: USB MIDI, three-note synthesis and TFT.
 * The control/audio loop owns synth state; USB callbacks enqueue MIDI and the
 * UI task exchanges by-value snapshots/actions. SPI never runs in an ISR.
 * FUSB hardware Serial/JTAG coexists with HUSB host; board revision and USB
 * power routing matter. MAX SD uses GPIO17; MAX/TFT initialization cannot
 * prove physical presence because these buses have no acknowledgment.
 * Audio is 48 kHz, 16-bit, mono duplicated into two I2S slots. The MAX is
 * still a single speaker amplifier; polyphony does not require stereo hardware.
 */

#include <Arduino.h>
#include <atomic>
#include <math.h>
#include "EspUsbHost.h"
#include "ESP_I2S.h"
#include "PolySynth.h"
#include "SynthControls.h"
#include "SynthUi.h"
#include "ModuleHealth.h"
#include "ControllerProfiles.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

EspUsbHost usb;
I2SClass i2s;

// WT9932P4-TINY header labels, verified against the manufacturer schematic.
constexpr int I2S_BCLK = 11;
constexpr int I2S_LRC = 12;
constexpr int I2S_DIN = 13;
constexpr int MAX_ENABLE_GPIO = 17; // SD/MODE: HIGH enables left channel; LOW shuts down.
constexpr uint8_t STATUS_LED_GPIO = 51;
constexpr int MAX_PRESENCE_GPIO = 22;
constexpr int TFT_PRESENCE_GPIO = 23;
constexpr uint32_t SAMPLE_RATE = 48000;
constexpr int16_t TEST_AMPLITUDE = DEFAULT_SIGNAL_AMPLITUDE;
// 256 frames at 48 kHz represent about 5.33 ms of audio. Smaller buffers
// improve control latency but increase write overhead; larger ones do the reverse.
constexpr size_t FRAMES_PER_BUFFER = 256;

// Queue only numeric MIDI fields. Library packet/device pointers may expire
// after the callback returns or when a USB device is unplugged.
struct MidiEvent {
    uint8_t codeIndex;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

struct UiSnapshot {
    SynthConfig config;
    SynthUiStatus status;
};

// Ownership boundary: loop() owns synth/controls; uiTask owns screen.
// Queues copy messages between them. Atomics are for flags/counters that
// cross tasks, not a substitute for locking arbitrary synth state.
QueueHandle_t midiQueue = nullptr;
QueueHandle_t uiActionQueue = nullptr;
QueueHandle_t uiSnapshotQueue = nullptr;
std::atomic<bool> uiReady{false};
std::atomic<uint32_t> uiHeartbeatMs{0};
bool uiTaskFailed = false;
bool audioFault = false;
ModuleHealth maxHealth, tftHealth;
std::atomic<bool> brightnessRequested{false};
std::atomic<bool> inversionRequested{false};
std::atomic<bool> colorPatternRequested{false};
std::atomic<uint32_t> droppedUi{0};
SynthControls controls;
ControllerProfile controllerProfile = ControllerProfile::Generic;
ControllerButtons controllerButtons;
bool controllerKnown = false, automaticProfile = true, learnModeFromProfile = false;
uint16_t controllerVid = 0, controllerPid = 0;
SynthUi screen;
Parameter selectedParameter = Parameter::Preset;
std::atomic<uint32_t> droppedMidi{0};
bool audioReady = false;
bool hostReady = false;
bool fault = false;
bool midiSeen = false;
PolySynth synth(SAMPLE_RATE, TEST_AMPLITUDE);
std::atomic<bool> panicRequested{false};
uint32_t lastMidiMs = 0;
uint16_t ccTraceRemaining = 0;
uint32_t noteOnCount = 0;
uint32_t noteOffCount = 0;
uint64_t audioBytes = 0;

// Avoid resending identical RGB values: addressable-LED writes have a cost
// even when nothing visible changes. Pack RGB into one cached comparison.
static void setStatusLed(uint8_t red, uint8_t green, uint8_t blue) {
    static uint32_t previous = UINT32_MAX;
    const uint32_t color = (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
    if (color != previous) {
        rgbLedWrite(STATUS_LED_GPIO, red, green, blue);
        previous = color;
    }
}

// A missing UI heartbeat means the task failed/stalled; it cannot tell us
// whether the panel itself is plugged in. Unsigned subtraction handles wrap.
static bool tftSoftwareError(uint32_t now) {
    return uiTaskFailed || now - uiHeartbeatMs.load() > 3000;
}

// ModuleHealth decides blink-pattern precedence. MIDI blue is momentary;
// USB green indicates enumeration, not proof of a usable MIDI instrument.
static void updateStatusLed(uint32_t now, size_t deviceCount) {
    const auto color = diagnosticColor(now, maxHealth.state(audioFault || !audioReady),
        tftHealth.state(tftSoftwareError(now)), fault || droppedMidi.load() || droppedUi.load(),
        midiSeen && now - lastMidiMs < 150, hostReady && deviceCount > 0);
    setStatusLed(color.red, color.green, color.blue);
}

static void setPresenceMonitoring(bool enabled) {
    // Optional input-only ground-return sensing. Never drive a module signal.
    pinMode(MAX_PRESENCE_GPIO, enabled ? INPUT_PULLUP : INPUT);
    pinMode(TFT_PRESENCE_GPIO, enabled ? INPUT_PULLUP : INPUT);
    maxHealth.enablePresence(enabled);
    tftHealth.enablePresence(enabled);
}


// Last control interaction travels by value with the UI snapshot.
// Only the control task writes this; only the UI task touches the TFT.
static uint32_t feedbackSequence = 0;
static char feedbackName[32]{}, feedbackValue[32]{}, feedbackHint[32]{};
// Keep only the latest interaction. A sequence change refreshes the popup
// timer even if a knob is at its limit and the formatted value stays the same.
static void showFeedback(const char *name, const char *value, const char *hint) {
    snprintf(feedbackName, sizeof(feedbackName), "%s", name);
    snprintf(feedbackValue, sizeof(feedbackValue), "%s", value);
    snprintf(feedbackHint, sizeof(feedbackHint), "%s", hint);
    ++feedbackSequence;
}
static void showParameterFeedback(Parameter parameter, const char *hint) {
    char value[32];
    controls.formatValue(parameter, value, sizeof(value));
    showFeedback(SynthControls::parameterName(parameter), value, hint);
}

// One-slot overwrite queue: the UI needs the newest state, not a backlog of
// obsolete frames. Format/copy strings here so the UI never reads controls
// concurrently or holds pointers into USB/controller-owned storage.
static void publishUi() {
    if (!uiSnapshotQueue) return;
    UiSnapshot snapshot{};
    snapshot.config = controls.config();
    auto &status = snapshot.status;
    status.feedbackSequence = feedbackSequence;
    snprintf(status.feedbackName, sizeof(status.feedbackName), "%s", feedbackName);
    snprintf(status.feedbackValue, sizeof(status.feedbackValue), "%s", feedbackValue);
    snprintf(status.feedbackHint, sizeof(status.feedbackHint), "%s", feedbackHint);
    status.learning = controls.learning();
    snprintf(status.controllerName, sizeof(status.controllerName), "%s", profileName(controllerProfile));
    // Learning temporarily overrides the selected row when showing binding
    // information. Otherwise the user can inspect the current menu target.
    const Parameter target = status.learning ? controls.learningParameter() : selectedParameter;
    status.learnTarget = target;
    const auto &binding = controls.binding(target);
    status.cc = binding.assigned ? binding.cc : -1;
    status.channel = binding.assigned ? binding.channel : -1;
    status.pickupPending = binding.assigned && binding.mode == MidiControlMode::Absolute && !binding.pickedUp;
    controls.formatBinding(target, status.bindingText, sizeof(status.bindingText));
    snprintf(status.learnMode, sizeof(status.learnMode), "%s", SynthControls::modeName(!status.learning && binding.assigned ? binding.mode : controls.learnMode()));
    status.midi = hostReady && usb.deviceCount() > 0;
    status.audio = audioReady && !audioFault;
    status.held = synth.heldCount();
    status.voices = synth.activeVoiceCount();
    xQueueOverwrite(uiSnapshotQueue, &snapshot);
}

// Pin all SPI drawing and local input polling to this task. The audio loop
// remains the only writer of synth state; UI actions request changes by queue.
static void uiTask(void *) {
    uiReady.store(screen.begin());
    // Publish an initial heartbeat before task creation to avoid diagnosing
    // startup as a stall. Queue allocation/task failure remains an explicit fault.
    uiHeartbeatMs.store(millis());
    UiSnapshot snapshot{};
    for (;;) {
        if (inversionRequested.exchange(false)) screen.toggleInversion();
        if (brightnessRequested.exchange(false)) screen.startBrightnessTest();
        if (colorPatternRequested.exchange(false)) screen.startBrightnessTest(true);
        const UiAction action = screen.poll();
        if (action.type != UiActionType::None && xQueueSend(uiActionQueue, &action, 0) != pdTRUE) {
            ++droppedUi;
        }
        // Nonblocking receive retains the last snapshot when no new one arrived.
        // The UI can still expire popups, poll buttons and finish dirty regions.
        xQueueReceive(uiSnapshotQueue, &snapshot, 0);
        screen.render(snapshot.config, snapshot.status);
        uiHeartbeatMs.store(millis());
        // Yield to the scheduler rather than busy-spinning on encoder input.
        // Each render call draws a limited region, not an entire screen.
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// Human-readable parameter/binding dump, useful for distinguishing a wrong
// controller mapping from a valid knob that is still waiting for pickup.
static void printConfig() {
    if (!Serial) return;
    char value[32], binding[48];
    for (size_t index = 0; index < PARAMETER_COUNT; ++index) {
        const auto parameter = static_cast<Parameter>(index);
        controls.formatValue(parameter, value, sizeof(value));
        controls.formatBinding(parameter, binding, sizeof(binding));
        Serial.printf("CONFIG: %s=%s [%s]\n", SynthControls::parameterName(parameter), value, binding);
    }
}

// Learn listens for the next CC. The MiniLab factory profile supplies a mode
// hint; an explicit user mode choice can override that hint afterward.
static void beginLearn(Parameter parameter) {
    selectedParameter = parameter;
    controls.beginLearn(parameter, controls.learnMode());
    learnModeFromProfile = true;
    if (Serial) Serial.printf("LEARN: %s mode=%s; turn a keyboard knob\n",
                              SynthControls::parameterName(parameter), SynthControls::modeName(controls.learnMode()));
}

// Cycling an already assigned knob also updates its existing binding. Merely
// changing the mode for a future Learn session would leave that knob broken.
static void cycleLearnMode(int steps) {
    learnModeFromProfile = false;
    const auto binding = controls.binding(selectedParameter);
    const int mode = static_cast<int>(!controls.learning() && binding.assigned
                                         ? binding.mode : controls.learnMode());
    controls.setLearnMode(static_cast<MidiControlMode>(((mode + steps) % MIDI_MODE_COUNT + MIDI_MODE_COUNT) % MIDI_MODE_COUNT));
    if (!controls.learning() && binding.assigned) {
        controls.setBindingMode(selectedParameter, controls.learnMode());
        if (Serial) Serial.printf("BINDING MODE: %s=%s (existing assignment updated)\n",
                                   SynthControls::parameterName(selectedParameter),
                                   SynthControls::modeName(controls.learnMode()));
    } else if (Serial) Serial.printf("LEARN MODE: %s\n", SynthControls::modeName(controls.learnMode()));
}

// Opt-in observation of existing module inputs; never drive a test pin.
static uint32_t localTraceUntil = 0;
static uint8_t localTraceLevels = 0;
static void traceLocalInputs() {
    if (!localTraceUntil) return;
    if (static_cast<int32_t>(millis() - localTraceUntil) >= 0) {
        localTraceUntil = 0;
        Serial.println("LOCAL TRACE: complete");
        return;
    }
    const uint8_t levels = digitalRead(18) | (digitalRead(19) << 1) |
                           (digitalRead(20) << 2) | (digitalRead(21) << 3);
    if (levels != localTraceLevels) {
        localTraceLevels = levels;
        Serial.printf("LOCAL INPUT: A18=%u B19=%u PUSH20=%u KEY0_21=%u\n",
                      levels & 1, (levels >> 1) & 1, (levels >> 2) & 1, (levels >> 3) & 1);
    }
}

// Drain a bounded number of actions before audio generation. Configure the
// DSP only after a parameter actually changed; selection alone is UI state.
static void processUi() {
    UiAction action;
    for (unsigned count = 0; uiActionQueue && count < 8 &&
         xQueueReceive(uiActionQueue, &action, 0) == pdTRUE; ++count) {
        if (localTraceUntil && Serial)
            Serial.printf("LOCAL ACTION: type=%u target=%s delta=%d\n",
                          static_cast<unsigned>(action.type), SynthControls::parameterName(action.parameter), action.delta);
        switch (action.type) {
            case UiActionType::SelectParameter:
                selectedParameter = action.parameter;
                break;
            case UiActionType::Adjust:
                selectedParameter = action.parameter;
                if (controls.adjust(action.parameter, action.delta)) {
                    synth.configure(controls.config());
                }
                showParameterFeedback(action.parameter, "Encoder");
                break;
            case UiActionType::Learn:
                beginLearn(action.parameter);
                break;
            case UiActionType::CancelLearn:
                controls.cancelLearn();
                if (Serial) Serial.println("LEARN: cancelled");
                break;
            case UiActionType::CycleLearnMode:
                cycleLearnMode(action.delta);
                break;
            case UiActionType::Panic:
                controls.cancelLearn();
                panicRequested.store(true);
                break;
            default: break;
        }
    }
}

// Diagnostics distinguish software readiness from physical verification.
// Pitch/envelope describe one representative voice; POLYPHONY reports the
// full sounding pool, including release tails. HELD can exceed three after stealing.
static void printStatus() {
    if (!Serial) {
        return;
    }
    Serial.printf("STATUS: I2S=%s HUSB=%s devices=%u tone=%.1fHz env=%s level=%.3f held=%u noteOn=%lu noteOff=%lu audioBytes=%llu droppedMidi=%lu fault=%u TFT=%s droppedUi=%lu\n",
                  audioReady ? "ready" : "failed", hostReady ? "ready" : "failed",
                  static_cast<unsigned>(usb.deviceCount()), synth.frequency(),
                  synth.stageName(), synth.envelopeLevel(), synth.heldCount(),
                  static_cast<unsigned long>(noteOnCount),
                  static_cast<unsigned long>(noteOffCount),
                  static_cast<unsigned long long>(audioBytes),
                  static_cast<unsigned long>(droppedMidi.load()), fault,
                  uiReady.load() ? "initialized" : "starting",
                  static_cast<unsigned long>(droppedUi.load()));

    Serial.printf("POLYPHONY: voices=%u/3 held=%u\n", synth.activeVoiceCount(), synth.heldCount());
    Serial.printf("AMP ENABLE: SD/EN=IO%d level=%s (output level, not hardware detection)\n",
                  MAX_ENABLE_GPIO, digitalRead(MAX_ENABLE_GPIO) == HIGH ? "HIGH" : "LOW");
    Serial.printf("CONTROLLER PROFILE: %s\n", profileName(controllerProfile));
    Serial.printf("MODULES: MAX=%s TFT=%s presence=%s (initialization is not hardware detection)\n",
                  ModuleHealth::name(maxHealth.state(audioFault || !audioReady)),
                  ModuleHealth::name(tftHealth.state(tftSoftwareError(millis()))),
                  maxHealth.presenceEnabled() ? "enabled" : "disabled");

    EspUsbHostDeviceInfo devices[2];
    const size_t count = usb.getDevices(devices, 2);
    for (size_t index = 0; index < count; ++index) {
        const auto &device = devices[index];
        // Numeric fields are copied; library-owned strings can expire on unplug.
        Serial.printf("USB DEVICE: address=%u VID=%04X PID=%04X\n",
                      device.address, device.vid, device.pid);
    }
}

// Auto-select defaults once per detected controller identity. Reapplying on
// every loop would erase user Learn assignments and repeatedly rearm pickup.
// Manual K/G selection disables automatic switching for this boot.
static void updateControllerProfile() {
    if (!automaticProfile) return;
    EspUsbHostDeviceInfo devices[4];
    const size_t count = usb.getDevices(devices, 4);
    if (!count) { controllerButtons.reset(); return; }
    // Prefer the recognized synth keyboard over unrelated USB interfaces.
    size_t chosen = 0;
    for (size_t i = 0; i < count; ++i)
        if (profileForUsb(devices[i].vid, devices[i].pid) != ControllerProfile::Generic) chosen = i;
    const auto &device = devices[chosen];
    if (controllerKnown && controllerVid == device.vid && controllerPid == device.pid) return;
    controllerKnown = true; controllerVid = device.vid; controllerPid = device.pid;
    controllerProfile = profileForUsb(controllerVid, controllerPid);
    loadControllerProfile(controls, controllerProfile);
    controllerButtons.reset();
    if (Serial) Serial.printf("CONTROLLER: %s VID=%04X PID=%04X default mappings loaded\n",
                              profileName(controllerProfile), controllerVid, controllerPid);
}

static void processMidi() {
    MidiEvent event;
    const uint32_t startedUs = micros();
    // Bound each batch so a busy pressure stream cannot starve I2S.
    for (size_t processed = 0; midiQueue && processed < 16 && micros() - startedUs < 2000 &&
         xQueueReceive(midiQueue, &event, 0) == pdTRUE; ++processed) {
        midiSeen = true;
        lastMidiMs = millis();
        // USB codeIndex (CIN) and the MIDI status must agree. Validate 7-bit
        // payloads before passing them to note, bend or controller handlers.
        // The status low nibble keeps identical notes on different channels distinct.
        const uint8_t type = event.status & 0xF0;
        if (event.codeIndex == 0x09 && type == 0x90 &&
            event.data1 < 128 && event.data2 > 0 && event.data2 < 128) {
            // PolySynth allocates/retriggers a slot and applies per-note velocity;
            // the shared patch does not restart unrelated voices' envelopes.
            synth.noteOn(event.status & 0x0F, event.data1, event.data2);
            ++noteOnCount;
            if (Serial) {
                Serial.printf("NOTE ON: channel=%u note=%u velocity=%u tone=%.1fHz\n",
                              (event.status & 0x0F) + 1, event.data1, event.data2, synth.frequency());
            }
        } else if (event.data1 < 128 && event.data2 < 128 &&
                   ((event.codeIndex == 0x08 && type == 0x80) ||
                    (event.codeIndex == 0x09 && type == 0x90 && event.data2 == 0))) {
            // Velocity-zero Note On is also Note Off. Release only its matching
            // channel/key; the allocator protects replacement voices from stale offs.
            synth.noteOff(event.status & 0x0F, event.data1);
            ++noteOffCount;
            if (Serial) {
                Serial.printf("NOTE OFF: channel=%u note=%u held=%u envelope=%s\n",
                              (event.status & 0x0F) + 1, event.data1, synth.heldCount(), synth.stageName());
            }
        } else if (event.codeIndex == 0x0E && type == 0xE0 &&
                   event.data1 < 128 && event.data2 < 128) {
            // Pitch bend is 14-bit, LSB first, with center8192. The synth stores
            // it per channel even before a note starts and preserves ADSR timing.
            synth.pitchBend(event.status & 0x0F, event.data1, event.data2);
            static uint32_t lastBendLogMs = 0;
            if (Serial && (millis() - lastBendLogMs >= 100 ||
                           (event.data1 == 0 && event.data2 == 64))) {
                lastBendLogMs = millis();
                Serial.printf("SLIDER PITCH: channel=%u value=%u bend=%+.3f st tone=%.1fHz\n",
                              (event.status & 0x0F) + 1,
                              event.data1 | (unsigned(event.data2) << 7),
                              synth.bendSemitones(event.status & 0x0F), synth.frequency());
            }
        } else if (event.codeIndex == 0x0B && type == 0xB0 &&
                   event.data1 == 121 && event.data2 < 128) {
            // CC121 recenters bend/modulation without resetting the patch or
            // releasing notes; these controller states are channel-specific.
            synth.resetControllers(event.status & 0x0F);
        } else if (event.codeIndex == 0x0B && type == 0xB0 &&
                   event.data1 < 128 && event.data2 < 128 &&
                   (event.data1 == 120 || event.data1 == 123)) {
            // CC120 All Sound Off cuts release tails immediately; CC123 All
            // Notes Off closes gates and lets each ADSR release normally.
            synth.allNotesOff(event.status & 0x0F, event.data1 == 120);
        } else if (event.codeIndex == 0x0B && type == 0xB0 &&
                   event.data1 < 120 && event.data2 < 128) {
            if (ccTraceRemaining && Serial) {
                --ccTraceRemaining;
                Serial.printf("CC RAW: channel=%u controller=%u value=%u\n",
                              (event.status & 0x0F) + 1, event.data1, event.data2);
            }
            // Save Learn state before applyCC: that call may consume the message
            // as an assignment and end the learning session.
            const bool wasLearning = controls.learning();
            if (wasLearning && learnModeFromProfile && (event.status & 0x0F) == 0)
                controls.setLearnMode(profileMode(controllerProfile, event.data1, controls.learnMode()));
            // A learned CC1 binding takes priority over the default vibrato
            // strip. Learning must not also modulate notes as a side effect.
            const bool defaultVibrato = event.data1 == 1 && !controls.learning() &&
                                        !controls.hasBinding(event.status & 0x0F, event.data1);
            const Parameter target = controls.learningParameter();
            const ControlResult result = controls.applyCC(event.status & 0x0F, event.data1, event.data2);
            // Only unassigned clicks invoke profile actions. A learned mapping
            // owns its CC, so a click cannot both edit a parameter and panic/reload.
            if (!wasLearning && !controls.hasBinding(event.status & 0x0F, event.data1)) {
                const auto action = controllerButtons.apply(controllerProfile, event.status & 0x0F, event.data1, event.data2);
                if (action == ControllerAction::ReloadSound) {
                    controls.setConfig(soundPreset(controls.config().preset, controls.config().volume));
                    synth.configure(controls.config());
                    if (Serial) Serial.println("CONTROLLER: current sound reloaded");
                } else if (action == ControllerAction::Panic) {
                    panicRequested.store(true);
                    synth.panic();
                    if (Serial) Serial.println("CONTROLLER: panic/silent");
                }
            }
            // Clear the default modulation when CC1 is reassigned, otherwise
            // the last vibrato depth could remain audible with no way to change it.
            if (event.data1 == 1) synth.modulation(event.status & 0x0F, defaultVibrato ? event.data2 : 0);
            if (result == ControlResult::Changed) {
                synth.configure(controls.config());
            }
            // Show the actual assigned target even when it is on another page,
            // at a limit, or waiting for absolute-controller pickup.
            bool feedbackMapped = false;
            for (unsigned i = 0; i < PARAMETER_COUNT; ++i) {
                const auto parameter = static_cast<Parameter>(i);
                const auto &mapped = controls.binding(parameter);
                if (!mapped.assigned || mapped.channel != (event.status & 0x0F) ||
                    mapped.cc != event.data1) continue;
                char hint[32];
                if (mapped.mode == MidiControlMode::Absolute && !mapped.pickedUp)
                    snprintf(hint, sizeof(hint), "Turn to match");
                else
                    snprintf(hint, sizeof(hint), "Ch%u CC%u", mapped.channel + 1, mapped.cc);
                showParameterFeedback(parameter, hint);
                feedbackMapped = true;
                break;
            }
            if (!feedbackMapped && defaultVibrato) {
                char value[32];
                snprintf(value, sizeof(value), "%.0f%%", event.data2 * 100.0f / 127);
                showFeedback("Vibrato depth", value, "Slider / CC1");
            } else if (!feedbackMapped && event.data1 != 113 && event.data1 != 115) {
                char value[32];
                snprintf(value, sizeof(value), "Ch%u CC%u", (event.status & 0x0F) + 1, event.data1);
                showFeedback("Unassigned", value, "Use MIDI Learn");
            }
            if (Serial && result == ControlResult::Learned) {
                Serial.printf("LEARNED: %s channel=%u CC=%u mode=%s\n",
                              SynthControls::parameterName(target), (event.status & 0x0F) + 1,
                              event.data1, SynthControls::modeName(controls.learnMode()));
            }
            // Sample CC telemetry at most 10Hz; controller traffic must not stall audio.
            static uint32_t lastCcLogMs = 0;
            if (Serial && (result == ControlResult::Learned || millis() - lastCcLogMs >= 100)) {
                lastCcLogMs = millis();
                Serial.printf("CC: channel=%u controller=%u value=%u result=%s\n",
                              (event.status & 0x0F) + 1, event.data1, event.data2,
                              result == ControlResult::Changed ? "changed" :
                              result == ControlResult::Learned ? "learned" : defaultVibrato ? "vibrato" : "waiting/unassigned");
            }
        }
    }
}

void setup() {
    // Begin with SD low so MAX stays muted while peripherals start. HIGH
    // later selects its left I2S slot, which contains the same mono mix as right.
    pinMode(MAX_ENABLE_GPIO, OUTPUT);
    digitalWrite(MAX_ENABLE_GPIO, LOW); // Keep amp disabled until I2S initializes.
    Serial.begin(115200);
    // Unattended USB logging must not stall the audio loop.
    Serial.setTxTimeoutMs(0);
    setStatusLed(32, 0, 32);
    delay(500);

    Serial.println("\nBolokoSynth P4: TFT + encoder + USB MIDI synth");
    Serial.printf("I2S: BCLK=IO%d LRC=IO%d DIN=IO%d, %luHz stereo 16-bit\n",
                  I2S_BCLK, I2S_LRC, I2S_DIN, static_cast<unsigned long>(SAMPLE_RATE));
    Serial.printf("RGB: GPIO%u; FUSB=laptop, HUSB=powered MIDI keyboard\n", STATUS_LED_GPIO);

    // Bounded storage absorbs brief USB bursts without allocating per event.
    // Failure/overflow is surfaced; dropping a Note Off silently risks stuck notes.
    midiQueue = xQueueCreate(64, sizeof(MidiEvent));
    if (!midiQueue) {
        fault = true;
        Serial.println("ERROR: MIDI event queue allocation failed");
    }

    // Initialize DSP defaults before enabling the amplifier. Stereo I2S framing
    // is required even though synthesis and the physical MAX output are mono.
    synth.configure(controls.config());
    i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
    audioReady = i2s.begin(I2S_MODE_STD, SAMPLE_RATE,
                           I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    // Latch startup failure but continue bringing up USB/UI diagnostics;
    // the amplifier stays disabled because no valid I2S stream exists.
    if (!audioReady) {
        audioFault = true;
        fault = true;
        Serial.printf("ERROR: I2S initialization failed: %d\n", i2s.lastError());
    } else {
        digitalWrite(MAX_ENABLE_GPIO, HIGH);
        Serial.println("I2S ready: silent until MIDI Note On; ADSR 5ms/50ms/70%/200ms");
    }

    // Register callbacks before starting the host. Callbacks must not draw,
    // generate audio, or mutate the voice pool; loop() consumes copied events.
    usb.onMidiMessage([](const EspUsbHostMidiMessage &message) {
        // Ignore empty/reserved USB MIDI packets observed in the earlier capture.
        if (!midiQueue || panicRequested.load() || message.codeIndex < 2 || message.status < 0x80) {
            return;
        }
        // Copy values only: the library's raw packet buffer is transient.
        const MidiEvent event{message.codeIndex, message.status, message.data1, message.data2};
        // Never wait inside the USB callback. On overflow request Panic rather
        // than let an incomplete note sequence leave a voice permanently held.
        if (xQueueSend(midiQueue, &event, 0) != pdTRUE) {
            ++droppedMidi;
            panicRequested.store(true);
        }
    });

    // A detach can remove the device before its Note Off arrives. Request a
    // loop-owned reset and discard pending events from the disconnected device.
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) {
        panicRequested.store(true);
    });

    // P4 HUSB is the high-speed host; FUSB remains available for flashing/logs.
    // Starting the host does not provide VBUS power: the injector supplies that.
    EspUsbHostConfig config;
    config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    hostReady = usb.begin(config);
    if (!hostReady) {
        fault = true;
        Serial.printf("ERROR: HUSB host start failed: %s\n", usb.lastErrorName());
    } else {
        Serial.println("HUSB host ready: Note On starts sound; Note Off releases it");
    }
    uiHeartbeatMs.store(millis());
    uiActionQueue = xQueueCreate(16, sizeof(UiAction));
    uiSnapshotQueue = xQueueCreate(1, sizeof(UiSnapshot));
    if (!uiActionQueue || !uiSnapshotQueue ||
        xTaskCreatePinnedToCore(uiTask, "synth-ui", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        fault = true;
        uiTaskFailed = true;
        Serial.println("ERROR: UI queue/task allocation failed");
    }
    publishUi();
    Serial.println("TFT: ST7789 320x240 landscape; SCK9 MOSI10 CS14 DC15 RES16");
    Serial.println("Slider: pitch bend automatic +/-2 semitones; CC1=vibrato depth 5Hz/50 cents; other CC sliders use ABS Learn");
    Serial.println("Encoder: A18 B19 PUSH20 BACK21; BLK open; VCC=3.3V");
    Serial.println("LED: 2 amber=MAX, 3 purple=TFT; dim=unverified, bright=fault/missing presence wire");
    Serial.println("LED: green=USB device, blue=MIDI, rapid red=other firmware error");
    Serial.println("Modules: A=confirm heard audio, F=confirm working TFT, 1/2=report MAX/TFT problem, !=clear confirmations, H/h=presence sensing on/off");
    Serial.println("Console: [/]=previous/next sound, s=status, c=config, j=learn volume slider, q=CC trace, K=MiniLab defaults, G=generic, b=white screen test, i=inversion, t=color pattern, p=panic, a/d/u/r/w/v=target, l=learn, m=mode, x=cancel, +/-=adjust");
    printStatus();
}

void loop() {
    static uint32_t lastLedMs = 0;
    static uint32_t lastStatusMs = 0;
    static size_t previousDeviceCount = SIZE_MAX;

    // Handle callback-requested Panic before consuming more events. Reset the
    // queue as well as voices so pre-detach/overflow notes cannot restart sound.
    if (panicRequested.load()) {
        if (midiQueue) xQueueReset(midiQueue);
        synth.panic();
        panicRequested.store(false);
        if (Serial) Serial.println("PANIC: cleared notes and pending MIDI after detach/overflow");
    }
    // Order matters: establish controller defaults, apply local edits, then
    // decode queued MIDI. All three stages run under the audio loop's ownership.
    updateControllerProfile();
    traceLocalInputs();
    processUi();
    processMidi();

    // One console byte per iteration bounds diagnostic work. Display commands
    // set atomic requests for uiTask; they never call SPI from this loop.
    if (Serial.available()) {
        const int command = Serial.read();
        if (command == 's' || command == 'S') {
            printStatus();
        // A/F records human confirmation, not automatic electrical detection.
        // Optional presence wires and software faults can reject confirmation.
        } else if (command == 'A' || command == 'F') {
            const bool isMax = command == 'A';
            const bool accepted = isMax ? maxHealth.confirm(audioFault || !audioReady)
                                       : tftHealth.confirm(tftSoftwareError(millis()));
            Serial.printf("MODULE: %s user confirmation %s\n", isMax ? "MAX" : "TFT",
                          accepted ? "recorded until restart/unplug" : "rejected: software fault or missing/unstable presence wire");
        } else if (command == '1' || command == '2') {
            (command == '1' ? maxHealth : tftHealth).reportProblem();
            Serial.println("MODULE: user-reported problem recorded");
        } else if (command == '!') {
            maxHealth.resetVerification();
            tftHealth.resetVerification();
            Serial.println("MODULE: confirmations/reported problems cleared");
        } else if (command == 'H' || command == 'h') {
            setPresenceMonitoring(command == 'H');
            Serial.println(command == 'H' ? "PRESENCE: enabled; IO22=MAX ground return, IO23=TFT ground return"
                                          : "PRESENCE: disabled; hardware unverified");
        } else if (command == 'p' || command == 'P') {
            panicRequested.store(true);
            if (midiQueue) xQueueReset(midiQueue);
            synth.panic();
            panicRequested.store(false);
            controls.cancelLearn();
            Serial.println("PANIC: silent");
        } else if (command == 't') {
            colorPatternRequested.store(true);
            Serial.println("DISPLAY: 30-second labeled color/contrast test");
        } else if (command == 'i') {
            inversionRequested.store(true);
            Serial.println("DISPLAY: toggle LCD inversion");
        } else if (command == 'b') {
            brightnessRequested.store(true);
            Serial.println("DISPLAY: 30-second steady white brightness test");
        } else if (command == '[' || command == ']') {
            if (controls.adjust(Parameter::Preset, command == ']' ? 1 : -1)) {
                synth.configure(controls.config());
                Serial.printf("SOUND: %s\n", soundName(controls.config().preset));
            }
        } else if (command == 'c') {
            printConfig();
        // Lock this boot to explicit profile choice; automatic enumeration
        // must not overwrite the user's selected mappings on the next loop.
        } else if (command == 'K' || command == 'G') {
            automaticProfile = false;
            controllerProfile = command == 'K' ? ControllerProfile::MiniLabMkII : ControllerProfile::Generic;
            loadControllerProfile(controls, controllerProfile);
            controllerButtons.reset();
            Serial.printf("CONTROLLER: %s selected manually\n", profileName(controllerProfile));
        // Debug traces are opt-in and expire/stop after a bounded count.
        // Verbose continuous serial output would otherwise compete with audio.
        } else if (command == 'e') {
            localTraceUntil = millis() + 60000;
            localTraceLevels = 0xFF;
            Serial.println("LOCAL TRACE: 60 seconds; A18/B19/PUSH20/KEY0_21 and decoded actions");
        } else if (command == 'q') {
            ccTraceRemaining = 96;
            Serial.println("CC TRACE: next 96 CC messages, before rate-limited telemetry");
        } else if (command == 'j') {
            selectedParameter = Parameter::Volume;
            controls.beginSliderLearn();
            learnModeFromProfile = false;
            Serial.println("SLIDER LEARN: Volume ABS; move the CC slider, then cross the current volume for pickup");
        } else if (command == 'l') {
            beginLearn(selectedParameter);
        } else if (command == 'x') {
            controls.cancelLearn();
        } else if (command == 'm') {
            cycleLearnMode(1);
        } else if (command == '+' || command == '-') {
            if (controls.adjust(selectedParameter, command == '+' ? 1 : -1)) {
                synth.configure(controls.config());
            }
        } else {
            const char targets[] = "adurwv";
            for (size_t index = 0; index < sizeof(targets) - 1; ++index) {
                if (command == targets[index]) selectedParameter = static_cast<Parameter>(index);
            }
        }
    }

    const uint32_t now = millis();
    const size_t deviceCount = usb.deviceCount();
    static uint32_t lastUiMs = 0;
    // State snapshots are limited to10Hz; input polling/popup expiry happen
    // independently on the UI task. Coalesce fast CC streams into latest values.
    if (now - lastUiMs >= 100) {
        lastUiMs = now;
        publishUi();
    }
    // LED/presence sampling runs at50Hz; debounce/pattern logic lives in
    // ModuleHealth. Disabled sensing leaves unused presence pins as inputs.
    if (now - lastLedMs >= 20) {
        lastLedMs = now;
        if (maxHealth.presenceEnabled()) maxHealth.sample(now, digitalRead(MAX_PRESENCE_GPIO) == HIGH);
        if (tftHealth.presenceEnabled()) tftHealth.sample(now, digitalRead(TFT_PRESENCE_GPIO) == HIGH);
        updateStatusLed(now, deviceCount);
    }
    if (deviceCount != previousDeviceCount || now - lastStatusMs >= 2000) {
        previousDeviceCount = deviceCount;
        lastStatusMs = now;
        printStatus();
    }

    // Keep MIDI/UI/diagnostics alive after an I2S startup failure, but never
    // write to an uninitialized audio peripheral. Yield briefly on this path.
    if (!audioReady) {
        delay(10);
        return;
    }

    // Advance each voice's ADSR once per frame, then duplicate the mono mix.
    // Release tails keep sounding; only idle/fully released envelopes are zero.
    // Keep I2S running even during silence so the next note needs no restart.
    int16_t frames[FRAMES_PER_BUFFER * 2];
    for (size_t index = 0; index < FRAMES_PER_BUFFER; ++index) {
        const int16_t sample = synth.nextSample();
        frames[index * 2] = sample;
        frames[index * 2 + 1] = sample;
    }
    // Write an interleaved L/R buffer to I2S DMA. Hardware consumption paces
    // this loop; an extra delay here would create gaps in the audio stream.
    const size_t written = i2s.write(reinterpret_cast<const uint8_t *>(frames), sizeof(frames));
    audioBytes += written;
    // Count bytes actually accepted. A short write latches an audio fault;
    // this checks transport progress, not speaker connection or audible quality.
    if (written != sizeof(frames)) {
        audioFault = true;
        fault = true;
        if (Serial) {
            Serial.printf("ERROR: I2S short write %u/%u error=%d\n",
                          static_cast<unsigned>(written), static_cast<unsigned>(sizeof(frames)),
                          i2s.lastError());
        }
    }
}
