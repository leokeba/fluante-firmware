#pragma once

// The sculpture's rotation (docs/DESIGN.md section 5.2): a TesserMotion part
// that drives a TMC2209 at a velocity (VACTUAL, the chip's own step
// generator), in turns per minute of the sculpture, through the driver's
// ramp at an acceleration. Starting enables the driver and waits at
// standstill, at a current of its own if set, before turning, as the old
// firmware's "preheat" did. The velocity is renewed every tick, so the
// driver's deadman never stops a running sculpture. The spinner claims the
// driver: its tree's controls are readouts while the spinner drives it.
//
// Added to Motion before the driver, so the velocity it sets reaches the
// chip in the same tick.

#include <stdint.h>

#include "tessermotion/motion.h"
#include "tessermotion/tmc2209.h"

namespace fluante {

class Spinner : public tessermotion::Part {
public:
    enum class State : uint8_t {
        Stopped,   // asked to stop, or never started
        Absent,    // asked to run, the driver doesn't answer
        Settling,  // enabled, at standstill before turning
        Running,   // turning, ramping to the speed
        Stopping,  // ramping down to 0, then disabled
        Halted,    // the driver was disabled underneath (Motion::stopAll(),
                   // a reset of the chip): waits for run()
    };

    struct Settings {
        float speed = 116;        // rpm of the sculpture; negative turns the other way
        float acceleration = 60;  // rpm/s, up and down; 0 jumps
        // Sculpture turns per motor turn: the motor's pulley's teeth over the
        // sculpture's (54/60).
        float ratio = 0.9f;
        uint16_t stepsPerTurn = 200;  // the motor's full steps
        uint32_t settleMs = 1000;     // at standstill after enabling
        float settleCurrent = 0;      // A rms while settling; 0: the run current
        // At boot, after a driver found coasting (its power stage was off
        // through the ESP32's reset while the sculpture turned): the wait
        // before enabling it.
        uint32_t coastMs = 10000;
    };

    explicit Spinner(tessermotion::Tmc2209& driver) : driver_(driver) {}

    // Under lock() once added to Motion (the tree does).
    Settings settings;

    // From any task: these take Motion's lock.
    // Run or stop, as asked: the tree's `running`. Running again after a halt.
    void run(bool on);
    bool running();  // as asked
    State state();
    float rpm();  // the sculpture's speed now, as the driver ramps it

    // The driver's velocity for `rpm` of the sculpture, in µsteps/s, at the
    // microsteps in effect (also rpm/s to µsteps/s²): the driver keeps the
    // speed through a change. Under the lock.
    float microstepsPerSecond(float sculptureRpm) const;

    // Part
    void begin() override;
    void update(uint64_t nowUs) override;
    // Motion::stopAll(): disabled at once, halted; `running` keeps what was
    // asked, so that it is stored as it was.
    void stop() override;

private:
    void halt();

    tessermotion::Tmc2209& driver_;
    bool want_ = false;
    State state_ = State::Stopped;
    uint64_t sinceUs_ = 0;  // entered Settling
    bool booted_ = false;   // the first update, which sees what the driver found
    uint64_t coastUntilUs_ = 0;
};

}  // namespace fluante
