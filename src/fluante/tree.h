#pragma once

// The application's trees (docs/DESIGN.md section 6), found by
// argument-dependent lookup: api.mount("strobe", strobe).

#include "fluante/spinner.h"
#include "fluante/strobe.h"
#include "tesser/api.h"
#include "tesser/node.h"
#include "tessermotion/motion.h"
#include "tessermotion/tmc2209.h"
#include "tessermotion/trees.h"

namespace fluante {

void describe(tesser::Object& o, Strobe& strobe);
void describe(tesser::Object& o, Spinner& spinner);

// The device's sections, in menu order, with their labels, icons and menu
// groups: /strobe, /spinner, then the motor's driver (/driver) and the control
// loop's statistics (/motion) under "Hardware". Before Device::begin().
void mountTree(tesser::Api& api, Strobe& strobe, Spinner& spinner, tessermotion::Tmc2209& driver,
               tessermotion::Motion& motion);

}  // namespace fluante
