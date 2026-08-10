#pragma once
/** Option-held navigation mode — see design/zoom-pan.md

DELIBERATELY PRIVATE. Lives in src/, not include/, so none of it becomes plugin API
surface.

Exists only so FramebufferWidget can ask whether navigation is in progress without
anything crossing into the SDK headers. RackScrollWidget owns the mode and sets the flag;
FramebufferWidget reads it to decide whether to throw away its cached texture.
*/

namespace rack {
namespace app {


/** True while Option-held navigation is active.

While true, framebuffers must NOT invalidate on scale or subpixel changes — letting the
existing textures stretch is what makes zooming smooth, and it also stops panel
brightness reduction re-filtering every panel every frame.
*/
bool navModeActive();

void navModeSetActive(bool active);


} // namespace app
} // namespace rack
