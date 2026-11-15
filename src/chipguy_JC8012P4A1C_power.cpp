/*
 * Battery and power helpers for the Guition JC8012P4A1C.
 * See chipguy_JC8012P4A1C_power.h for what the hardware does and doesn't allow.
 *
 * Copyright (c) 2026 chipguyhere
 * MIT License
 */

#include "chipguy_JC8012P4A1C_power.h"
#include <esp_sleep.h>
#include "soc/soc_caps.h"

// The divider, from the schematic (1_PWR): R2 68k from BAT+ to the sense node,
// R6 100k from the node to ground, R4 0R from the node to GPIO52.
static constexpr float BATTERY_DIVIDER = 168.0f / 100.0f;   // (68k + 100k) / 100k

static constexpr int ADC_SAMPLES = 8;

void chipguy_JC8012P4A1C_power::begin() {
    if (_begun) return;
    _begun = true;

    // A full cell puts 4.2 x 100/168 = 2.5 V on the pin, inside the range of
    // the widest attenuation.
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
}

static int _avgMilliVolts(int pin) {
    // analogReadMilliVolts applies the chip's eFuse ADC calibration, which is
    // what makes the reading worth quoting in volts at all.
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) sum += analogReadMilliVolts(pin);
    return (int)(sum / ADC_SAMPLES);
}

float chipguy_JC8012P4A1C_power::getBatteryVoltage() {
    begin();
    return _avgMilliVolts(BATTERY_ADC_PIN) * BATTERY_DIVIDER / 1000.0f;
}

int chipguy_JC8012P4A1C_power::getBatteryPercent() {
    // A generic single-cell Li-ion open-circuit voltage curve.  Cells differ,
    // load sags the reading, and on USB the charger sets the voltage — treat
    // this as a battery icon, nothing more.
    static const struct { float v; int pct; } curve[] = {
        { 4.20f, 100 }, { 4.10f, 90 }, { 4.00f, 80 }, { 3.92f, 70 }, { 3.85f, 60 },
        { 3.79f,  50 }, { 3.75f, 40 }, { 3.72f, 30 }, { 3.68f, 20 }, { 3.60f, 10 },
        { 3.40f,   0 },
    };
    float v = getBatteryVoltage();
    if (v >= curve[0].v) return 100;
    const int n = sizeof(curve) / sizeof(curve[0]);
    if (v <= curve[n - 1].v) return 0;
    for (int i = 0; i < n - 1; i++) {
        if (v <= curve[i].v && v > curve[i + 1].v) {
            float span = curve[i].v - curve[i + 1].v;
            float frac = (v - curve[i + 1].v) / span;
            return curve[i + 1].pct + (int)(frac * (curve[i].pct - curve[i + 1].pct) + 0.5f);
        }
    }
    return 0;
}

bool chipguy_JC8012P4A1C_power::wasAwakenedFromDeepSleep(bool *byTimer) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (byTimer) *byTimer = (cause == ESP_SLEEP_WAKEUP_TIMER);
    return cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER;
}

void chipguy_JC8012P4A1C_power::deepSleep(uint32_t wakeAfterSeconds, uint64_t wakePinMask) {
    begin();

    // Nothing to hold: the backlight enable (GPIO23) and the speaker-amp
    // enable (GPIO20) both have 10k pull-downs, so they go off by themselves
    // when their pads let go in deep sleep.

    if (wakePinMask) {
#if SOC_PM_SUPPORT_EXT1_WAKEUP
        // Only LP GPIOs (0..15) can do this; the caller is expected to use the
        // free ones on FPC3 (2..5), pulled up externally, switched to ground.
        esp_sleep_enable_ext1_wakeup(wakePinMask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    }
    if (wakeAfterSeconds) {
        esp_sleep_enable_timer_wakeup((uint64_t)wakeAfterSeconds * 1000000ULL);
    }

    Serial0.flush();
    esp_deep_sleep_start();
}

void chipguy_JC8012P4A1C_power::shutdown() {
    deepSleep(0, 0);
}
