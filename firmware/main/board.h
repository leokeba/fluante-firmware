#pragma once

// Fluante's control board (docs/DESIGN.md section 3): a classic ESP32
// (esp32doit-devkit-v1), the old firmware's latest pins ("new control board",
// leokeba/fluante_firmware caca2e2, src/pins.h without OLD_PINS).

namespace board {

// TMC2209 on its single-wire UART: UART 2 at 115200 baud, address 0.
constexpr int kTmcRx = 26, kTmcTx = 25;
constexpr int kTmcUart = 2;
constexpr int kTmcEnable = 13;  // EN, active low
// MS1 and MS2 set the UART address: both low, 0.
constexpr int kTmcMs1 = 14, kTmcMs2 = 27;
// STEP and DIR are wired but unused: the chip turns the motor by itself
// (VACTUAL). Held low.
constexpr int kTmcStep = 33, kTmcDir = 32;

// The strobe's LED driver input.
constexpr int kStrobe = 19;

// Driven high at boot by the old firmware since the new board (its purpose
// isn't recorded: docs/DESIGN.md section 12).
constexpr int kAux = 18;

}  // namespace board
