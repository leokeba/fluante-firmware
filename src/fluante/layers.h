#pragma once

// The layers (docs/DESIGN.md section 5.5): several strobes on the one LED
// driver, each its own frequency, phase and duty, the LED lit while any is
// (their union), on TesserKIT's PulseOutput. Their pulses begin at whole
// periods of NowTP's network time plus their phase, so Fluantes asked the
// same layers flash together. While on, the layers take the pin from the
// strobe (route()). The tree's setters write the list; poll() hands it to
// the output, under the API lock.

#include <stdint.h>

#include <functional>
#include <vector>

#include "fluante/network_clock.h"
#include "fluante/strobe.h"
#include "tesserkit/pulses.h"

namespace fluante {

struct Layer {
    bool on = true;
    double frequency = Strobe::kFrequency;  // Hz
    float phase = 0;                        // degrees of the period
    float duty = Strobe::kDuty;             // percent of the period
};

class Layers {
public:
    Layers(tesserkit::Pulses& driver, int pin) : driver_(driver), out_(driver, pin) { list.emplace_back(); }

    static constexpr size_t kMax = tesserkit::pulses::kMaxTrains;
    static constexpr double kLowestHz = Strobe::kLowestHz;
    static constexpr double kHighestHz = Strobe::kHighestHz;

    // The layers, as the tree writes them: one at the strobe's defaults to
    // begin with. Past kMax, ignored.
    std::vector<Layer> list;

    // On, the layers drive the LED instead of the strobe. Off by default.
    bool on() const { return on_; }
    void setOn(bool on);

    // Takes the pin; false (and not tried again until on changes) when the
    // output refuses it.
    bool begin();
    void end();
    bool begun() const { return out_.begun(); }
    bool refused() const { return refused_; }

    // Their lock to network time.
    NetworkClock& clock() { return clock_; }
    const NetworkClock& clock() const { return clock_; }
    // Network time minus local time, false while there is none.
    using NetworkOffset = std::function<bool(int64_t& offsetUs)>;
    // Every tick, under the API lock: the list to the output, and once a
    // second the clock's sample.
    void poll(uint32_t nowMs, const NetworkOffset& offset);

    // A layer as the output takes it.
    static tesserkit::pulses::Train train(const Layer& l);
    tesserkit::pulses::Stats stats() const { return out_.stats(); }
    int pin() const { return out_.pin(); }

private:
    tesserkit::Pulses& driver_;
    tesserkit::PulseOutput out_;
    NetworkClock clock_;
    bool on_ = false;
    bool refused_ = false;
};

// Who drives the LED, every tick once the strobe's capacitor has charged
// (section 5.3): the layers while on (the strobe given back the pin if they
// can't take it), the strobe otherwise.
void route(Strobe& strobe, Layers& layers);

}  // namespace fluante
