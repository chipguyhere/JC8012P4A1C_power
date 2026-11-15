/*******************************************************************************
 * JC8012P4A1C BatteryMonitor
 *
 * Prints the battery voltage and a rough percentage once a second on the USB
 * serial port.  Type 's' + Enter in the Serial Monitor to deep-sleep for 20
 * seconds.
 *
 * What to expect (and why — see the library README):
 *   - On USB the "battery" voltage is the charger's, so it climbs toward
 *     4.2 V and the percentage reads high whatever the cell's real state.
 *   - Unplug USB and the reading settles to the cell's own voltage within a
 *     few seconds; that is when the percentage means something.
 *   - With no battery fitted the number means nothing at all.
 *   - The 20 s sleep wakes on its timer, on USB and on battery alike: the
 *     board never draws little enough for the IP5306 to switch itself off.
 *     The flip side is that sleep on battery is expensive — see the README.
 *
 * Arduino IDE Board Settings:
 *   - Board: ESP32P4 Dev Module, PSRAM: Enabled, Flash Size: 16MB,
 *     Partition Scheme: Custom, USB CDC On Boot: Enabled
 ******************************************************************************/

#include <chipguy_JC8012P4A1C_power.h>

void setup() {
    Serial0.begin(115200);
    delay(1500);            // give the USB serial port a moment
    power.begin();

    bool byTimer;
    if (power.wasAwakenedFromDeepSleep(&byTimer)) {
        Serial0.println(byTimer ? "Woke from deep sleep: timer" : "Woke from deep sleep: header pin");
    } else {
        Serial0.println("Cold boot / reset");
    }
    Serial0.println("Send 's' to deep-sleep for 20 s.");
}

void loop() {
    static uint32_t lastPrint = 0;

    if (millis() - lastPrint >= 1000) {
        lastPrint = millis();
        Serial0.printf("battery %.2f V (~%d%%)\n",
                       power.getBatteryVoltage(), power.getBatteryPercent());
    }

    if (Serial0.available() && Serial0.read() == 's') {
        Serial0.println("Deep sleeping for 20 s.");
        power.deepSleep(20);    // never returns
    }
    delay(20);
}
