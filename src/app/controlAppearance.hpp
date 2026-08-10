#pragma once
/** Drawn knobs and jacks — see design/control-appearance.md

DELIBERATELY PRIVATE. Lives in src/, not include/, so none of it becomes plugin API
surface. Do not reference it from any header under include/.

Ported from Wcoast: panel/primitives.js for the drawing, host/panel-loader.js for the
colour code and direction ring.
*/
#include <widget/Widget.hpp>


namespace rack {

namespace app {
struct ModuleWidget;
struct PortWidget;
}

namespace appearance {


/** Draws replacement graphics over every knob and port descended from this module widget.

Call from the body of ModuleWidget::draw, after Widget::draw has painted the children, so
the drawn controls land on top of the plugin's own artwork.

Returns immediately when the feature is disabled.
*/
void drawControls(app::ModuleWidget* mw, const widget::Widget::DrawArgs& args);


/** Re-reads every per-module definition file from disk.

Iterating on how these look is the whole job at first, and restarting Rack for each
attempt would make that unbearable.
*/
void reloadDefinitions();


/** The colour this port carries, resolved exactly as the drawn jack resolves it: the
module's definition file if it names one, otherwise guessed from the port's name.

Shared with cables so a cable coloured by its destination cannot disagree with the jack it
plugs into. */
NVGcolor portColor(app::PortWidget* port);

/** Dash length for the flow animation, in cable widths, keyed by the port's signal family.
Ported from Wcoast's FLOW_DASH: audio 1.6, control and pitch 3.4, trigger 5.6 — so gate
signals get the longest dashes and audio the shortest. */
float portFlowDashLength(app::PortWidget* port);


} // namespace appearance
} // namespace rack
