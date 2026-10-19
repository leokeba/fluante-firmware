# Fluante design

Status: phase 1 (2026-10-19): the strobe and the spinner, their trees and host tests, on TesserKIT, TesserMotion and NowTP; the firmware builds for the classic ESP32 (ESP-IDF 6.1-dev, 1.48 MB, 16 % of the slot free) and runs on DivergentFluante's board over USB (§10): the strobe's settings applied, the TMC2209 not answering yet (motor supply not checked). This is the specification the code follows; when the two disagree, fix one of them. Sections marked *(later)* are committed to but not built yet.

Fluante replaces [leokeba/fluante_firmware](https://github.com/leokeba/fluante_firmware) (Arduino, PlatformIO, ESPUI, TMCStepper, 2023), called "the old firmware" below.

## 1. Goals

A Fluante is a 3D-printed sculpture (fluante_generator) turned by a stepper motor and lit by a strobe: the strobe freezes or slowly animates its pattern, like a zoetrope. The firmware:

- turns the sculpture at a speed, in turns per minute, ramped;
- flashes the strobe at a frequency and duty, set independently of the speed (Leo, 2026-10-19);
- is reached from a phone or a computer over Wi-Fi (its own network until it joins one), updated over the air from this repository's releases;
- shows and sets the other Fluantes nearby, over ESP-NOW (NowTP), from any one of them.

### Non-goals (for now)

- **Phase-synchronized strobes** across Fluantes (the old firmware's clock sync, partly built there): a goal, not in phase 1 (Leo, 2026-10-19). §9 lists what it needs.
- **Step and direction** pulses: the TMC2209 turns the motor by itself (VACTUAL). The old firmware's LEDC-STEP variant and its speed oscillation are dropped (Leo, 2026-10-19).
- **The strobe locked to the rotation** (frames per turn): not tied (Leo, 2026-10-19).
- UDP, Ethernet, Art-Net, MQTT: Wi-Fi and NowTP only (Leo, 2026-10-19).

## 2. Glossary

| Term | Meaning |
|---|---|
| Speed | The sculpture's turns per minute (rpm), signed. |
| Ratio | Sculpture turns per motor turn: the motor pulley's teeth over the sculpture pulley's (54/60 = 0.9). |
| VACTUAL | The TMC2209's velocity register: microsteps per 2^24 cycles of its 12 MHz clock, which isn't calibrated. |
| Settle | The time at standstill after enabling the driver, before turning (the old firmware's "preheat"). |
| Halted | The driver was disabled underneath the spinner (an update, its own tree, a reset of the chip): the spinner waits to be asked again. |

## 3. Hardware

The old firmware's latest board ("new control board", caca2e2): a classic ESP32 (esp32doit-devkit-v1, 4 MB, no PSRAM). Pins in `firmware/main/board.h`:

| Part | Pins | Notes |
|---|---|---|
| TMC2209 | UART 2: RX 26, TX 25, 115200 baud; EN 13 (active low); MS1 14, MS2 27 (low: address 0); STEP 33, DIR 32 (held low, unused) | 0.11 Ω sense resistors (TesserMotion's default). |
| Strobe | GPIO 19 | The LED driver's input. |
| GPIO 18 | driven high at boot | The old firmware did since this board; purpose unknown (§12). |

## 4. Architecture

Only what is specific to a Fluante is here; the rest comes from the libraries, pinned in `deps.lock`:

- **TesserKIT**: `Device`, `System`, `Network` (Wi-Fi, the access point, mDNS), `Firmware` (uploads, GitHub releases, rollback), `Files` (LittleFS: the settings), `Auth`, `NowTp` (the Fluantes nearby, mounted under `/peers`), `Log`; TesserAPI's serial transport on the console's UART (the launcher's USB way).
- **TesserMotion**: `Tmc2209` on a `UartPort`, `Motion` (the control task, 50 Hz).
- **TesserUI**: the interface, and `Views` for the first page.

### 4.1 Pieces of this repository

| Piece | Does | Uses |
|---|---|---|
| `timing` | `plan()`: the LEDC's clock, divider and resolution for a frequency; `dutyCounts()`. From ballet-photophonique. | nothing |
| `Pwm` | the strobe's LEDC: `platform/esp/pwm_esp.cpp` (high-speed timer 0, channel 0); a fake in the tests | ESP-IDF's LEDC |
| `Strobe` | on, frequency, duty, applied to the LEDC | `Pwm`, `timing` |
| `Spinner` | a TesserMotion `Part`: start, ramp, stop, halts; the velocity in rpm | `Tmc2209` |
| `tree.cpp`, `views.cpp` | the `/strobe` and `/spinner` trees, the sections' order and labels, the first page | TesserAPI, TesserUI |

### 4.2 Tasks and locks

- The strobe's setters run under the API lock (the transports' requests); `Strobe::poll()` in TesserKIT's tick, under the same lock.
- The spinner and the driver run in TesserMotion's control task, under Motion's lock (recursive). The tree's spinner settings take that lock. The spinner is added to Motion before the driver, so a velocity it sets reaches the chip in the same tick.

## 5. Behaviour

### 5.1 Strobe

- `on`, `frequency` (1–1000 Hz) and `duty` (0–100 %) apply at once: a duty at the end of the current period, a frequency by retiming the LEDC (a new period starts when its resolution changes).
- The LEDC is planned at 12 bits: from APB (80 MHz) down to about 19 Hz, from the 1 MHz reference tick below it, down to 0.24 Hz. At 60 Hz, the frequency moves in steps of about 0.0007 Hz and the pulse in steps of 4 µs. `applied/` shows the actual frequency and the pulse's length.
- Factory defaults are the old firmware's: on, 60.01 Hz, 3 %.
- *(later)* A phase, set from NowTP's shared clock (§9).

### 5.2 Spinner

- **Running** (`running`, stored): from stopped, the spinner waits for the driver to answer (`absent`), writes stealthChop's initial `pwm_ofs` and `pwm_grad` into PWMCONF (keeping its other bits, as last read), enables the driver, waits `settle` at standstill (`settling`), then ramps to `speed` at `acceleration` (`running`). The velocity is written every tick, so the driver's deadman (1 s) never stops a running sculpture.
- **Speed** changes, sign included, ramp at `acceleration` (0 jumps). The driver's velocity is `rpm / 60 / ratio · stepsPerTurn · microsteps` µsteps/s; it's approximate, as the TMC2209's clock isn't calibrated.
- **Stopping** ramps down to 0, then disables the driver: the sculpture turns freely.
- **Halts:** the driver disabled underneath (Motion's `stopAll()` during an update, `/driver/enabled` written, the chip reset by a power loss) halts the spinner; `running` keeps what was asked, so it is stored as it was, and the sculpture turns again at the next power-up. Writing `running` (true or false) leaves the halt.
- **At power-up** the stored `running` starts the sculpture: a Fluante left on turns when plugged in, as the old firmware always did. A fresh one is off.
- The old firmware's preheat also ran the settle at 30 mA before raising the current to 950 mA; the spinner settles at the run current (§12).
- Factory defaults: 116 rpm (the old firmware's soft start ended there), 60 rpm/s, ratio 0.9, 200 steps; the driver at 256 microsteps, 0.95 A run and 0.5 A hold, stealthChop.

## 6. The tree

Persisted nodes are marked *(p)*. Writes to `/strobe` and `/spinner` need `User`; TesserKIT's Auth leaves everyone Admin until an admin account exists.

```
/strobe                 label "Strobe", ui.icon bulb
  on (p)                bool
  frequency (p)         1–1000 Hz, step 0.001
  duty (p)              0–100 %, step 0.1
  applied/              ui.inline, read-only, watched: frequency (Hz, actual), pulse (µs), bits (advanced)
  gpio                  read-only, advanced
/spinner                label "Rotation", ui.icon motor
  running (p)           bool, watched
  state                 stopped | absent | settling | running | stopping | halted, watched
  rpm                   the speed now, watched
  speed (p)             −240–240 rpm
  acceleration (p)      0–600 rpm/s
  mechanics/ (p)        Admin, collapsed: ratio (0.01–100), stepsPerTurn (1–1000)
  start/ (p)            Admin, collapsed: settle (0–10000 ms), initPwm, pwmOfs, pwmGrad (0–255)
/driver                 TesserMotion's TMC2209 tree (config, chopper, status, registers, map), group "Hardware"
/motion                 the control loop's statistics, group "Hardware"
/peers/<name>           the Fluantes nearby (NowTP); /peers/*/strobe/frequency writes to all
/sys                    TesserKIT
```

## 7. Interface

TesserUI's interface, served by the device, its first page from `Views` (`views.cpp`): Rotation (running, state, speed, the speed now), Strobe (on, applied frequency, frequency, duty), Nearby (each Fluante under `/peers`: its strobe and whether it turns). The rest is TesserUI's tree view.

## 8. Persistence, first flash and updates

- Settings are stored by TesserKIT (LittleFS, `Files`), restored by `Device::begin()`, before the strobe begins and before Motion starts.
- The partition table is TesserKIT's `tesserkit_4mb_files.csv`: two 1.75 MB OTA slots, LittleFS, a core dump. A new image rolls back unless it confirms itself.
- Updates from this repository's GitHub releases (`fluante-{target}.bin`), checked daily, or uploaded. During an update, `motion.stopAll()` halts the spinner.
- The old firmware's settings (Preferences: name, Wi-Fi) aren't carried over: a first flash starts from the factory defaults.

## 9. Requests to other layers

A gap goes to the lowest layer, asked of its agent; no workarounds here. **None sent yet**: each waits for Leo's go-ahead (2026-10-19).

| Request | Layer | For | Status |
|---|---|---|---|
| The shared clock through `tesserkit::NowTp`: which node is the time reference (a setting, or chosen automatically), `networkTimeUs()` / `localTimeUs()`, the offset and round trip in `/sys/nowtp/status`. NowTP has `TimeSync`; TesserKIT never sets a reference, so nothing syncs. | TesserKIT (NowTP) | phase-synchronized strobes (§11, phase 3) | proposed |
| A velocity ramp on `Tmc2209` (acceleration, as `Tmc5160::Ramp` has in hardware): VACTUAL jumps can stall a motor. The spinner ramps itself meanwhile, as an application's part may. | TesserMotion | the spinner (§5.2) | proposed |
| PWMCONF's fields as settings (`pwm_ofs`, `pwm_grad`, `pwm_autoscale`, `pwm_autograd`, `pwm_freq`, `freewheel`, `pwm_lim`, `pwm_reg`), stored; decoded fields for `TCOOLTHRS`, `SGTHRS`, `SG_RESULT`. Today only raw register writes, not stored: the spinner writes `pwm_ofs` and `pwm_grad` at each start. | TesserMotion | the start (§5.2), the old firmware's driver page | proposed |
| A current for a while (the old preheat's 30 mA settle), without changing the stored run current. | TesserMotion | the start (§12) | proposed, if Leo wants the low-current settle |
| Mounting a driver's tree without its controls (`enabled`, `velocity`, `stop`) when an application's part drives it. | TesserMotion | `/driver` beside `/spinner` | proposed |
| A precise LEDC output (ballet-photophonique's `plan()` and `pwm_esp.cpp`, copied here), with a phase *(later)*. | TesserKIT or TesserMotion | the strobe (§5.1); shared with ballet-photophonique | proposed |

## 10. Testing

1. **Host** (`test/`, `cmake -S . -B build && cmake --build build && ctest --test-dir build`, ASan and UBSan): the LEDC plan at 60 Hz, 1 Hz and 1 kHz; what the strobe gives a fake LEDC (begin, on/off, duty counts, retiming only on change, the pulse's length); the spinner on TesserMotion's fake TMC2209: units, booting stopped, the start (PWMCONF written with its other bits kept, the settle, the ramp), outliving the deadman, speed and direction changes on a ramp, stopping on a ramp, halts on `stopAll()`, on `/driver/enabled` and on a reset of the chip, waiting for an absent driver; the tree declared cleanly, validated, stored and restored.
2. **Firmware build** with ESP-IDF 6.1-dev; CI releases with 5.5 (TesserKIT's `firmware.yml`).
3. **On the board** (`test/e2e_board.py PORT`, over the serial API; it never turns the motor). 2026-10-19, DivergentFluante's board (40:22:d8:68:17:e8, ESP32-D0WD-V3 rev 3.0; its old flash in `~/Dev/board-backups/fluante-4022d86817e8-oldfw.bin`, sha1 7e83a023…), erased and flashed over USB, no Wi-Fi saved: boots, access point `fluante-17e8`, 120 KB of heap free (110 KB largest block); the trees answer; the strobe's 48.5 Hz at 2 % applied (48.5000 Hz, a 412 µs pulse) and restored. **The TMC2209 doesn't answer** (`/driver/status/present` false): on USB power only, its logic is likely unpowered (it runs from the motor supply); not tried with the supply on. 8 checks of 9.
4. *(later)*: the strobe on a scope (frequency and pulse against `applied/`), the motor turning, ramping, stopping and turning again after a power cycle; two Fluantes seeing each other under `/peers`.

## 11. Roadmap

| Phase | Content | Acceptance |
|---|---|---|
| 1 *(host-tested)* | Strobe, spinner, trees, first page; Wi-Fi, NowTP, updates | Host tests; the firmware on the board: the strobe measured, the sculpture turning, a setting written to another Fluante under `/peers` |
| 2 | Releases and the launcher on GitHub Pages (`.github/workflows/release.yml`, `pages/`) once the repository is on GitHub | A release installed from `/sys/firmware/github` |
| 3 | Phase-synchronized strobes on NowTP's shared clock (§9) | Two Fluantes' strobes within 100 µs of each other on a scope |

## 12. Open questions

- GPIO 18: what does it drive (the LED driver's enable, a fan)? It is driven high, as before.
- The old preheat settled at 30 mA, then raised the current to 950 mA: needed (stealthChop's automatic tuning, noise, a cold start)? The spinner settles at the run current.
- The GitHub repository's name for releases: `leokeba/fluante-firmware` is assumed.

## 13. Decisions

- **Port onto TesserKIT and TesserMotion** (Leo, 2026-10-19): the latest pins; no step and direction; the strobe not tied to the rotation; phase sync later; Wi-Fi and NowTP only. Gaps to the libraries only with Leo's go-ahead.
- **The strobe on the LEDC, not the MCPWM** (2026-10-19): ballet-photophonique's plan gives finer frequency steps (0.0007 Hz at 60 Hz against the old MCPWM's 0.0036 Hz at 1 µs) from 0.24 Hz up, where the old timer's 16 bits stopped at 15 Hz; the LEDC's `hpoint` can carry a phase later.
- **`running` is stored** (2026-10-19): a Fluante left on turns when plugged in, as the old firmware always did; a fresh one is off, as TesserMotion's drivers boot.
