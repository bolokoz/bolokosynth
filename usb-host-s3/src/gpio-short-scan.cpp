/*
 * Limited S3 adjacent-header screening using inputs and weak pulls only.
 * Run only on the disconnected bare board. Exclude supply/reset, USB, UART,
 * memory, RGB and strapping pins; never actively drive adjacent GPIOs.
 * Pull interactions can flag suspicious pairs but cannot certify continuity,
 * exclude all shorts, or protect a pin already connected to an unsafe voltage.
 * A meter/physical inspection is needed for electrical conclusions.
 */

#include <Arduino.h>

// Bare-board screening only. Tested GPIOs are ALWAYS inputs with weak pulls.
// Native USB, UART, memory, strap, RGB, supply and reset pins are excluded.
// This cannot certify continuity or protect a GPIO already bridged to 5V.
struct HeaderPin { const char *label; int gpio; };
static const HeaderPin LEFT[] = {
    {"3V3", -1}, {"3V3", -1}, {"RST", -1}, {"4", 4}, {"5", 5},
    {"6", 6}, {"7", 7}, {"15", 15}, {"16", 16}, {"17", 17},
    {"18", 18}, {"8", 8}, {"3", 3}, {"46", 46}, {"9", 9},
    {"10", 10}, {"11", 11}, {"12", 12}, {"13", 13}, {"14", 14},
    {"5V", -1}, {"GND", -1}
};
static const HeaderPin RIGHT[] = {
    {"GND", -1}, {"43/TX", 43}, {"44/RX", 44}, {"1", 1}, {"2", 2},
    {"42", 42}, {"41", 41}, {"40", 40}, {"39", 39}, {"38", 38},
    {"37", 37}, {"36", 36}, {"35", 35}, {"0/BOOT", 0}, {"45", 45},
    {"48/RGB", 48}, {"47", 47}, {"21", 21}, {"20/USB+", 20},
    {"19/USB-", 19}, {"GND", -1}, {"GND", -1}
};
static const int SAFE_PINS[] = {
    1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    21, 38, 39, 40, 41, 42, 47
};
constexpr unsigned SAMPLES = 8;

static bool safePin(int pin)
{
    for (int candidate : SAFE_PINS) if (candidate == pin) return true;
    return false;
}

static const char *skipReason(int pin)
{
    if (pin < 0) return "supply/GND/RST: no software continuity test";
    if (pin == 0 || pin == 3 || pin == 45 || pin == 46) return "boot strap";
    if (pin == 19 || pin == 20) return "native USB";
    if (pin >= 35 && pin <= 37) return "octal PSRAM";
    if (pin == 43 || pin == 44) return "UART bridge";
    if (pin == 48) return "RGB LED";
    return "not allowlisted";
}

static void floatTestPins()
{
    for (int pin : SAFE_PINS) pinMode(pin, INPUT);
}

static unsigned sampleHigh(int pin)
{
    unsigned highs = 0;
    delay(20);
    for (unsigned i = 0; i < SAMPLES; ++i) {
        highs += digitalRead(pin) == HIGH;
        delay(2);
    }
    return highs;
}

struct PairRead { unsigned a; unsigned b; };
static PairRead pairState(int a, int b, uint8_t modeA, uint8_t modeB)
{
    pinMode(a, INPUT);
    pinMode(b, INPUT);
    pinMode(a, modeA);
    pinMode(b, modeB);
    delay(20);
    PairRead readings = {0, 0};
    for (unsigned i = 0; i < SAMPLES; ++i) {
        readings.a += digitalRead(a) == HIGH;
        readings.b += digitalRead(b) == HIGH;
        delay(2);
    }
    pinMode(a, INPUT);
    pinMode(b, INPUT);
    return readings;
}

struct Totals { unsigned tested = 0; unsigned skipped = 0; unsigned suspect = 0; };
static void scanRow(const char *name, const HeaderPin *row, size_t size, Totals &totals)
{
    for (size_t i = 0; i + 1 < size; ++i) {
        const HeaderPin &a = row[i];
        const HeaderPin &b = row[i + 1];
        if (!safePin(a.gpio) || !safePin(b.gpio)) {
            ++totals.skipped;
            Serial.printf("PAIR %s positions=%u-%u pins=%s/%s SKIPPED: %s; %s\n",
                          name, unsigned(i + 1), unsigned(i + 2), a.label, b.label,
                          safePin(a.gpio) ? "ordinary GPIO" : skipReason(a.gpio),
                          safePin(b.gpio) ? "ordinary GPIO" : skipReason(b.gpio));
            continue;
        }
        floatTestPins();
        const PairRead uu = pairState(a.gpio, b.gpio, INPUT_PULLUP, INPUT_PULLUP);
        const PairRead dd = pairState(a.gpio, b.gpio, INPUT_PULLDOWN, INPUT_PULLDOWN);
        const PairRead ud = pairState(a.gpio, b.gpio, INPUT_PULLUP, INPUT_PULLDOWN);
        const PairRead du = pairState(a.gpio, b.gpio, INPUT_PULLDOWN, INPUT_PULLUP);
        const bool anomaly = uu.a != SAMPLES || uu.b != SAMPLES ||
                             dd.a != 0 || dd.b != 0 ||
                             ud.a != SAMPLES || ud.b != 0 ||
                             du.a != 0 || du.b != SAMPLES;
        ++totals.tested;
        if (anomaly) ++totals.suspect;
        Serial.printf("PAIR %s positions=%u-%u pins=%s/%s %s high-counts UU=%u,%u DD=%u,%u UD=%u,%u DU=%u,%u /8\n",
                      name, unsigned(i + 1), unsigned(i + 2), a.label, b.label,
                      anomaly ? "SUSPECT" : "NO_ANOMALY",
                      uu.a, uu.b, dd.a, dd.b, ud.a, ud.b, du.a, du.b);
    }
}

static void scan()
{
    Serial.println("GPIO_SCAN_BEGIN");
    Serial.println("Weak-pull input screening, not continuity certification.");
    Serial.println("Layout: components facing you, antenna TOP, USB connectors BOTTOM.");
    Serial.println("All modules, keyboard and external debugger must be disconnected.");
    Serial.println("NO_ANOMALY does not rule out a short; SUSPECT can also mean leakage/loading.");
    Serial.println("5V/3V3/GND/RST and excluded-pin pairs remain UNTESTED.");
    unsigned singleSuspects = 0;
    floatTestPins();
    for (int pin : SAFE_PINS) {
        pinMode(pin, INPUT_PULLUP);
        unsigned up = sampleHigh(pin);
        pinMode(pin, INPUT_PULLDOWN);
        unsigned down = sampleHigh(pin);
        pinMode(pin, INPUT);
        bool anomaly = up != SAMPLES || down != 0;
        if (anomaly) ++singleSuspects;
        Serial.printf("PIN GPIO%d %s pull-up-high=%u/8 pull-down-high=%u/8\n",
                      pin, anomaly ? "SUSPECT_HELD_OR_LOADED" : "NO_ANOMALY", up, down);
    }
    Totals totals;
    scanRow("LEFT", LEFT, sizeof(LEFT) / sizeof(LEFT[0]), totals);
    scanRow("RIGHT", RIGHT, sizeof(RIGHT) / sizeof(RIGHT[0]), totals);
    floatTestPins();
    Serial.printf("SUMMARY pins-screened=%u pin-suspects=%u adjacent-pairs-screened=%u pair-suspects=%u adjacent-pairs-skipped=%u\n",
                  unsigned(sizeof(SAFE_PINS) / sizeof(SAFE_PINS[0])), singleSuspects,
                  totals.tested, totals.suspect, totals.skipped);
    Serial.println("GPIO_SCAN_END");
}

void setup()
{
    Serial.begin(115200);
    Serial.setTxTimeoutMs(20);
    floatTestPins();
    // No GPIO outputs, RGB LED writes, UART initialization or external peripherals.
    // Wait for an explicit serial command so the report is captured by the host.
}

void loop()
{
    if (Serial.available()) {
        const int command = Serial.read();
        if (command == 's' || command == 'S') scan();
    }
    delay(10);
}
