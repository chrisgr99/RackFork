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


} // namespace appearance
} // namespace rack
