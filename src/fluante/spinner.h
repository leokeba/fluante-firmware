#pragma once

// The sculpture's rotation (docs/DESIGN.md section 5.2): a TesserMotion part
// that drives a TMC2209 at a velocity (VACTUAL, the chip's own step
// generator), in turns per minute of the sculpture, ramped at an
// acceleration. Starting writes stealthChop's initial PWM values, enables the
// driver and waits at standstill before turning, as the old firmware's
// "preheat" did. The velocity is written every tick, so the driver's deadman
// never stops a running sculpture.
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
                   // its tree, a reset of the chip): waits for run(true)
    };

    struct Settings {
        float speed = 116;        // rpm of the sculpture; negative turns the other way
        float acceleration = 60;  // rpm/s, up and down
        // Sculpture turns per motor turn: the motor's pulley's teeth over the
        // sculpture's (54/60).
        float ratio = 0.9f;
        uint16_t stepsPerTurn = 200;  // the motor's full steps
        uint32_t settleMs = 1000;     // at standstill after enabling
        // stealthChop's initial values (PWMCONF), written before enabling;
        // pwm_autoscale adapts them while turning.
        bool initPwm = true;
        uint8_t pwmOfs = 255;
        uint8_t pwmGrad = 14;
    };

    explicit Spinner(tessermotion::Tmc2209& driver) : driver_(driver) {}

    // Under lock() once added to Motion (the tree does).
    Settings settings;

    // From any task: these take Motion's lock.
    // Run or stop, as asked: the tree's `running`. Running again after a halt.
    void run(bool on);
    bool running();  // as asked
    State state();
    float rpm();  // the sculpture's speed now, as ramped

    // The driver's velocity for `rpm` of the sculpture, in µsteps/s, at the
    // driver's microsteps. Under the lock.
    float microstepsPerSecond(float sculptureRpm) const;

    // Part
    void update(uint64_t nowUs) override;
    // Motion::stopAll(): disabled at once, halted; `running` keeps what was
    // asked, so that it is stored as it was.
    void stop() override;

private:
    void halt();

    tessermotion::Tmc2209& driver_;
    bool want_ = false;
    State state_ = State::Stopped;
    float rpm_ = 0;
    uint64_t sinceUs_ = 0;  // entered Settling
    uint64_t lastUs_ = 0;   // the last update while turning
};

}  // namespace fluante
