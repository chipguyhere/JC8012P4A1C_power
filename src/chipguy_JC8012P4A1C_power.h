/*
 * Battery and power helpers for the Guition JC8012P4A1C — the 10.1" ESP32-P4
 * panel.
 *
 * What the hardware gives software — and it is not much:
 *
 *   GPIO52  BAT sense  the cell's voltage through a 68k/100k divider
 *                      (V_BAT x 100/168), an ADC-capable pin
 *
 * Everything else about power on this board is fixed in hardware and out of
 * the ESP32-P4's reach:
 *
 *   - The whole board runs from an IP5306 "power bank" chip: charger, battery
 *     boost to 5 V, and on/off logic in one.  It is the plain IP5306, not the
 *     I2C variant — no bus, no status line to a GPIO, its four LED pins
 *     unconnected.  So there is no "USB present", no "charging", no "charge
 *     complete" to read.
 *   - Its 5 V output (VOUT-BAT) feeds EVERYTHING — the 3.3 V buck, the
 *     backlight boost, the speaker amplifier — even when USB is plugged in
 *     (USB goes into the IP5306 and out again).
 *   - The IP5306's datasheet describes a light-load auto-off (under ~45-50 mA
 *     for ~32 s), but on this board it did not trigger even at ~8 mA of load
 *     (measured).  So there is NO software power-off, and timer wake works on
 *     battery as on USB.
 *   - Sleep currents from the cell (measured): deep sleep ~40 mA, light sleep
 *     with the ESP32-C6 and touch held in reset ~10 mA.  Deep sleep loses
 *     because this board's P4 (rev v1.3) can't hold GPIO54 low through deep
 *     sleep, so the C6 restarts.  See the SleepCurrentProbe example.
 *   - The IP5306's KEY button (SW1) goes only to the IP5306: short press = on,
 *     two presses within ~1 s = off (on battery; with USB connected a double
 *     press does NOT cut power — observed).  No GPIO sees it.
 *   - Deep-sleep wake is limited to the ESP32-P4's LP GPIOs (0..15).  On this
 *     board the only free ones are GPIO2..5 on the FPC3 header.  The BOOT
 *     button (GPIO35) and the touch INT (GPIO21) are NOT wake-capable.
 *
 * So this library reads the battery and offers deep sleep with timer and
 * header-pin wake.  The board's only real "off" is the power button.  The rest of what you need to know is in the README's
 * "Power characteristics at a glance".
 *
 * Copyright (c) 2026 chipguyhere
 * MIT License
 */

#pragma once

#include <Arduino.h>

// After Arduino.h: CONFIG_IDF_TARGET_* comes from sdkconfig.h.
#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "chipguy_JC8012P4A1C_power: this board is an ESP32-P4. Select ESP32P4 Dev Module."
#endif

class chipguy_JC8012P4A1C_power {
public:
    static constexpr int BATTERY_ADC_PIN = 52;   // V_BAT x 100/168

    // Pins that can wake the chip from deep sleep and are free on this board
    // (LP GPIOs brought out on FPC3).  Pass BIT64(n) of these to deepSleep().
    static constexpr int WAKE_CAPABLE_HEADER_PINS[4] = { 2, 3, 4, 5 };

    // Configure the ADC input.  Idempotent; every other method calls it, so
    // calling it yourself is optional.
    void begin();

    // ── Battery ─────────────────────────────────────────────────────────────
    // Battery voltage in volts (e.g. 3.82f): the average of several calibrated
    // ADC readings, times the divider ratio.  Good to roughly +/-50 mV — a
    // battery icon or a low-battery cutoff, not a fuel gauge.  With USB
    // plugged in this reads the CHARGER's voltage (up to ~4.2 V), not the
    // cell's state of charge; with no battery fitted it reads whatever the
    // IP5306 leaves on the open terminal.  See the README.
    float getBatteryVoltage();

    // A rough state-of-charge estimate, 0..100, from the voltage above via a
    // typical Li-ion open-circuit curve.  Only meaningful when running on
    // battery with a steady load; on USB the charger's voltage makes it read
    // high, and a lit backlight makes it read low.
    int getBatteryPercent();

    // ── Sleep ───────────────────────────────────────────────────────────────
    // True if this boot is a wake from deepSleep() rather than a power-on or
    // reset.  If byTimer is given, it is set when the wake was the timer
    // rather than a header pin.
    bool wasAwakenedFromDeepSleep(bool *byTimer = nullptr);

    // Deep sleep until `wakeAfterSeconds` pass (0 = no timer) or one of the
    // pins in `wakePinMask` is pulled LOW (bit n = GPIOn; only GPIO2..5 are
    // both free and wake-capable here — give them a pull-up to 3.3 V).  Never
    // returns: the chip reboots on wake and setup() runs again.
    //
    // The backlight goes dark on its own (its enable has a pull-down), but the
    // panel, the touch controller and the codec stay powered — there is no
    // switch on their rails.
    //
    // ON BATTERY, READ THIS: deep sleep costs ~40 mA from the cell here
    // (measured) because the ESP32-C6 restarts once the P4 lets go of GPIO54.
    // Light sleep with the C6 held in reset measures ~10 mA — see the
    // SleepCurrentProbe example; a library helper for it is a TODO.
    void deepSleep(uint32_t wakeAfterSeconds = 0, uint64_t wakePinMask = 0);

    // "Turn off" as far as software can: deep sleep with no wake source.  The
    // power stays on (see above), so the board sleeps until RESET, a power
    // cycle, or — on battery — a double press of the IP5306's power button,
    // which is the real "off".  A sketch should tell the user to press it.
    void shutdown();

private:
    bool _begun = false;
};

inline chipguy_JC8012P4A1C_power power;
