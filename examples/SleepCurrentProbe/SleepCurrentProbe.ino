/*******************************************************************************
 * JC8012P4A1C SleepCurrentProbe — a bench sketch for a power profiler.
 *
 * Walks the board through a series of quiescing steps, each held long enough
 * to read as a plateau on a current profiler at the battery connector, to find
 * (a) which consumer keeps the board above the IP5306's light-load cutoff and
 * (b) how low it can go.  See README-engineering.md, "Getting under the
 * light-load line".
 *
 * HOW TO RUN: flash it, unplug USB, press RESET (the board then runs from the
 * battery).  The RGB LED marks each stage with a short blink, then stays dark
 * while you measure:
 *
 *   stage 1  (1 green)  awake, nothing touched — the C6 has been running its
 *                       Wi-Fi firmware since power-on; panel never initialised
 *   stage 2  (2 green)  + C6 held in reset          (GPIO54 low)
 *   stage 3  (3 green)  + touch held in reset       (GPIO22 low)
 *   stage 4  (4 green)  + panel held in reset       (GPIO27 low)
 *   stage 5  (5 green)  panel reset released instead (GPIO27 high)
 *   stage 6  (1 blue)   DEEP sleep 20 s with pad holds on 54/22/27.  On P4
 *                       silicon before rev 3.0 the holds are expected NOT to
 *                       work (ESP-IDF note DIG-399): if the C6 wakes, you'll
 *                       see the current jump back up ~10 ms into the sleep.
 *   stage 7  (1 red,    LIGHT sleep, C6/touch/panel pins still driven (light
 *             every     sleep keeps pad state on any revision), waking every
 *             60 s)     60 s only to blink.  This is the floor.  If it is
 *                       under the IP5306's threshold, the IP5306 cuts the power
 *                       ~32 s in: the current drops to ~0 and the red blinks
 *                       stop.  Press the power-bank button to bring it back.
 *
 * Each stage's timing is also printed on Serial0 when USB is connected.
 *
 * Stays away from the display library on purpose: the panel is left in its
 * power-on state, so stages 4/5 compare "in reset" vs "released, never
 * initialised" (the JD9365 comes out of reset in Sleep In).
 *
 * Arduino IDE Board Settings: ESP32P4 Dev Module, PSRAM Enabled, Flash 16MB,
 * Partition Scheme Custom, USB CDC On Boot Enabled.
 ******************************************************************************/

#include <JC8012P4A1C_Board.h>      // pin constants only; the bus is never started
#include <esp_sleep.h>
#include <esp_chip_info.h>
#include <driver/gpio.h>

static constexpr uint32_t STAGE_MS = 15000;

static constexpr gpio_num_t C6_EN    = (gpio_num_t)JC8012P4A1C_Board::C6_RESET_PIN;   // 54
static constexpr gpio_num_t TP_RST   = (gpio_num_t)JC8012P4A1C_Board::TOUCH_RST_PIN;  // 22
static constexpr gpio_num_t LCD_RST  = (gpio_num_t)JC8012P4A1C_Board::LCD_RST_PIN;    // 27
static constexpr int        LED      = JC8012P4A1C_Board::RGB_LED_PIN;                // 26

// Survives deep sleep: which part of the sequence we're in.
RTC_DATA_ATTR static uint8_t s_phase = 0;   // 0 = awake stages, 1 = after deep sleep

static void blink(int n, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < n; i++) {
        rgbLedWrite(LED, r, g, b); delay(150);
        rgbLedWrite(LED, 0, 0, 0); delay(250);
    }
}

static void stage(int n, const char *what) {
    Serial0.printf("[%7lu ms] stage %d: %s\n", (unsigned long)millis(), n, what);
    Serial0.flush();
    blink(n, 0, 40, 0);
    delay(STAGE_MS);
}

static void drive(gpio_num_t pin, int level) {
    gpio_hold_dis(pin);                // in case a previous sleep left it held
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
}

// Light sleep keeps each pad's active configuration only if the pad's
// separate "sleep" configuration isn't selected — make sure it isn't.
static void keepDuringLightSleep(gpio_num_t pin) {
    gpio_sleep_sel_dis(pin);
}

static void lightSleepForever() {
    drive(C6_EN, LOW);
    drive(TP_RST, LOW);
    drive(LCD_RST, HIGH);
    keepDuringLightSleep(C6_EN);
    keepDuringLightSleep(TP_RST);
    keepDuringLightSleep(LCD_RST);
    rgbLedWrite(LED, 0, 0, 0);

    for (uint32_t n = 1;; n++) {
        Serial0.printf("[%7lu ms] stage 7: light sleep #%lu (60 s)\n",
                       (unsigned long)millis(), (unsigned long)n);
        Serial0.flush();
        esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
        esp_light_sleep_start();
        blink(1, 40, 0, 0);
    }
}

void setup() {
    Serial0.begin(115200);
    delay(300);

    if (s_phase == 1 && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        // Back from stage 6's deep sleep.
        s_phase = 0;
        Serial0.println("woke from stage-6 deep sleep; entering stage 7");
        lightSleepForever();
    }
    s_phase = 0;

    esp_chip_info_t info;
    esp_chip_info(&info);
    Serial0.printf("\nSleepCurrentProbe — ESP32-P4 revision v%d.%d\n",
                   info.revision / 100, info.revision % 100);
    Serial0.println("Unplug USB and press RESET to measure on battery.");

    rgbLedWrite(LED, 0, 0, 0);

    stage(1, "awake, nothing touched (C6 running since power-on)");

    drive(C6_EN, LOW);
    stage(2, "+ C6 held in reset (GPIO54 low)");

    drive(TP_RST, LOW);
    stage(3, "+ touch held in reset (GPIO22 low)");

    drive(LCD_RST, LOW);
    stage(4, "+ panel held in reset (GPIO27 low)");

    drive(LCD_RST, HIGH);
    stage(5, "panel reset released (GPIO27 high; JD9365 comes up in Sleep In)");

    // Stage 6: deep sleep with pad holds.  On rev >= 3.0, gpio_hold_en()
    // alone holds a pad through deep sleep; before that it is not expected to.
    Serial0.printf("[%7lu ms] stage 6: deep sleep 20 s with holds on 54/22/27\n",
                   (unsigned long)millis());
    Serial0.flush();
    blink(1, 0, 0, 40);
    gpio_hold_en(C6_EN);
    gpio_hold_en(TP_RST);
    gpio_hold_en(LCD_RST);
    s_phase = 1;
    esp_sleep_enable_timer_wakeup(20ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {}
