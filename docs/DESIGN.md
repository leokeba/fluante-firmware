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

- **Phase-synchronized strobes** across Fluantes (the old firmware's clock sync, partly built there): a goal, not in phase 1 (Leo, 2026-10-19). §9 lists what it needs. NowTP's network time, measured by nowtp-9f on two classic ESP32s 1 m apart (2026-10-19, NowTP 83b80c1): median −1 µs, p5/p95 −46/+92 µs, worst 346 µs, single rounds off by ~120 µs until the next. Enough: at 116 rpm, 346 µs turns the sculpture 0.24°, inside a 500 µs flash.
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
| GPIO 18 | low at power-up, high 2 s after the first tick (§5.3) | A MOSFET bypassing the resistor that limits the strobe capacitor's inrush current (Leo's recollection, 2026-10-19). |

## 4. Architecture

Only what is specific to a Fluante is here; the rest comes from the libraries, pinned in `deps.lock`:

- **TesserKIT**: `PwmOutput` (the strobe's LEDC), `Device`, `System`, `Network` (Wi-Fi, the access point, mDNS), `Firmware` (uploads, GitHub releases, rollback), `Files` (LittleFS: the settings), `Auth`, `NowTp` (the Fluantes nearby, mounted under `/peers`), `Log`; TesserAPI's serial transport on the console's UART (the launcher's USB way).
- **TesserMotion**: `Tmc2209` on a `UartPort`, `Motion` (the control task, 50 Hz).
- **TesserUI**: the interface, and `Views` for the first page.

### 4.1 Pieces of this repository

| Piece | Does | Uses |
|---|---|---|
| `Strobe` | on, frequency, duty, with Fluante's defaults and resolution | TesserKIT's `PwmOutput` on `espPwm()` (the LEDC's high-speed timer 0 and channel 0, taken first); a fake `tesserkit::Pwm` in the tests |
| `Inrush` | when to bypass the strobe capacitor's inrush resistor (§5.3) | nothing |
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

- **Running** (`running`, stored): from stopped, the spinner waits for the driver to answer (`absent`), enables it, at `settleCurrent` if set (the driver's current override, not stored), waits `settle` at standstill (`settling`), then turns at `speed` through the driver's ramp (`running`). The velocity is renewed every tick, so the driver's deadman (1 s) never stops a running sculpture.
- **The driver's ramp** follows `acceleration` (rpm/s), written into `/driver/ramp/acceleration` in µsteps/s² every tick: that node shows the spinner's value. Speed changes, sign included, ramp through it (0 jumps). The driver's velocity is `rpm / 60 / ratio · stepsPerTurn · microsteps` µsteps/s; `rpm` reads the driver's ramped velocity back. Approximate: the TMC2209's clock isn't calibrated.
- **stealthChop's start values** are the driver's stored PWM settings (`/driver/pwm/`), whose factory defaults here are the old preheat's `ofs` 255 and `grad` 14.
- **The claim:** the spinner claims the driver at begin, so `/driver/enabled`, `velocity` and `stop` are readouts, refused with `busy`.
- **Stopping** asks the driver to hold, which ramps down to 0, then disables it: the sculpture turns freely.
- **Halts:** the driver disabled underneath (Motion's `stopAll()` during an update, the chip reset by a power loss) halts the spinner; `running` keeps what was asked, so it is stored as it was, and the sculpture turns again at the next power-up. Writing `running` (true or false) leaves the halt.
- **At power-up** the stored `running` starts the sculpture: a Fluante left on turns when plugged in, as the old firmware always did. A fresh one is off.
- The old firmware's preheat settled at 30 mA before raising the current to 950 mA: `settleCurrent` 0.03 does the same; its default, 0, settles at the run current (§12).
- Factory defaults: 116 rpm (the old firmware's soft start ended there), 60 rpm/s, ratio 0.9, 200 steps, a 1 s settle at the run current; the driver at 256 microsteps, 0.95 A run and 0.5 A hold, stealthChop, PWM `ofs` 255 and `grad` 14.

### 5.3 Inrush

At power-up the strobe's capacitor charges through a resistor that limits the inrush current; a MOSFET on GPIO 18 then bypasses it. The firmware holds GPIO 18 low from `app_main`, and drives it high 2 s after TesserKIT's first tick (`Inrush`, host-tested), about when the old firmware did: at the end of its `setup()`, after Wi-Fi and ESP-NOW's one-second wait. The strobe flashes meanwhile, as it did.

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
  start/ (p)            Admin, collapsed: settle (0–10000 ms), settleCurrent (0–2 A, 0: the run current)
/driver                 TesserMotion's TMC2209 tree (config, chopper, pwm, ramp, status, registers, map), group
                        "Hardware"; claimed by the spinner: enabled, velocity and stop are readouts
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

A gap goes to the lowest layer, asked of its agent; no workarounds here. Sent with Leo's go-ahead on 2026-10-19, to tesserkit-45 and tessermotion-aa.

| Request | Layer | For | Status |
|---|---|---|---|
| The shared clock through `tesserkit::NowTp`: which node is the time reference (a setting, or chosen automatically), `networkTimeUs()` / `localTimeUs()`, the offset and round trip in `/sys/nowtp/status`. NowTP has `TimeSync`; TesserKIT never sets a reference, so nothing syncs. | TesserKIT (NowTP) | phase-synchronized strobes (§11, phase 3) | done, TesserKIT 6bd8608 on NowTP d2b515e (pinned): `auto` nodes elect a reference with no setting, every change applies at once. On the board (2026-10-19): NowTP starts once Wi-Fi is up (channel 4 here), synced about 15 s after boot to a nearby board elected reference. `NowTp::networkTimeUs()`, `localTimeUs()`, `timeSynced()`; `/sys/nowtp/time/` (reference `auto`, `this` or a MAC; interval; status). `auto` needs a NowTP change (asked of nowtp-9f). 100 µs unverified until measured on two boards. Crystals differ by ±20 ppm: a locked strobe needs its period trimmed continuously |
| A velocity ramp on `Tmc2209` (acceleration, as `Tmc5160::Ramp` has in hardware): VACTUAL jumps can stall a motor. The spinner ramps itself meanwhile, as an application's part may. | TesserMotion | the spinner (§5.2) | done, TesserMotion 9d4cc2e (pinned): `Tmc2209::ramp.acceleration`, `actualVelocity()`; the spinner's own ramp dropped |
| PWMCONF's fields as settings (`pwm_ofs`, `pwm_grad`, `pwm_autoscale`, `pwm_autograd`, `pwm_freq`, `freewheel`, `pwm_lim`, `pwm_reg`), stored; decoded fields for `TCOOLTHRS`, `SGTHRS`, `SG_RESULT`. Today only raw register writes, not stored: the spinner writes `pwm_ofs` and `pwm_grad` at each start. | TesserMotion | the start (§5.2), the old firmware's driver page | done, 9d4cc2e: `driver.pwm` stored at `/driver/pwm/`, `status/stallGuard`, decoded TCOOLTHRS, SGTHRS, SG_RESULT; the spinner's PWMCONF write dropped |
| A current for a while (the old preheat's 30 mA settle), without changing the stored run current. | TesserMotion | the start (§12) | done, 9d4cc2e: `overrideCurrent()`, used by `settleCurrent` |
| Mounting a driver's tree without its controls (`enabled`, `velocity`, `stop`) when an application's part drives it. | TesserMotion | `/driver` beside `/spinner` | done differently, 9d4cc2e: `claim(owner)` turns the controls into readouts refusing writes with `busy` |
| **`UartPort::read()` waits too little**: 10 ms is 1 tick at 100 Hz, which ends at the next tick, 0 to 10 ms away; reads straddling a tick time out, so `connect()`'s ~20 reads in a row rarely complete. Measured on the board: 59 of 300 back-to-back IOIN reads fail with 1 tick, none with 2 or 3. Fix: round up and add a tick | TesserMotion | the TMC2209 found at all (§10) | done, TesserMotion e257d60 (pinned): waits round up and add a tick; on the board, found at every boot |
| **Changing microsteps while turning overspeeds the motor**: the ramp's velocity stays in µsteps/s of the old resolution, so 256 → 16 commands 16× the speed (≈ 945 rpm for 60) and 256 → 1 256×, ramping down at an acceleration converted with the new resolution (15 s, or minutes). Leo heard the stall on the board (2026-10-19); reproduced on the host. Asked: keep the physical speed through a change (or refuse it while turning); microsteps as a choice of powers of two (1 = full steps … 256) instead of an integer range | TesserMotion | `/driver/config/microsteps` | sent |
| **A chip still turning at boot is stopped abruptly**: when only the ESP32 resets (its USB port opened, an update), the TMC2209 keeps its supply and turns on; `begin()` drives EN high and configures it disabled, then the spinner enables it at standstill while the sculpture coasts: a horrible noise before the ramp (Leo, 2026-10-19). Asked: resume a chip found turning (kept power, `stst` 0) at its measured velocity, and say so, so the spinner skips its settle | TesserMotion | every reset of the ESP32 (§5.2) | sent |
| Diagnostics for an absent chip: failed connects counted, the last reason (no reply, echo only, bad CRC, wrong version) | TesserMotion | `/driver/status` | done, 9d4cc2e: every failed transfer counted, `status/lastError` |
| A precise LEDC output (ballet-photophonique's `plan()` and `pwm_esp.cpp`, copied here before), with a phase *(later)*. | TesserKIT | the strobe (§5.1); shared with ballet-photophonique | done, TesserKIT 2536928 (pinned at e08bf49; `tesserkit/pwm.h`: `pwm::plan(chip, …)`, `Pwm`, `espPwm()`, `PwmOutput`); the strobe is on it, the copy removed. As asked: `plan()` per chip, a `Pwm` platform interface with a fake, an output class (frequency, duty, resolution, on; applied timing, counts, pulse), no tree; LEDC timers from the bottom up; the phase *(later)* |

## 10. Testing

1. **Host** (`test/`, `cmake -S . -B build && cmake --build build && ctest --test-dir build`, ASan and UBSan): the LEDC plan at 60 Hz, 1 Hz and 1 kHz; what the strobe gives a fake LEDC (begin, on/off, duty counts, retiming only on change, the pulse's length); the spinner on TesserMotion's fake TMC2209: units, booting stopped, the start (PWMCONF written with its other bits kept, the settle, the ramp), outliving the deadman, speed and direction changes on a ramp, stopping on a ramp, halts on `stopAll()`, on `/driver/enabled` and on a reset of the chip, waiting for an absent driver; the tree declared cleanly, validated, stored and restored.
2. **Firmware build** with ESP-IDF 6.1-dev; CI releases with 5.5 (TesserKIT's `firmware.yml`).
3. **On the board** (`test/e2e_board.py PORT`, over the serial API; it never turns the motor). 2026-10-19, DivergentFluante's board (40:22:d8:68:17:e8, ESP32-D0WD-V3 rev 3.0; its old flash in `~/Dev/board-backups/fluante-4022d86817e8-oldfw.bin`, sha1 7e83a023…), erased and flashed over USB, no Wi-Fi saved: boots, access point `fluante-17e8`, 120 KB of heap free (110 KB largest block); the trees answer; the strobe's 48.5 Hz at 2 % applied (48.5000 Hz, a 412 µs pulse) and restored. The TMC2209 answers only now and then (`/driver/status/present`): 11 checks of 11 once, absent on the next boots with `errors` rising every 2 s. A raw probe (reads only) found the chip at address 0 on RX 26 / TX 25 with the echo (IOIN 0x21000041, version 0x21; GCONF 0x000001c0, CHOPCONF 0x100100c0, PWMCONF 0xc10d0024, DRV_STATUS 0xc01f0000 as read through the driver once), and the cause in TesserMotion's `UartPort` (§9). Opening the CP2102 port resets the board (`uptime` 0). Firmware for the board is built against clean clones of `deps.lock`'s commits (`*_PATH`), since the siblings' working trees may be mid-edit.
4. **Turning** (`test/turn_board.py PORT --speed 30 --current 0.5`, with Leo's go-ahead, 2026-10-19), on TesserMotion e257d60 (the `UartPort` fix): the driver found at every boot (three resets), `errors` 0. At 30 rpm and 0.5 A: settle, ramp, 30 rpm (TSTEP 421 clock cycles per µstep: 28.5 k µsteps/s, 28.4 k expected), stealthChop, a ramp down and the driver off; stealthChop's tuning took `pwm_ofs_auto` from 36 to 244 during the settle. At the old firmware's 116 rpm and 0.95 A: TSTEP 108 (109 expected), `cs_actual` 16, SG_RESULT about 380, no flags, but `pwm_scale_sum` at its 255 maximum and `pwm_ofs_auto` at 255: stealthChop runs out of voltage at that speed (the supply against the motor's back-EMF), so torque has little margin. With and without the start's PWM values the registers look alike, but the chip kept the first run's PWMCONF, so that comparison says nothing yet; whether the old preheat matters is for the ear and the eye (§12). Whether the sculpture turned as asked was not seen from here.
5. **On TesserMotion 9d4cc2e** (2026-10-19): the spinner on the driver's ramp, claim and PWM settings (PWMCONF read back `pwm_ofs` 255, `pwm_grad` 14 from the settings). At 116 rpm and 0.95 A, with `settleCurrent` 0 and 0.03: the same ramp (TSTEP 110–111 at speed), stealthChop saturated as before (`pwm_scale_sum` 255); during a 0.03 A settle `pwm_scale_auto` stays at 0. Which start is quieter or smoother is for Leo's ears. While turning at 30 rpm: the control loop at 20 ms, its longest tick 4–11 ms, no overrun; replies in 0.01–0.27 s, but about one 3 KB `/driver/registers` reply in ten lost, once the board had a saved Wi-Fi network it kept retrying (warnings on the console's UART break long replies).
6. **The old preheat** (2026-10-19, TesserKIT e08bf49, TesserMotion 9d4cc2e): three starts each at 116 rpm and 0.95 A with `settleCurrent` 0 and 0.03, sampled every ~0.1 s. While settling, the current scale fell to the hold current (8–9) at 0, and sat at 3–10 at 0.03. Accelerating below 60 rpm, StallGuard's minimum was 6–20 at 0 and 14–30 at 0.03, its mean 95–103 and 96–111; above 60 rpm 210–226 / 346–352 for both; no errors, and the same stealthChop regulation after 4 s. No difference the driver can see, within run-to-run scatter. Noise and a jerk at the start are for the ear and the eye. Also seen: `pwm_grad_auto` climbing from start to start (65 to 89), stealthChop still saturated at that speed.
7. **A reset of the ESP32 alone** (opening its USB port, flashing): the motor, still turning on the TMC2209's own supply, is stopped by the driver's boot and held at standstill while the sculpture coasts: a horrible noise (Leo, 2026-10-19). Until TesserMotion resumes a chip found turning (§9), stop the sculpture before resetting or flashing the ESP32.
8. *(later)*: the strobe on a scope (frequency and pulse against `applied/`), the motor turning, ramping, stopping and turning again after a power cycle; two Fluantes seeing each other under `/peers`.

## 11. Roadmap

| Phase | Content | Acceptance |
|---|---|---|
| 1 *(host-tested)* | Strobe, spinner, trees, first page; Wi-Fi, NowTP, updates | Host tests; the firmware on the board: the strobe measured, the sculpture turning, a setting written to another Fluante under `/peers` |
| 2 | Releases and the launcher on GitHub Pages (`.github/workflows/release.yml`, `pages/`) once the repository is on GitHub | A release installed from `/sys/firmware/github` |
| 3 | Phase-synchronized strobes on NowTP's shared clock (§9): the strobe's period steered slowly (the LEDC's fractional divider, no timer reset) toward flashes at whole periods of network time, so single bad rounds are filtered rather than followed | Two Fluantes' strobes within 100 µs of each other on a scope, median; a GPIO edge between two units (nowtp-9f offers the firmware) |

## 12. Open questions

- GPIO 18 (§5.3): Leo thinks it bypasses the strobe capacitor's inrush resistor, not sure; 2 s is a guess from the old firmware's timing. To check on the board (the supply at power-up with and without the delay).
- The old preheat settled at 30 mA, then raised the current to 950 mA: the driver sees no difference (§10.6), so `settleCurrent` stays 0 unless Leo hears or sees one.
- Microsteps: with VACTUAL the chip makes the steps itself, so 256 costs nothing and is the smoothest; fewer (down to 1, full steps) only add noise and vibration. Leave it at 256 until TesserMotion keeps the speed through a change (§9).
- At 116 rpm and 0.95 A stealthChop saturates (`pwm_scale_sum` 255): the motor supply's voltage, a lower current, or spreadCycle above some speed (`stealthChopMax`)?
- The GitHub repository's name for releases: `leokeba/fluante-firmware` is assumed.

## 13. Decisions

- **Port onto TesserKIT and TesserMotion** (Leo, 2026-10-19): the latest pins; no step and direction; the strobe not tied to the rotation; phase sync later; Wi-Fi and NowTP only. Gaps to the libraries only with Leo's go-ahead.
- **The strobe on the LEDC, not the MCPWM** (2026-10-19): ballet-photophonique's plan gives finer frequency steps (0.0007 Hz at 60 Hz against the old MCPWM's 0.0036 Hz at 1 µs) from 0.24 Hz up, where the old timer's 16 bits stopped at 15 Hz; the LEDC's `hpoint` can carry a phase later.
- **`running` is stored** (2026-10-19): a Fluante left on turns when plugged in, as the old firmware always did; a fresh one is off, as TesserMotion's drivers boot.
