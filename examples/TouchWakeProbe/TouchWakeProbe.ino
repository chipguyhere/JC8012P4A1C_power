/*******************************************************************************
 * JC8012P4A1C TouchWakeProbe — a bench sketch for a power profiler.
 *
 * Measures the "light sleep, wake on touch" state with the display in real
 * use, and shows that it works.  Companion to SleepCurrentProbe.
 *
 * HOW TO RUN: flash it, unplug USB, press RESET.  The ESP32-C6 is held in
 * reset from the start (no Wi-Fi in this sketch).  The screen shows a solid
 * colour; the RGB LED marks each stage with a short blink:
 *
 *   stage 1  (1 green)  display on, backlight 100 %
 *   stage 2  (2 green)  backlight off, panel still running
 *   stage 3  (3 green)  panel asleep (display.sleep()), CPU awake
 *   then     (1 red)    LIGHT SLEEP until the screen is touched or BOOT is
 *                       pressed.  Touch the screen: it wakes (1 blue blink),
 *                       shows the next colour for 10 s — touches are logged —
 *                       and goes back to sleep.  Repeat as often as you like.
 *
 * WAKE_MODE (below): 0 = touch stays armed and its interrupt wakes the chip
 * (~45 mA asleep); 1 (default) = swipe-to-wake: touch is held in reset while
 * asleep and checked once a second.  A finger that is ALREADY resting on the
 * screen when touch restarts is absorbed into its startup calibration and never
 * reported (measured), so the wake gesture is a finger MOVING across the screen
 * for about a second, not a still press.
 *
 * Wake pin polarity (mode 0): the sketch samples the touch INT line (GPIO21) while
 * nobody is touching and wakes on the opposite level, so it doesn't need to
 * know the GSL3680's polarity in advance; the idle level is logged.
 *
 * DIAG_STAGES (below, 0 by default; set 1): instead of the touch-wake loop, after stage 3
 * switch off one more display-related thing per stage (cumulative) to find
 * what keeps ~35 mA flowing after display.sleep():
 *
 *   stage 4  (4 green)  + MIPI D-PHY shut down
 *   stage 5  (5 green)  + P4 LDO channel 3 (the D-PHY's 2.5 V) off
 *   stage 6  (6 green)  + panel held in hardware reset (GPIO27 low)
 *   stage 7  (7 green)  + touch held in reset (GPIO22 low)
 *   then     (1 red)    light sleep; BOOT wakes it and it restarts.
 *
 * Serial0 logs everything when USB is connected.
 *
 * Arduino IDE Board Settings: ESP32P4 Dev Module, PSRAM Enabled, Flash 16MB,
 * Partition Scheme Custom, USB CDC On Boot Enabled.
 ******************************************************************************/

#include <JC8012P4A1C_Board.h>
#include <chipguy_JC8012P4A1C_display.h>
#include <esp_sleep.h>
#include <esp_cache.h>
#include <driver/gpio.h>
#include "hal/mipi_dsi_phy_ll.h"
#include "hal/ldo_ll.h"
#include "soc/mipi_dsi_host_struct.h"

// 0 = touch stays armed through light sleep and its interrupt wakes the chip
//     (simple, ~45 mA asleep — the touch controller scans the whole time)
// 1 = swipe-to-wake: touch is held in reset while asleep; every CHECK_MS the
//     chip wakes, resumes touch (~110 ms, no firmware reload), looks for a
//     finger for DETECT_MS, and goes back to sleep if there is none
#define WAKE_MODE   1
static constexpr uint32_t CHECK_MS  = 3000;
// After resume(false) the controller calibrates and a MOVING finger is first
// reported ~110 ms later (measured on a healthy unit: ~84 ms after resume(true),
// which itself spends 30 ms on its firmware check).  So: light-sleep on a timer
// until QUIET_MS, then poll every 5 ms until DETECT_MS.  (Don't wake on the
// touch INT during calibration — on a faulty unit it sat low and the chip woke
// every ~1.8 ms; on a healthy one it idles high.)
static constexpr uint32_t QUIET_MS  = 100;
static constexpr uint32_t DETECT_MS = 170;

#define DIAG_STAGES 0   // 1 = attribution stages 4-7 (CPU awake) instead of the touch-wake loop
                        // 2 = floor attribution: four 15 s LIGHT-sleep plateaus A-D

JC8012P4A1C_Board board;
chipguy_JC8012P4A1C_display display(board);

static constexpr uint32_t STAGE_MS = 15000;
static constexpr uint32_t AWAKE_MS = 10000;

static constexpr gpio_num_t C6_EN   = (gpio_num_t)JC8012P4A1C_Board::C6_RESET_PIN;     // 54
static constexpr gpio_num_t TP_INT  = (gpio_num_t)JC8012P4A1C_Board::TOUCH_INT_PIN;    // 21
static constexpr gpio_num_t TP_RST  = (gpio_num_t)JC8012P4A1C_Board::TOUCH_RST_PIN;    // 22
static constexpr gpio_num_t BOOT    = (gpio_num_t)JC8012P4A1C_Board::BOOT_BUTTON_PIN;  // 35
static constexpr int        LED     = JC8012P4A1C_Board::RGB_LED_PIN;                  // 26
static constexpr gpio_num_t LCD_RST_PIN = (gpio_num_t)JC8012P4A1C_Board::LCD_RST_PIN;  // 27

static const uint16_t COLOURS[] = { 0x07E0, 0x001F, 0xF800, 0xFFE0, 0x07FF, 0xF81F };  // RGB565
static uint8_t  s_colour = 0;
static uint8_t  s_fb = 0;

static void blink(int n, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < n; i++) {
        rgbLedWrite(LED, r, g, b); delay(150);
        rgbLedWrite(LED, 0, 0, 0); delay(250);
    }
}

static void log(const char *fmt, ...) {
    char buf[160];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    Serial0.printf("[%7lu ms] %s\n", (unsigned long)millis(), buf);
    Serial0.flush();
}

// Fill the whole screen with one colour through the normal draw-buffer path.
static void showColour(uint16_t c) {
    uint16_t *db = display.getDrawBuffer(0);
    size_t n = (size_t)display.width() * display.height();
    for (size_t i = 0; i < n; i++) db[i] = c;
    display.flip(0, s_fb);
    s_fb ^= 1;
}

static void stage(int n, const char *what) {
    log("stage %d: %s", n, what);
    blink(n, 0, 40, 0);
    delay(STAGE_MS);
}

// Read touch until the controller reports nothing for a while, so its INT line
// has returned to idle before we sample it and arm the wake.
static void waitForRelease() {
    uint32_t quietSince = millis();
    while (millis() - quietSince < 300) {
        uint16_t x, y;
        if (display.getTouchDriver().getTouch(&x, &y)) quietSince = millis();
        delay(15);
    }
}

void setup() {
    Serial0.begin(115200);
    delay(300);

    // Hold the ESP32-C6 in reset for the whole run (light sleep keeps it held).
    pinMode(C6_EN, OUTPUT);
    digitalWrite(C6_EN, LOW);
    rgbLedWrite(LED, 0, 0, 0);

    if (!display.begin(0)) {
        log("display.begin failed");
        while (true) delay(1000);
    }
    log("display up, touch %s", display.touchAvailable() ? "OK" : "NOT available");
    showColour(COLOURS[s_colour]);

    // Before anything is suspended: confirm touch works right after the normal
    // full startup.  Tap during the green screen.
    log("stage 1: display on, backlight 100%% - tap the green screen");
    blink(1, 0, 40, 0);
    {
        int taps = 0; bool down = false; uint8_t intLow = 0;
        uint32_t end = millis() + STAGE_MS;
        while ((int32_t)(end - millis()) > 0) {
            uint16_t x, y;
            bool t = display.getTouchDriver().getTouch(&x, &y);
            if (t && !down) taps++;
            down = t;
            if (gpio_get_level(TP_INT) == 0 && intLow < 255) intLow++;
            delay(15);
        }
        log("stage 1: %d taps seen (touch working awake: %s), INT-low samples %u",
            taps, taps ? "YES" : "NO", intLow);
    }

    display.setBacklight(0);
    stage(2, "backlight off, panel running");

    display.setBacklight(100);   // so wake() restores 100 %
    display.sleep();
    stage(3, "panel asleep (Display Off + Sleep In), stream paused, CPU awake");

#if DIAG_STAGES == 2
    // Floor attribution in light sleep, where a few mA is visible.  Each plateau
    // is 15 s; the LED blink between them marks the boundary on the trace.
    display.getTouchDriver().suspend();          // touch in reset (C6 already is)
    gpio_sleep_sel_dis(C6_EN);
    gpio_sleep_sel_dis(TP_RST);
    gpio_sleep_sel_dis(LCD_RST_PIN);
    gpio_sleep_sel_dis(BOOT);
    auto plateau = [](const char *what) {
        log("plateau %s", what);
        blink(1, 0, 40, 0);
        esp_sleep_enable_timer_wakeup(15ULL * 1000000ULL);
        esp_light_sleep_start();
    };
    gpio_set_level(LCD_RST_PIN, 1);
    plateau("A: panel Sleep In, stream paused, D-PHY + LDO3 on (= between-bursts state)");
    mipi_dsi_phy_ll_power_on_off(&MIPI_DSI_HOST, false);
    plateau("B: + D-PHY shut down");
    ldo_ll_enable(LDO_ID2UNIT(3), false);
    plateau("C: + LDO channel 3 off");
    gpio_set_level(LCD_RST_PIN, 0);
    plateau("D: + panel held in reset");
    log("light sleep until BOOT (then restart)");
    blink(2, 40, 0, 0);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_enable(BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    esp_restart();
#elif DIAG_STAGES == 1
    mipi_dsi_phy_ll_power_on_off(&MIPI_DSI_HOST, false);
    stage(4, "+ D-PHY shut down");

    ldo_ll_enable(LDO_ID2UNIT(3), false);
    stage(5, "+ LDO channel 3 (D-PHY 2.5 V) off");

    gpio_set_level(LCD_RST_PIN, 0);
    stage(6, "+ panel held in hardware reset");

    gpio_set_level(TP_RST, 0);
    stage(7, "+ touch held in reset");

    log("light sleep until BOOT (then restart)");
    blink(1, 40, 0, 0);
    gpio_sleep_sel_dis(C6_EN);
    gpio_sleep_sel_dis(TP_RST);
    gpio_sleep_sel_dis(LCD_RST_PIN);
    gpio_sleep_sel_dis(BOOT);
    gpio_wakeup_enable(BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    log("BOOT pressed: restarting");
    esp_restart();
#endif
}

#if WAKE_MODE == 1
// Swipe-to-wake.  Touch sits in reset while the chip light-sleeps; every
// CHECK_MS it is resumed (no firmware check), the chip light-sleeps AGAIN
// through the controller's silent calibration, then polls briefly.
static void sleepUntilSwipe(uint32_t &checks, bool &byBoot, uint32_t &detectMs) {
    auto &tp = display.getTouchDriver();
    gpio_sleep_sel_dis(C6_EN);
    gpio_sleep_sel_dis(TP_RST);
    gpio_sleep_sel_dis(BOOT);
    gpio_wakeup_enable(BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    checks = 0;
    for (;;) {
        // Touch off, sleep until the next check (or BOOT).
        tp.suspend();
        esp_sleep_enable_timer_wakeup((uint64_t)CHECK_MS * 1000ULL);
        esp_light_sleep_start();
        if (gpio_get_level(BOOT) == 0) { byBoot = true; tp.resume(false); break; }

        // Touch on (no firmware check — retention is measured), sleep through
        // the controller's calibration, then poll briefly.
        checks++;
        tp.resume(false);
        uint32_t t0 = millis();
        esp_sleep_enable_timer_wakeup((uint64_t)QUIET_MS * 1000ULL);
        esp_light_sleep_start();
        if (gpio_get_level(BOOT) == 0) { byBoot = true; break; }
        bool seen = false;
        while (millis() - t0 < DETECT_MS) {
            uint16_t x, y;
            if (tp.getTouch(&x, &y)) { byBoot = false; detectMs = millis() - t0; seen = true; break; }
            delay(5);
        }
        if (seen) break;
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable(BOOT);
}
#endif

void loop() {
    static uint32_t wakes = 0;

#if WAKE_MODE == 1
    waitForRelease();
    log("sleeping: swipe a finger across the screen to wake (up to one check period) (checks every %lu ms); BOOT also wakes",
        (unsigned long)CHECK_MS);
    blink(1, 40, 0, 0);
    uint32_t checks = 0, detectMs = 0; bool byBoot = false;
    uint32_t t0 = millis();
    sleepUntilSwipe(checks, byBoot, detectMs);
    wakes++;
    if (byBoot) log("woke #%lu after %lu ms (BOOT), %lu touch checks", (unsigned long)wakes,
                    (unsigned long)(millis() - t0), (unsigned long)checks);
    else        log("woke #%lu after %lu ms (swipe, seen %lu ms after resume), %lu touch checks",
                    (unsigned long)wakes, (unsigned long)(millis() - t0),
                    (unsigned long)detectMs, (unsigned long)checks);
#else
    waitForRelease();
    int idle = gpio_get_level(TP_INT);
    log("sleeping: touch INT idle level %d, waking on %s; BOOT also wakes",
        idle, idle ? "LOW" : "HIGH");
    blink(1, 40, 0, 0);

    // Keep the C6's reset (and every other pin) as driven during light sleep.
    gpio_sleep_sel_dis(C6_EN);
    gpio_sleep_sel_dis(TP_RST);
    gpio_sleep_sel_dis(TP_INT);
    gpio_sleep_sel_dis(BOOT);

    gpio_wakeup_enable(TP_INT, idle ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    gpio_wakeup_enable(BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    uint32_t t0 = millis();
    esp_light_sleep_start();

    gpio_wakeup_disable(TP_INT);
    gpio_wakeup_disable(BOOT);
    wakes++;
    bool byBoot = gpio_get_level(BOOT) == 0;
    log("woke #%lu after %lu ms (%s)", (unsigned long)wakes,
        (unsigned long)(millis() - t0), byBoot ? "BOOT" : "touch");
#endif

    uint32_t tw = millis();
    display.wake();
    s_colour = (s_colour + 1) % (sizeof COLOURS / sizeof COLOURS[0]);
    showColour(COLOURS[s_colour]);
    log("display back in %lu ms", (unsigned long)(millis() - tw));
    blink(1, 0, 0, 40);

    uint32_t until = millis() + AWAKE_MS;
    while ((int32_t)(until - millis()) > 0) {
        uint16_t x, y;
        if (display.getTouchDriver().getTouch(&x, &y)) {
            log("touch at %u,%u", x, y);
            delay(200);
        }
        delay(15);
    }
    display.sleep();
}
