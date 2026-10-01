//===- app/debug_showcase.hpp - Samples of debug categories -----*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares drawDebugShowcase(), which draws one sample of every
/// debug category above the arena (F6 or --showcase).
///
/// It is needed in phase 0, while not every category has real data yet: it is
/// how colors, the category toggles and primitive rendering are checked. It
/// will be removed once every category is drawn by a real module.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace fighter::app {

void drawDebugShowcase();

} // namespace fighter::app
