// The strobe's lock to network time (docs/DESIGN.md section 5.4).
#include "fluante/strobe.h"

namespace fluante {

void Strobe::sync(uint32_t nowMs, const NetworkOffset& offset) {
    if (!out_.begun()) {
        if (lock_.due(nowMs)) lock_.wait();
        return;
    }
    int32_t step = lock_.step();
    if (step != out_.trim()) out_.setTrim(step);
    if (!lock_.due(nowMs) || !lock_.enabled()) return;
    int64_t offsetUs = 0;
    PhaseLock::Sample s;
    if (!offset || !offset(offsetUs) || !out_.sample(s.count, s.localUs)) {
        lock_.wait();
        return;
    }
    s.offsetUs = offsetUs;
    const tesserkit::pwm::Timing& planned = out_.planned();
    PhaseLock::Timer t;
    t.periodUs = planned.frequency > 0 ? 1e6 / planned.frequency : 0;
    t.divider = planned.divider;
    t.stepUs = out_.timing().stepUs();
    PhaseLock::Action a = lock_.update(t, s);
    // One that fails (a settings save in the way): the next samples see the
    // period where it was, and start over.
    if (a.restart) out_.restartAt(a.restartAtUs);
}

}  // namespace fluante
