# Click-to-connect cables

Modification 3. A cable is picked up by clicking a port and dropped by clicking another,
with no button held in between.

The reason is the same as for Option-held navigation: a button-held drag forces the
pointer to travel while the button is down, which fights a screen magnifier that follows
the pointer. It also means a patch cable cannot be carried across a rack wider than one
pointer sweep.

## One thing was already done for us

`CableWidget::getInputPos` and `getOutputPos` fall back to `RackWidget::getMousePos()`
when there is neither a connected nor a hovered port. So an in-flight cable's loose end
follows the pointer with no drag involved, and needed no change at all.

## Rules

- **Clicking a port always picks up a NEW cable.** It never grabs an existing cable off
  the port, which dragging does today. With no button held, a stray click on a patched
  port would otherwise silently unplug something.
- **Outputs accept another connection.** An output may feed many inputs, so dropping there
  simply adds one.
- **An occupied input refuses, and the carried cable is discarded.** Inputs take one cable
  only. A refused drop does not leave the cable stuck to the pointer.
- **Dropping on the port it came from** puts it away.
- **Cancelling** is right-click, Escape, or a left-click that lands on anything which is
  not a port — a panel or empty rack.

Unchanged: Shift-click still deletes the top cable, and right-click still opens the port
menu when nothing is in flight.

## How it avoids fighting the drag model

Everything happens in `PortWidget::onButton` on press, and the event is consumed with
`e.consume(NULL)`. That suppresses `onDragStart`, so `onDragStart`, `onDragEnd` and
`onDragDrop` never run for these clicks. The trick is Rack's own — the existing
Shift-click deletion does it, with the comment "Consume null so onDragStart isn't
triggered". The two models therefore do not coexist per click; one or the other handles
it completely.

Hover snapping had to move. `onDragEnter` and `onDragLeave` only fire during a drag, so
the preview showing where a carried cable will land is set from `onEnter` and `onLeave`
instead.

Undo follows the same shape as `PortWidget::onDragEnd`: an empty action is dropped, a
single action is pushed bare, more than one is pushed as a ComplexAction. Picking up never
removes a cable, so a cancelled pick-up has nothing to undo.

Cancelling on a click uses the event's **target** rather than its position — the target is
precisely "the widget that took this click", so no geometry test is needed to tell a port
from a panel.

## Carrying a cable while navigating

Because Option-held navigation suppresses framebuffer invalidation rather than freezing
the rack to a bitmap, everything stays live during a gesture. So a cable can be picked up,
carried while panning and zooming, and dropped on the far side of a patch — which a
button-held drag can never do. See design/zoom-pan.md.

## ABI notes

- In-flight state is global, in `src/app/cableClick.{hpp,cpp}` behind a private header,
  because the click that completes a connection lands on a different widget than the one
  that started it.
- Only existing non-inline function bodies are modified: `PortWidget::onButton`,
  `onEnter`, `onLeave`, `RackWidget::onButton`, `Scene::onHoverKey`.
- No new members, virtuals or layout changes on any SDK class.

## Not done

- Dropping onto an occupied input refuses rather than replacing. Replacing would be more
  convenient but destroys a connection on a single click with no button held, which is too
  easy to do by accident.
- Ctrl-drag's "create new instead of grabbing" and Ctrl-Shift-drag's "clone" have no
  click equivalents yet. Since a plain click already creates new, the first is redundant;
  cloning still needs a gesture.
