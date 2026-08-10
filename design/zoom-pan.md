# Modifier-held navigation: Option to pan and zoom

Modification 2. Hold Option and the rack becomes navigable without pressing a button:
moving the pointer pans, the scroll wheel zooms, and holding the pointer near an edge
keeps travelling that way. Release and everything is exactly as it was.

The problem it solves is specific. Rack's normal way to see more of a patch is to drag it
with a button held. Chris works with screen magnification that follows the pointer, so a
button-held drag fights the magnifier: the pointer has to travel to move the view, and
the magnified viewport chases the pointer while he is trying to read what it passes over.


## Provenance

Ported from Wcoast, `host/rack.js` — `_navMove`, `_startNavEdge`, `_armPanWheel`, and the
`_optDown` state. Read those before changing behaviour here. The tuned constants come
from that file and should not be casually re-tuned:

| Constant | Wcoast value | Meaning |
| --- | --- | --- |
| `VIEW_MOTION_GAIN` | 3 | view pixels travelled per pointer pixel moved |
| `NAV_EDGE_MARGIN` | 24 | pointer within this many px of an edge auto-scrolls |
| `NAV_EDGE_RATE` | 14 px/frame | steady edge-scroll speed |

The edge rate is converted here from per-frame to per-second, because Rack limits itself
to 30 Hz on macOS by default while a browser runs at 60. 14 px/frame at 60 Hz is 840
px/s; using the raw per-frame value would travel at half Chris's expected speed.


## The direction convention, stated once

**The view chases the pointer.** Move the pointer toward what you want to see. The
content therefore slides the *opposite* way, and nothing stays synchronised under the
pointer. From Wcoast:

```
this._tx -= dx * VIEW_MOTION_GAIN;
```

This is NOT Rack's existing Option-drag, which is grab-and-drag: there the content
follows the pointer 1:1 and the point under the cursor stays under it. Both conventions
are defensible; mixing them is not. This mode uses the chasing convention throughout,
which is what makes edge-scrolling consistent — at the left edge you reveal what is to
the left, and moving the pointer left does the same thing. Under grab-and-drag those two
would pull in opposite directions.

Gain of 3 is deliberate coarse positioning: a sweep across the window covers several
windows of rack. You aim precisely after releasing Option.


## The pointer is never captured

No cursor locking, no freezing, no warping. This is a hard requirement, not a
preference: the screen magnifier follows the pointer, so a locked or teleported pointer
takes the magnified viewport with it. Rack has cursor-locking machinery for knob drags —
do not reach for it here.

A consequence worth knowing: when the view pans under a stationary pointer, the magnifier
stays put and the content flows past it. That is the reading experience the whole feature
exists to provide, and it only works because the pointer is not the thing being moved.


## Smooth zoom by suppressing invalidation, not by snapshotting

Wcoast replaces the live rack with a bitmap for the duration of the gesture, because
re-rasterising every faceplate against a stream of wheel events "is what made zooming
feel like wading".

Rack has the same disease and a better cure available, because **it already keeps a
bitmap per module**. Every panel is a cached framebuffer texture. Zooming churns only
because `FramebufferWidget::draw` marks itself dirty when the render scale changes. While
this mode is active we stop doing that, and nanovg scales the textures that already
exist — the draw path already computes a `scaleRatio` and stretches the image, so this
needs no new drawing code at all.

Four reasons this beats porting the snapshot:

- **Everything stays live.** A frozen picture cannot show a cable being pulled. Once
  click-to-connect removes mouse-down cable dragging, a cable in flight can be carried
  while panning and zooming, with no rework of this mode.
- A whole-rack texture runs out. Panning leaves its edges quickly, and capturing the full
  module bounding box at retina scale hits OpenGL's maximum texture size on a big patch.
- It costs about five lines instead of a snapshot subsystem.
- **It removes the panel-dimming cost for free.** Panel brightness reduction re-filters
  on every framebuffer re-render, at 2–8 ms per panel plus a GPU readback. Zooming
  without suppression would re-filter every visible panel every frame. Suppression means
  no re-render, so no filter.

The trade is the same one Wcoast documents: panels look soft while zoomed in during the
gesture, and sharpen the moment you release. That happens automatically — on release the
scale no longer matches, so every framebuffer re-renders once.

The subpixel-change invalidation is suppressed too, for the same reason during panning.
The clip-box invalidation is left alone, since that is what fills in content scrolling
into view.


## Behaviour details

- **Mode detection polls** `Window::getMods()` in `step()` rather than watching key
  events, so releasing Option while the window is unfocused cannot strand you in the
  mode.
- **Clicks are blocked** from reaching modules, so a stray click mid-navigation cannot
  grab a knob. Mostly free: `ScrollWidget::onButton` already steals Option-plus-left
  before children, so only right and middle click needed adding. Wcoast does the same,
  suppressing `contextmenu` while Option is held.
- **Edge-scroll needs a fresh pointer position.** It reads the last position seen by
  `onHover`, and only runs on a frame where a hover actually arrived. Move the pointer
  off the rack — onto the menu bar, say — and edge-scrolling stops rather than running
  away on a stale coordinate.
- **Cmd is untouched.** Cmd-scroll still zooms exactly as it does in stock Rack, and
  `settings::mouseWheelZoom` still inverts it. Option-scroll zooms in addition.
- **Zoom still pivots on the pointer**, as stock Rack does, and keeps Rack's existing
  limits of a quarter to four times.

Settings, so it can be tuned by feel: `navPanEnabled`, `navPanGain`, `navEdgeMargin`,
`navEdgeRate`, exposed in the View menu.


## Not done

- **No cursor change.** Wcoast switches the cursor to `all-scroll` while the mode is
  held, which is a genuine affordance. Rack has no cursor-shape API — it never calls
  `glfwCreateStandardCursor` — so this would mean adding one. Worth doing later.
- Wcoast also suspends its animation loops for the gesture (cable flow, scopes, monitor
  pulses). Our equivalent is the invalidation suppression; Rack's own animations are
  cheap by comparison and are left running.


## ABI notes

- Mode state lives in `RackScrollWidget::Internal`, an opaque struct defined in the .cpp.
  Plugins cannot see it, so new fields there are ABI-free. This is the only clean place
  in the codebase to put widget state.
- The shared flag lives in `src/app/navMode.{hpp,cpp}` with a private header, so
  `FramebufferWidget` can read it without anything entering the plugin API.
- Only existing non-inline function bodies are modified.
