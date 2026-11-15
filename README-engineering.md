# Guition JC8012P4A1C — power engineering notes

The derivations behind [README.md](README.md)'s "Power characteristics at a glance".
Part references are from Guition's schematic, sheet `1_PWR` unless noted; the full
reading is the board library's
[schematic-description.md](https://github.com/chipguyhere/JC8012P4A1C_board/blob/main/schematic-description.md)
(§1.2, §1.3 and §3 for power). IP5306 behaviour is from Injoinic's datasheet for the
non-I²C part and is **inferred** until measured on this board. Nothing here was
measured.

---

## The power path

```
USB1/USB2/USB3 VBUS ─┬─ USB5V_IN ── IP5306 VIN ──┐                 ┌─ U2 TLV62569 → 3.3 V → everything logic
CN2 VIN ── Q1 ───────┘   (D8 TVS)                 ├─ VOUT = VOUT-BAT ┼─ IC1 MP3202 → backlight
                                  CN1 BAT+ ── IP5306 BAT/SW (L1)     └─ U11 NS4150 → speaker
                                        └─ R2 68k / R6 100k → GPIO52
```

There is exactly one power path and it goes **through the IP5306** (U1) in every case.
With 5 V on `VIN` the IP5306 passes it to `VOUT` and charges the cell from it; without,
it boosts the cell to 5 V on `VOUT`. The 3.3 V buck, the backlight boost and the speaker
amplifier all hang off `VOUT-BAT`. So the IP5306's output being on is the board being
on, and its output being off is the board being off — including the 3.3 V rail and hence
the CH340C, the codec and the RTC's normal supply (the RTC falls back to its CR1220).

Input conditioning: R1 2 Ω + C7 10 µF on `VIN` (an RC damping the input against
hot-plug ringing of the USB cable's inductance), C1 10 µF. Battery node: C2 22 µF, and
R3 0.5 Ω + C8 22 µF — a second, deliberately lossy bulk cap that damps the LC formed by
L1 and the ceramic C2. Output: 4 × 22 µF.

## The IP5306, as used here

**No bus, no indicators.** The IP5306 comes in a plain version and an I²C version
(`IP5306_I2C`, where two LED pins become SCL/SDA). This board uses LED1–3 as
no-connects and routes nothing else to the P4, which fits either version wired as
plain; with no bus connected, the I²C version's registers would be unreachable anyway.
Either way: **no register access, no status**. The "charging" and "full" states the
chip computes are only ever shown on LED pins that go nowhere.

**KEY (pin 5)** is pulled to ground through R5 10 kΩ by SW1. Datasheet behaviour: a
short press (≥ ~30–50 ms) turns the boost on; two short presses within ~1 s turn it off;
a press held ≥ ~2 s toggles the flashlight driver (pin shared with an LED output —
unconnected here, so nothing visible happens). With `VIN` present the output is on
regardless: a double press with USB connected and no cell fitted did **not** cut the
output (observed).

**Light-load shutdown.** The IP5306 datasheet class describes switching the boost off
when the output current stays below roughly **45–50 mA for ~32 s** — the "power bank
turns off when the phone is full" feature. **On this board it did not happen** at about
8 mA of 5 V load (~10 mA from the cell) sustained over several minutes (measured,
[below](#measured-sleep-currents)). Either this part has the feature disabled or its
detection works differently from the datasheet description; either way there is no
software power-off, sleep keeps the IP5306 on, and timer wake works on battery.

**Low-battery cutoff.** The IP5306 stops boosting when the cell falls to about 3.0 V
(datasheet class, measured at the BAT pin under load) — an abrupt output cut. Hence the
README's software floor of 3.3–3.4 V, where the resting-cell curve still has some
margin above the chip's cutoff even with the backlight's sag.

**Charging.** Constant-current / constant-voltage to **4.2 V** (the plain part's fixed
termination), with the charge current regulated from the input side at up to about
**2.1 A** at 5 V (the datasheet's headline figure; the actual current into the cell
depends on the cell voltage and the part's efficiency). Trickle-charge below ~3 V.

**Against Guition's cell.** The battery-included SKU ships a LiPo pouch cell of about
50 g (observed). Pouch cells run about 50–60 mAh/g, so ≈ 2,500–3,000 mAh, ≈ 10 Wh
(inferred). Input-side 2.1 A at 5 V is 10.5 W; at ~90 % efficiency into a 3.7–4.2 V
cell that's 2.2–2.5 A at the start of constant-voltage — **≈ 0.75–1.0 C** worst case,
more likely ~0.8 C. Acceptable for most pouch cells (1 C max charge is a common rating)
but warm, and the IP5306 has no temperature input. Two things cut it in practice: the
2.1 A input limit is shared with the board's own load (Guition quotes ~700 mA), and
the USB source may deliver less. **TODO (bench):** measure charge current at the cell
with the board idle and with the backlight on. No
NTC input exists on this package, so there is no temperature qualification of charging.
Charge current and end-of-charge are **TODO (bench)**.

**Without a cell.** The board runs normally on USB with no cell fitted (observed): the
IP5306's pass-through carries it. The charger, seeing no cell, floats or cycles the BAT
pin, and the sense divider on `BAT+` reads that node — what the number looks like is
**TODO (bench)**.

## Battery sense arithmetic

`BAT+` → R2 68 kΩ → node → R6 100 kΩ → GND; node → R4 0 Ω → GPIO52.

- Ratio: 100 / (68 + 100) = **0.595**, so the library multiplies by **1.68**.
- Full cell 4.20 V → **2.50 V** at the pin; 3.30 V → 1.96 V; 3.00 V → 1.79 V. All inside
  the P4 ADC's widest-attenuation range (`ADC_11db` in the Arduino API).
- Continuous drain: 4.2 V / 168 kΩ = **25 µA**, whether the board is on or off. That's
  the floor under the "Off" row: ~0.6 mAh/day, ~18 mAh/month.
- Source impedance: 68 ‖ 100 = **40.5 kΩ**, and **there is no filter capacitor** on the
  node. The P4's SAR ADC charges a small sample capacitor from the pin each conversion;
  from 40 kΩ that can read slightly low and noisy. `analogReadMilliVolts()` averaged over
  8 samples is the mitigation; a 100 nF capacitor across R6 would be the hardware fix.
  **TODO (bench):** compare with a meter.
- Error budget (inferred): eFuse-calibrated ADC ≈ ±10–20 mV at the pin → ±17–34 mV at the
  cell; resistor tolerance (unknown; if 1 %, ±1.3 % worst case on the ratio → ±55 mV at
  4.2 V). Hence "about ±50 mV".
- On USB, `BAT+` is the charger's output node, so the reading is the charge voltage, not
  the cell's resting voltage.

GPIO52 is **ADC2_CHANNEL3** (P4 datasheet, table 2-7: ADC2 channels 0–5 on GPIO49–54). Unlike the classic ESP32's ADC2 it has no Wi-Fi
conflict, because the P4 has no radio of its own (the C6 does the radio).

## Sleep and wake on the P4

- **Wake sources in deep sleep:** the timer, and ext1 on **LP GPIOs 0–15**. On this board
  GPIO0/1 are the 32 kHz crystal, GPIO6 is the C6 link, GPIO7–15 are I²C, I²S and C6
  SDIO — leaving **GPIO2–5** (FPC3) as the only free wake pins. `deepSleep()` uses
  `esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW)`. BOOT is GPIO35 and
  touch INT is GPIO21 — HP-domain pads, not wake-capable. The RX8025T's INT# is not
  routed.
- **Pad states in deep sleep:** HP-domain pads lose their drivers, so GPIO23 (backlight
  EN, R16 10 kΩ pull-down) and GPIO20 (amp CTRL, R86 10 kΩ pull-down) fall low by
  themselves — nothing needs `gpio_hold`. The panel reset (GPIO27) and touch reset
  (GPIO22) float; both chips stay powered, and a floating reset line could leave either
  in reset or not — irrelevant, since the display is re-initialised on wake.
- **On battery** the same applies: the IP5306 stays on through deep sleep (observed), so
  timer and GPIO2–5 wake work, at the deep-sleep current.

## Measured sleep currents

Power profiler on the battery connector, USB unplugged, Guition's ~50 g pouch cell, the
`SleepCurrentProbe` example (2026-09-30). The panel was never initialised; no camera, no
card. Chip: **ESP32-P4 revision v1.3** (from the upload log).

| Step | Cell current | Change | Conclusion |
|---|---|---|---|
| Awake, nothing touched (C6 running since power-on) | ~100 mA | — | |
| + C6 held in reset (GPIO54 low) | 78 mA | −22 mA | the C6 is the largest quiescable load |
| + touch held in reset (GPIO22 low) | 75 mA | −3 mA | |
| + panel reset low / released | 75 mA | ~0 | an uninitialised JD9365 draws little either way |
| **Deep sleep**, `gpio_hold_en()` on 54/22/27 | **~40 mA** | | the hold does not survive deep sleep on rev < 3.0 (ESP-IDF note DIG-399): GPIO54 floats, R59 pulls `C6_CHIP_PU` up, the C6 reboots and runs (inferred from the current; **TODO:** confirm ~3.3 V on CN5 pin 6) |
| **Light sleep**, C6/touch/panel pins still driven | **~10 mA** average, 1–20 mA ripple | | the floor so far; **the IP5306 did not cut out** |

A second run with the display in real use (`TouchWakeProbe`, `DIAG_STAGES 1`): display
up, then slept, then one more thing switched off per stage, each cumulative, with the CPU
awake (averages over the stage):

| Step | Cell current |
|---|---|
| Display running, backlight 100 % | > 1 A (profiler's range exceeded) |
| Backlight off | ~250 mA |
| Panel Display Off + Sleep In, video stream paused | ~98 mA (reads ~110 with jitter) |
| + MIPI D-PHY shut down | no change |
| + P4 LDO channel 3 (D-PHY 2.5 V) off | no change |
| + panel held in hardware reset | no change |
| + touch controller held in reset | **~75 mA** |
| Light sleep with all of the above | **~5.9 mA** |
| (for comparison) Light sleep with touch still scanning | ~45 mA |

So the **GSL3680, once its firmware is loaded and it is scanning, draws ~25–40 mA** —
the first run's "−3 mA for touch" was a controller that had never been given firmware.
The display link and the sleeping panel draw nothing measurable. `display.sleep()` must
pause the video stream before light sleep (without it the DSI DMA is cut off mid-frame,
the bus hangs, and the hardware watchdog resets the chip — observed); putting the D-PHY
into ULPS as well was tried and measured no benefit, so it isn't done.

**Duty-cycled touch ("swipe to wake").** Keep touch in reset during light sleep, wake
on a timer, bring touch back with `gsl3680_touch::resume()` and look for a finger, reset
it again if none (`TouchWakeProbe`, `WAKE_MODE 1`). Measured:

- The GSL3680 **keeps its firmware through a held reset** — checked after 0.1 s, 1 s,
  5 s, 30 s and 2 min: the firmware word reads 0x5A5A5A5A and touch works, with no
  re-download. `resume()` takes 110 ms, or ~80 ms with `verify=false`.
- A finger **already resting** on the screen when touch restarts is absorbed into the
  controller's startup calibration and **never reported** — the wake gesture must be a
  **moving** finger. A swipe is first reported **~118 ms after `resume(false)`**.
- The controller scans on its own after `resume()`; the P4 can light-sleep through the
  calibration. Its INT line idles high on a healthy unit and must not be used as a wake
  source during calibration (on one unit whose touch later failed outright it sat low,
  and the chip woke every ~1.8 ms at ~97 mA).
- Final sequence: suspend → light sleep 1 s → `resume(false)` (~80 ms awake) → light
  sleep 100 ms (touch scanning: **42.5 mA**) → poll every 5 ms until 170 ms → suspend.
  **Average 24.0 mA** at 1 s checks (the earlier all-awake version: 32.5 mA).
  At **3 s checks: 16.1 mA** average, 11.7 mA between checks; waking stays reliable
  with touch off 3 s at a time.

**What the floor between checks is made of** (unit 2, light sleep, 15 s plateaus,
`DIAG_STAGES 2`):

| Plateau | Avg |
|---|---|
| A: C6 + touch in reset, panel Sleep In, stream paused, D-PHY + LDO3 on (= between checks) | 12.1 mA |
| B: + D-PHY shut down (`phy_shutdownz = 0`) | 16.5 mA — **worse** by 4.4 |
| C: + LDO channel 3 off | 13.9 mA |
| D: + panel held in reset | 10.1 mA |
| then, settled | 9.9 → 8.9 mA over ~30 s |

So this unit's floor is ~9–10 mA (unit 1's was 5.9 — board-to-board spread, not a
display leftover), and only ~2 mA separates the between-checks state from everything
off. Getting it would mean holding the panel in reset (a full ~400–500 ms panel re-init
on every wake) and switching the D-PHY and its LDO together — shutting the PHY down
alone costs 4.4 mA, presumably from lanes left floating into the panel's receivers. Not
done: diminishing returns.

Consequences:

- **Use light sleep, not deep sleep,** on rev < 3.0 silicon with the C6 fitted. A P4
  rev ≥ 3.0 board could hold GPIO54 through deep sleep and should then do better than
  light sleep.
- **No software power-off** — the IP5306's light-load shutdown didn't trigger at ~8 mA of
  5 V load. The power button is the off switch.
- Guition's demos contain no sleep, pad-hold or power-management code for this board;
  the vendor hadn't explored this.

### What's left in the ~10 mA

At ~3.8 V that is ~38 mW. Candidates (from the table below): the P4 in light sleep with
its HP domain and PSRAM still powered (a few mA), the IP5306's own consumption boosting
into a tiny load (it runs a switching converter continuously — likely a large share, and
the probable source of the 1–20 mA ripple), the CH340C with no host (suspend vs not),
the WS2812 (~0.5–1 mA), and the panel and codec in their power-on states. Next
experiments, each cheap with the probe sketch: light sleep with `esp_sleep_pd_config()`
powering down more domains; deep sleep with the C6 told to sleep over ESP-Hosted (newer
ESP-Hosted-MCU has host-commanded slave power save) or — for Wi-Fi-less projects — the
C6 flashed through CN5 with firmware that simply deep-sleeps.

## The sleep budget, component by component

The pre-measurement analysis, kept because it names every load. Datasheet-class
figures, inferred.

### Every load on the 3.3 V rail, with the P4 in deep sleep

Datasheet-class figures, inferred, not measured on this board.

| Load | Not quiesced | Quiesced | How to quiesce | Notes |
|---|---|---|---|---|
| **ESP32-C6** (U7) | **~20–40 mA** CPU running ESP-Hosted, radio idle; 80–100+ mA if Wi-Fi was associated | **~1 µA** in reset | hold `C6_CHIP_PU` (GPIO54) **low through deep sleep** | **Prime suspect** — see below |
| **JD9365 panel** (logic, charge pumps, source/gate drivers on `ESP_3V3`) | **~10–40 mA** left "display on" with the DSI stream stopped (typical for a 10" panel's logic side) | **< 1 mA** in Sleep In | Display Off (0x28), Sleep In (0x10), wait 120 ms, *then* sleep; keep `LCD_RST` (GPIO27) from floating | Second suspect |
| **Camera** on FPC5 (if fitted) | ~10–100 mA depending on sensor and streaming | — | unplug it; its enable is tied on | Rule out by test setup |
| **GSL3680** touch | ~3–8 mA scanning | < 0.1 mA in reset | hold `TOUCH_RST` (GPIO22) low through sleep | Too small alone |
| **ES8311** codec (+ mic bias) | ~5–15 mA if it was configured and left running | ~µA | write its power-down registers | Too small alone; nothing if audio never started |
| **CH340C** USB-serial | ≤ ~12 mA if it thinks it's active | ~0.1 mA suspended | none from software | With no USB host it should see an idle bus and suspend (**TODO**: confirm) |
| **WS2812** LED | 0.3–1 mA dark | same | none | floor |
| W25Q128 flash | 10–50 µA standby | ~1 µA in deep power-down | release-power-down/power-down command | floor |
| ESP32-P4 deep sleep | tens of µA | — | — | its core DC-DC (U3) is off via `EN_DCDC` |
| TLV62569 (3.3 V) quiescent | ~35 µA | — | — | |
| RX8025T | ~1 µA | — | — | |
| SDIO pull-ups R61–R66 (51 k) | ≤ 0.4 mA if lines sit low | 0 if lines float high | — | only while the C6's pins drive low |
| Bus pull-ups, R34, R40, R16, R86 | 0 | 0 | — | lines idle at the resistor's own rail |
| TF card (if inserted) | ~0.1–1 mA idle | 0 | LDO4 off (P4 LDOs power down in deep sleep — **TODO**: confirm) | remove it for the test |

**Sum, everything quiesced:** P4 + flash + buck + RTC + WS2812 + CH340C suspended + panel
in Sleep In + touch/codec off ≈ **1–2 mA at 3.3 V**, about **1–1.5 mA at 5 V** — some
thirty times under the threshold. So a real software power-off is **expected to be
possible**; the board does not have an unavoidable load big enough to prevent it.

**What could hold it over the line on its own:** only the **C6** (running), the **panel**
(display-on), or a **camera**. Touch, codec and CH340C together are at most ~20–30 mA
even un-quiesced — enough to push a marginal case over, not enough alone.

### Why the C6 was the prime suspect (confirmed)

Its enable, `C6_CHIP_PU`, has **R59 10 kΩ to 3.3 V and C82 1 µF to ground** (sheet
`5_ESP32-C6`). Driving P4 GPIO54 low holds the C6 in reset — **while the P4 is awake**.
When the P4 enters deep sleep, its HP-domain pads (GPIO54 among them) release unless
explicitly held, and R59 pulls `CHIP_PU` back up: about 10 ms later (R59 · C82) the C6
**boots its ESP-Hosted firmware and runs**, searching in vain for a host that is asleep.
A sketch that "holds the C6 in reset" awake and then deep-sleeps would therefore measure
a running C6 — 20–40 mA at 3.3 V on its own, which with the panel un-slept is plausibly
the whole story. The same applies to `TOUCH_RST` and `LCD_RST`.

Two further ways the C6 can escape: the core's ESP-Hosted transport resets the C6 itself
(through GPIO54) whenever Wi-Fi is started, and the C6 may simply never be told anything
if the sketch never started Wi-Fi — in which case it runs its firmware from power-on
regardless.

**How to tell, with a meter:** `C6_CHIP_PU` is brought out on **CN5 pin 6** (the C6's
programming header). Measure it during P4 deep sleep: ~0 V means the C6 is held in
reset; ~3.3 V means it is running. CN5 pin 3 is the C6's UART TX — a USB-serial adapter
there at 115200 will show the C6's boot banner the moment the P4 goes to sleep, if the
pad lets go.

## Estimates behind the consumption table

- **Off:** 25 µA (divider) + IP5306 standby with output off (datasheet class: tens of µA
  or less) — call it **≈ 30–75 µA**, i.e. months of shelf life on a few-Ah cell before
  its own protection or the IP5306's cutoff. Inferred.
- **Awake, backlight on:** the backlight dominates. The MP3202 regulates the LED string
  current through R13‖R14 = 1.1 Ω (display library's engineering notes); with the
  datasheet's feedback reference that is on the order of 100 mA through a string of
  unknown voltage — a few watts. Guition's 700 mA at 5 V (3.5 W) for the whole board is
  consistent with that.
