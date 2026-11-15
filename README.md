# chipguy_JC8012P4A1C_power

Battery and sleep helpers for the **Guition JC8012P4A1C** — the 10.1-inch ESP32-P4 touch
screen, which can run from a lithium battery on its back. It reads the battery voltage
and puts the board to sleep.

It is deliberately small, because the board gives software very little to hold on to.
All the power handling is done by one chip — an **IP5306** "power bank" chip, the same
kind found in phone power banks — that charges the battery, makes 5 V from it, and
switches itself on and off, with no wire to the ESP32-P4 at all. The section below says
exactly what is and isn't possible, so you can decide up front whether this board fits a
battery project. (Short version: it's a good *portable* screen, and a poor *sleeping*
one — even asleep it draws enough that the power chip never switches off, and software
has no way to turn it off.)

One of four libraries for the board:

| Library | What it does |
|---|---|
| [chipguy_JC8012P4A1C_display](https://github.com/chipguyhere/JC8012P4A1C_display) | The screen and touch at 800×1280, with LVGL |
| [chipguy_JC8012P4A1C_480display](https://github.com/chipguyhere/JC8012P4A1C_480display) | The same screen, drawn 480 wide and scaled up |
| [chipguy_JC8012P4A1C_board](https://github.com/chipguyhere/JC8012P4A1C_board) | The shared I²C bus, the pin map, and what else is on the board |
| **chipguy_JC8012P4A1C_power** (this one) | The battery and sleep |

Answers marked **inferred** come from the schematic and the IP5306's published
behaviour. Answers marked **observed** were seen on a real board (2026-09-30, and an
earlier power-profiling session on the battery connector); **TODO (bench)** marks what
only a measurement would settle. The derivations are in
[README-engineering.md](README-engineering.md).

---

## Power characteristics at a glance

### Power sources

**Can this board be powered by USB?** Yes, entirely, with or without a battery — from
any of the three USB-C ports, or 5 V on pin 1 of the 4-pin UART connector CN2. All of
them feed the IP5306's input, and the IP5306 passes it through to the rest of the board
while charging the battery. Guition quotes about **700 mA at 5 V** for the whole board.

**Can this board run on battery power?** Yes. A single lithium cell (3.7 V nominal,
4.2 V full) on the 2-pin connector **CN1** (photo item 4). Guition sells a version with
a battery (SKU `…_Y1`): a lithium-polymer pouch cell weighing about **50 g** (observed),
which suggests roughly **2,500–3,000 mAh** (inferred from weight; **TODO (bench):** read
the cell's label). From the battery, the
IP5306 boosts the cell to 5 V and the board runs exactly as on USB.

**Can the board recognize power from an input that is neither battery nor USB?** No.
CN2's 5 V input is wired to the same point as the USB ports — to the board it *is* USB.

**Can software tell which source is powering the board right now?** No. No line from
the IP5306 or from USB reaches a GPIO. The only hint is indirect: with USB present the
battery reading is the charger's voltage, which climbs toward 4.2 V and stays there
(see [battery measurement](#battery-measurement)). That can't tell "on USB, cell full"
from "on battery, cell full".

**Can we ask the hardware to stay powered only while on external power?** No. The
IP5306 switches to the battery by itself the moment USB goes away, and nothing can tell
it not to. What the board does instead: with a cell fitted it runs from the cell until
you double-press the power button or the cell runs down to the IP5306's cutoff. If you
never want to run on the battery, don't fit one — but see
[always-on](#recommendations) first.

### Power management chip and protection

**Does this board have a PMIC?** Partly — an **IP5306** (Injoinic), the plain version
**without I²C**. It is a charger, a 5 V boost and an on/off switch in one, running its
own fixed state machine. It is **not connected to the ESP32-P4 in any way**: its four
LED-indicator pins, which on a power bank would drive the charge lights, are unconnected
here, and it has no bus. Everything below that says "fixed" is because of this.

**Does the board prevent the cell discharging below a minimum?** Partly. The IP5306
switches its output off when the cell sags to its low-battery threshold — about
**3.0 V** under load (inferred from the part's datasheet; **TODO (bench)**). That is an
**abrupt** cut: the board just goes dark, with no warning to software. Whether the cell
itself also has a protection circuit depends on the cell (Guition's is **TODO
(bench)**). **The sketch owns the graceful floor**: watch `getBatteryVoltage()` and save
your state and shut down around 3.3–3.4 V, before the IP5306 does it for you.

**Can software decide whether the battery charges, or set a charge level?** No.
Charging is automatic whenever 5 V is present, and fixed by the chip: it charges to
**4.2 V** at up to about **2 A** from the 5 V input (inferred from the IP5306's
datasheet; the plain version has no settings). For Guition's ~2,500–3,000 mAh cell that
is at most about 0.8 C — within what most pouch cells accept, though above the gentler
0.5 C "standard" rate; in practice charging is slower, because the same input current
also runs the board and many USB ports can't supply 2 A.
The IP5306 has **no temperature sensor input**, so nothing stops it charging a hot cell.

### Battery measurement

**Can we read the battery level?** Yes, as a **voltage**: `power.getBatteryVoltage()`
reads **GPIO52**, which sees the cell through a **68 kΩ / 100 kΩ** divider (the pin sees
100/168 of the cell voltage — 2.5 V for a full cell), using the ESP32-P4's
factory-calibrated `analogReadMilliVolts()` at the widest attenuation, averaged over 8
samples. `getBatteryPercent()` maps that through a generic lithium-cell curve.

**What is the practical resolution?** About **±50 mV** on the cell (inferred;
**TODO (bench):** compare against a meter at 3.5, 3.8 and 4.1 V). Good enough for a
battery icon and a low-battery cutoff; not for a fuel gauge. The percentage is a rough
guess on a resting cell and wrong under load — the backlight alone pulls the reading
down.

**Can we tell whether charging is active?** No. The IP5306 would report it on its LED
pins, and those go nowhere.

**Can we tell whether charging voltage is available?** No. There is no USB-present
line (see [power sources](#power-sources)).

**Can the board detect whether a battery is present?** No. With no cell fitted and USB
plugged in, GPIO52 sees whatever the IP5306's charger leaves on the empty connector —
**TODO (bench):** most likely a plausible-looking 4.2 V "full battery" that is a lie,
possibly a reading that jumps around as the charger probes for a cell. Either way,
don't trust the number unless you know a cell is fitted.

**Does the reading mean the same thing on USB and on battery?** No. On USB it is the
charger's voltage and reads high; the state-of-charge estimate is only valid on battery,
after the reading has settled for a few seconds.

### Indicators

**Power light?** No. The board has no power LED. The **RGB LED** on GPIO26 is yours to
use (`board.setLed()` in the board library) if you want one.

**Battery / charge light?** No. The IP5306's four charge-light outputs are left
unconnected.

### Shutting down, sleeping, waking

**Can this board shut itself off?** **No.** The IP5306's datasheet describes a "light
load" feature that turns its 5 V output off when almost nothing is drawn (under roughly
45–50 mA for about 32 seconds). **On this board it doesn't happen:** with the board in
light sleep drawing about **10 mA from the cell** — around 8 mA at the IP5306's 5 V
output, far under that line — it kept running through minute after minute of quiet
(observed, 2026-09-30, `SleepCurrentProbe` example). So `power.shutdown()` is a sleep,
not an off, and the power button's double press is the only real off. Details in
[README-engineering.md](README-engineering.md#measured-sleep-currents).

**Can it still shut itself off when USB is present?** No. With 5 V on the input the
IP5306 passes it straight through and never switches off. On USB, `shutdown()` is deep
sleep that lasts until RESET or a power cycle.

**Can the board be powered down but still wake on a timer?** Only as ESP32-P4 sleep with
the power still on — which, because the IP5306 stays on, works **on USB and on battery
alike**. On this board **light sleep beats deep sleep** (measured: ~10 mA vs ~40 mA from
the cell), for a reason that comes down to the chip revision — see
[power consumption](#power-consumption). Nothing external can bring back a board whose power really is off: the
real-time clock's alarm pin isn't wired to anything, and the IP5306 only restarts on its
button or on USB being plugged in. The cost on battery is that "asleep" is not cheap —
see [power consumption](#power-consumption).

**Which interrupts can wake it?** On USB: the **timer**, and a **LOW level on GPIO2–5**
(the four free wake-capable pins, on the FPC3 expansion connector; they need a pull-up to
3.3 V). Nothing else — the ESP32-P4 can only wake from deep sleep on its pins 0–15, and
on this board GPIO0/1 are the clock crystal and 6–15 are taken. **The BOOT button
(GPIO35) cannot wake it, and neither can touch (GPIO21).** Same on battery.

**IMU wake?** No IMU on this board.

**Touch wake?** No. The touch controller's interrupt goes to GPIO21, which can't wake
the P4 from deep sleep, and its I²C bus is off while asleep anyway.

**Is there something to do at boot to stay on?** No. The IP5306 holds the power on by
itself, and — as above — the board's own draw keeps it above the IP5306's light-load
line even when idle, so it doesn't switch off on its own (observed).

### The power button

**Is there a power button?** Yes — **SW1**, beside the battery connector at the top edge
of the board (with the camera connector on top, landscape). On the as-built unit it is
under the back cover. It goes **only to the IP5306**. No GPIO sees it.

**What does it do in hardware?** The IP5306's fixed behaviour (inferred from its
datasheet): a **short press turns the board on**; **two short presses within about a
second turn it off** (on battery). A long press (over ~2 s) would toggle a flashlight
LED on a power bank — on this board that pin is unconnected, so it does nothing. All of
this is in the chip, not in software, and firmware gets no warning before the power
goes.

**Can we detect the press ourselves?** No.

**Can the button hard-off a hung board?** **On battery, yes** (inferred from the
datasheet) — the double press cuts the 5 V output no matter what the software is doing.
Fixed, not configurable. **On USB, no:** a double press with USB connected (and no
battery) did not cut the power (observed) — the IP5306 keeps passing USB through. Use
the **RESET** button, or unplug USB.

### Other buttons

**Is BOOT exposed to the user?** Yes — **SW3** on GPIO35, readable as
`board.bootButtonPressed()` (board library). It's a fine UI button, but it **can't wake
the board from sleep**, and holding it while the board resets enters upload mode.

**Reset button?** Yes — **SW2** resets the ESP32-P4. Software can't read it. It does not
touch the IP5306, so it can't turn a board *on*.

### Peripheral power switches

**What can we switch?**

| What | How | Default |
|---|---|---|
| Backlight | GPIO23 (display library's `setBacklight()`) | off until the display starts; off in sleep and reset (pull-down) |
| Speaker amplifier | GPIO20 (board library's `ampEnable()`) | off (pull-down) |
| MIPI PHY (display + camera links) | the P4's internal LDO channel 3 | on while the display runs |
| microSD card + GPIO45–48's I/O power | the P4's internal LDO channel 4 | off until something enables it |
| ESP32-C6 radio (~22 mA running, measured) | held in reset by driving GPIO54 low | **running from power-on** (its enable has a pull-up), whether or not Wi-Fi is used |
| Touch controller | held in reset on GPIO22 | running once the display starts |

**What cannot be powered off?** The whole **3.3 V rail** has no switch, so the panel's
logic, the touch controller, the sound chip, the camera (if one is plugged in — its
enable is tied on), the USB-serial chip, the RGB LED and the clock all stay powered
whenever the IP5306 is on. With the C6 and touch held in reset that floor measures about
10 mA from the cell in light sleep.

### Power consumption

Measured with a power profiler on the battery connector, USB unplugged, Guition's cell
(2026-09-30, `SleepCurrentProbe` example; the panel never initialised). Rows still marked
TODO haven't been measured yet.

| State | What's on | Battery current | How measured |
|---|---|---|---|
| **Off** (power button double-pressed) | IP5306 standby, the 168 kΩ sense divider | **TODO (bench)** — inferred floor ≈ 25 µA in the divider at 4.2 V plus the IP5306's own standby current | meter in series with the cell |
| **Light sleep, C6, touch and panel held in reset** (display was running, then slept) | P4 light sleep, 3.3 V rail idle | **~5.9 mA average** (1–20 mA ripple) | power profiler |
| **Light sleep, touch armed to wake** (C6 in reset, panel asleep) | as above + the touch controller scanning | **~45 mA** | power profiler |
| **Light sleep, swipe-to-wake**, touch checked every 1 s (`TouchWakeProbe`) | as above, touch in reset between ~200 ms checks | **24.0 mA average** (42.5 mA while touch scans mid-check) | power profiler |
| **Light sleep, swipe-to-wake**, touch checked every 3 s | same | **16.1 mA average**, 11.7 mA between checks | power profiler |
| **Awake, display running, backlight 100 %** | | **> 1 A** (above the profiler's range) | power profiler |
| **Awake, display running, backlight off** | | **~250 mA** | power profiler |
| **Awake, panel asleep, touch scanning** | | **~98–110 mA** | power profiler |
| **Deep sleep** (`deepSleep()` / `shutdown()`) | as above, but the **C6 restarts and runs** (see below) | **~40 mA** | power profiler |
| **Awake, backlight off, C6 and touch in reset** | | **~75 mA** | power profiler |
| **Awake, backlight off, C6 running** (power-on state) | | **~100 mA** | power profiler |
| **Awake, backlight 100 %, idle UI** | | **TODO (bench)** | series meter |
| **Awake, Wi-Fi active, audio playing** | | **TODO (bench)** | series meter |

**Why deep sleep loses to light sleep here.** The ESP32-C6 Wi-Fi chip draws about
**22 mA** from the cell while running (measured: holding it in reset took the awake
board from 100 to 78 mA). It is held in reset by driving GPIO54 low — but this board's
ESP32-P4 is **revision v1.3**, and on P4 silicon before v3.0 a pin can't be held through
**deep** sleep (an ESP-IDF limitation). GPIO54 lets go, the C6's pull-up restarts it, and
deep sleep costs ~40 mA. **Light** sleep keeps pins driven on any revision, so the C6
stays in reset and the board drops to ~10 mA.

**The touch controller is the other big load.** Once its firmware is loaded and it is
scanning, the GSL3680 draws **~25–40 mA** from the cell (measured: holding it in reset
dropped the awake board from ~98 to ~75 mA, and the sleeping board from ~45 to ~5.9 mA).
So "wake on touch" is the expensive part of any sleep: light sleep with touch armed is
~45 mA, light sleep woken by a button instead is ~5.9 mA. The display link itself (the
MIPI PHY and its 2.5 V LDO) and the panel in Sleep In showed no measurable draw.

The middle ground is **swipe-to-wake**: keep touch in reset while asleep, and bring it
up for ~200 ms every few seconds to look for a finger — measured at **24 mA** checking
every second and **16 mA** every 3 seconds (touch off for 3 s at a time wakes just as
reliably). A resting
finger isn't seen (the controller calibrates it away when it restarts), so the gesture
is a swipe of about a second.

Floors differ from board to board: with everything off, one unit measured 5.9 mA and
another 9–10 mA. With Guition's cell (~2,500–3,000 mAh, inferred) that is roughly
**11–19 days of light sleep woken by a button**, **~7 days with swipe-to-wake at 3 s**,
**2½ days with touch-to-wake**, and **about 3 days of deep sleep**. The off-state drain is not zero either:
the sense divider sits straight across the cell.

### Recommendations

**What should `setup()` do?** Nothing is required for power — no latch to hold, no PMIC
to configure. `power.begin()` is optional (every call does it). Check
`power.wasAwakenedFromDeepSleep()` if you want to skip a splash screen after a timer
wake.

**Always-on on USB with a battery we don't need:** nothing can be configured. The
IP5306 holds the cell at 4.2 V and tops it up whenever it sags, which ages it fastest.
For a permanent wall installation, **take the cell out**: the board runs on USB with no
cell fitted (observed). Without a cell, `getBatteryVoltage()` means nothing; ignore it.

**Battery fitted but we choose not to run on it:** not possible — the switch-over is
automatic and software can't even see USB go away. If that matters, don't fit a cell.

**Battery is the primary source:** poll `getBatteryVoltage()` every few seconds (it's
cheap); treat about 3.5 V as "warn" and 3.3 V as "stop". At the stop point, save your
state, show "please charge or switch off", and sleep — the board then sleeps, still
draining the cell, until someone double-presses the power button or the IP5306's own
~3.0 V cutoff ends it. For any sleep, hold the C6 and touch in reset and use **light**
sleep (see the `SleepCurrentProbe` example until the library grows a helper): ~10 mA,
so periodic-wake designs get days to weeks rather than hours. A battery project on this
board is still "on while in use, switched off with the button otherwise" for anything
longer.

---

## Installation

Download the ZIP from GitHub (**Code ▸ Download ZIP**), unzip, move the folder into your
Arduino `libraries` directory, and restart the IDE. It needs nothing but the ESP32 core.
Board settings: **ESP32P4 Dev Module**, PSRAM **Enabled**, Flash Size **16MB**,
Partition Scheme **Custom**, USB CDC On Boot **Enabled**.

## The API

```cpp
#include <chipguy_JC8012P4A1C_power.h>     // provides a global `power`

float v   = power.getBatteryVoltage();     // e.g. 3.82
int   pct = power.getBatteryPercent();     // rough, 0..100; only meaningful on battery

bool byTimer;
if (power.wasAwakenedFromDeepSleep(&byTimer)) { ... }

power.deepSleep(600);                      // wake after 10 min (~40 mA asleep: the C6
                                           // restarts — see "Power consumption")
power.deepSleep(0, BIT64(2));              // wake when GPIO2 (FPC3) is pulled LOW
power.shutdown();                          // deep sleep, no wake source: "off" as far as
                                           // software can — the power button is the real off
```

There is no `isCharging()`, no `usbPowerPresent()`, no `powerOff()` and no
`powerButtonPressed()`, because the hardware has none of them — see the questionnaire
above rather than looking for a workaround.

## Examples

- **BatteryMonitor** — prints the voltage and percentage once a second; send `s` over
  serial to deep-sleep for 20 s. Plug and unplug USB to watch the reading switch between
  the charger's voltage and the cell's.

## Where to go next

| File | For whom |
|---|---|
| [README-engineering.md](README-engineering.md) | Engineers: the IP5306's behaviour and what it means for this board, the power path from USB and battery to every rail, the sense-divider arithmetic, sleep and wake on the P4, and the estimates behind the "off" row above. |
| [board library README-board.md](https://github.com/chipguyhere/JC8012P4A1C_board/blob/main/README-board.md) | The pin map, the buttons, the free wake-capable pins. |
| [board library README-sound.md](https://github.com/chipguyhere/JC8012P4A1C_board/blob/main/README-sound.md) | The speaker amplifier, which runs from the same 5 V as the backlight. |
| [display library README](https://github.com/chipguyhere/JC8012P4A1C_display/blob/main/README.md) | The backlight. |

## License

MIT — see [LICENSE](LICENSE).
