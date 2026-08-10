#pragma once
/** Render-path wiring for panel brightness reduction — see design/panel-dimming.md

DELIBERATELY PRIVATE, like panelFilter.hpp. Lives in src/, not include/, so none of it
becomes plugin API surface. Do not reference it from any header under include/.

The pure algorithm is in panelFilter.hpp, which depends on nothing from Rack. This file
is the part that knows about widgets, OpenGL and settings.
*/

namespace rack {

namespace widget {
struct FramebufferWidget;
}

namespace window {


/** Called after a FramebufferWidget has rendered. If the framebuffer belongs to a module
panel and the feature is enabled, reads it back, filters it, and caches the result.

Cheap and safe to call for every framebuffer: it returns immediately for anything that is
not a panel.
*/
void panelDimProcess(widget::FramebufferWidget* fbw);

/** Returns the filtered image handle to draw instead of the raw framebuffer, or -1 if
this framebuffer has no filtered version and should be drawn as-is. */
int panelDimImage(widget::FramebufferWidget* fbw);

/** Drops any cached image for this widget. Called from the framebuffer's destructor and
when the GL context goes away, since the cache holds GL resources. */
void panelDimInvalidate(widget::FramebufferWidget* fbw);

/** Drops every cached image. Call when a global parameter changes. */
void panelDimInvalidateAll();

/** Drops every cached image AND asks every framebuffer in the scene to re-render.

Both halves are needed. Dropping the cache alone changes nothing on screen, because the
filter only runs when a framebuffer is genuinely re-rendered, and a panel's framebuffer
is not dirty just because a setting moved.
*/
void panelDimRefresh();


} // namespace window
} // namespace rack
