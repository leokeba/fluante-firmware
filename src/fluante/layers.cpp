// The layers (docs/DESIGN.md section 5.5).
#include "fluante/layers.h"

namespace fluante {

void Layers::setOn(bool on) {
    if (on == on_) return;
    on_ = on;
    refused_ = false;
}

bool Layers::begin() {
    if (out_.begun()) return true;
    if (refused_) return false;
    refused_ = !out_.begin();
    return !refused_;
}

void Layers::end() { out_.end(); }

tesserkit::pulses::Train Layers::train(const Layer& l) {
    tesserkit::pulses::Train t;
    if (!(l.frequency > 0)) return t;
    t.on = l.on && l.duty > 0;
    t.periodUs = 1e6 / l.frequency;
    t.phaseUs = t.periodUs * l.phase / 360.0;
    t.pulseUs = t.periodUs * l.duty / 100.0;
    return t;
}

void Layers::poll(uint32_t nowMs, const NetworkOffset& offset) {
    for (size_t i = 0; i < kMax; i++) {
        out_.setTrain(static_cast<int>(i), i < list.size() ? train(list[i]) : tesserkit::pulses::Train());
    }
    if (clock_.due(nowMs) && clock_.enabled()) {
        int64_t offsetUs = 0;
        if (offset && offset(offsetUs)) {
            clock_.update(driver_.nowUs(), offsetUs);
        } else {
            clock_.wait();
        }
    }
    out_.setTimebase(clock_.timebase());
    out_.poll();
}

void route(Strobe& strobe, Layers& layers) {
    if (layers.on() && !layers.refused()) {
        if (layers.begun()) return;
        strobe.end();
        if (layers.begin()) return;
    } else {
        layers.end();
    }
    strobe.begin();
}

}  // namespace fluante
