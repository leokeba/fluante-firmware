#include "fluante/views.h"

namespace fluante {

void addViews(tesserui::Views& views) {
    auto& page = views.page("overview", "Fluante").icon("home");
    auto& rotation = page.card("Rotation");
    rotation.row().node("/spinner/running").node("/spinner/state");
    rotation.row().node("/spinner/speed").widget("readout", "/spinner/rpm", "Now");
    auto& strobe = page.card("Strobe");
    strobe.row().node("/strobe/on").widget("readout", "/strobe/applied/frequency", "Applied");
    strobe.node("/strobe/frequency").node("/strobe/duty");
    auto& layers = page.card("Layers");
    layers.row().node("/layers/on").node("/layers/sync/state");
    layers.node("/layers/list");
    // The other Fluantes nearby, over NowTP ("$" stands for each one).
    page.card("Nearby").each("/peers", "$/strobe").each("/peers", "$/spinner/running");
}

}  // namespace fluante
