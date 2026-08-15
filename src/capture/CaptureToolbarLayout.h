#pragma once

#include "capture/CaptureState.h"

namespace easy::capture {

/// Rebuilds the capture/record toolbar from one shared layout algorithm used by
/// both painting and hit testing. Rows wrap when high-DPI controls would exceed
/// the physical work surface.
void rebuildCaptureToolbar(CaptureState& state, const D2D1_RECT_F& selectionRect,
                           D2D1_SIZE_F surfaceSize);

}  // namespace easy::capture
