#pragma once
/** Grab handles for detaching a connected cable — see design/cable-handle.md

DELIBERATELY PRIVATE. Lives in src/, not include/.

A pill-shaped stretch of cable appears just clear of a jack after a short dwell. Clicking
it lifts that end off the port and leaves you carrying the cable, exactly as though you had
started pulling it from that terminal.
*/
#include <math.hpp>
#include <nanovg.h>


namespace rack {

namespace app {
struct CableWidget;
struct PortWidget;
}

namespace app {


/** Rack's cable slump control point. Declared here so the handle's curve sampling uses the
SAME formula the cable is drawn with, rather than a copy that can drift out of step. */
math::Vec cableSlumpPos(math::Vec pos1, math::Vec pos2);

/** Called from RackWidget::onHover with the pointer in rack coordinates. Finds the nearest
end-stretch of any cable and runs the reveal dwell. */
void cableHandleHover(math::Vec rackPos);

/** Called from RackWidget::onButton BEFORE dispatching to children, so the handle can
claim the click ahead of the module panel it sits over. Returns true if it took the click.

Must run before children: the pill sits over a panel, and ModuleWidget consumes clicks.
*/
bool cableHandleClick(math::Vec rackPos);

/** Draws the pill if this cable is the one currently showing a handle. Called from
CableWidget::drawLayer so it sits in the cable z-order. */
void cableHandleDraw(app::CableWidget* cw, NVGcontext* vg, float thickness);

/** Forgets the shown handle, e.g. when the cable it belonged to is going away. */
void cableHandleClear();


} // namespace app
} // namespace rack
