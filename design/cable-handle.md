# Cable grab handles

Hovering a cable near a jack reveals a pill-shaped swelling of the cable a short distance
from the port. Clicking it lifts that end off the port and leaves you carrying the cable,
exactly as though you had started pulling it from that terminal.

Ported from Wcoast's grab stretch (`host/rack.js`, the `LIT_GRAB_*` constants and the
"short GRAB STRETCH just off each terminal" block).

## Constants

| Constant | Value | From |
| --- | --- | --- |
| dwell before revealing | 300 ms | Wcoast `LIT_HOVER_MS` |
| hit and draw width | 2.2 cable widths | `LIT_GRAB_W` / `LIT_GRAB_SHOW` |
| start offset from the drawn cable end | 8 px | see below |
| stretch length | 0.62 x port radius | `LIT_GRAB_TO_R - LIT_GRAB_FROM_R`, cut 25% |
| gap from the jack | 2 mm | `LIT_GRAB_GAP_MM` |

Length and clearance are both **proportional to the port**, not fixed pixel counts, so they
stay right on plugins that draw unusually large or small jacks. Wcoast's ratio works out at
0.83 of a port radius; this is deliberately 25% shorter, because Rack panels pack controls
far closer to their jacks and a pill that reaches a neighbouring knob covers something you
need to click.

On a short cable — adjacent jacks — both the clearance and the length shrink to fit half the
cord, rather than refusing a handle. Requiring the full size was what made short cables
ungrabbable.

**Start clear of the port.** Wcoast records that a handle whose round cap reached back over
the jack was exactly how a press meant for the terminal ended up catching the cord. That
matters more here than it did there, because the port is now the click target for picking up
a *new* cable — an overlapping handle would make new connections unreliable, not just untidy.

It is invisible until hovered. A cable that advertised this everywhere would be wearing
decoration it does not need.

## Rack has no cable hit-testing

This is the one genuinely new capability. `CableWidget` is a plain `widget::Widget`, not
opaque, with no button or hover handler, and its `box` is a fixed 9 by 9 with no relation to
where the cable actually runs. Event dispatch can therefore never find a cable.

So proximity is tested explicitly from `RackWidget::onHover`: sample each complete cable's
curve into 48 points, and measure the pointer's distance to the segments overlapping the end
stretch. The sampling reuses the same `cableSlumpPos` the cable is drawn with — the formula
was made non-static for exactly this reason, so the handle can never sit on a curve that has
drifted out of step with the drawn one.

**The click must be claimed before children.** The pill sits over a module panel, and
`ModuleWidget` consumes clicks, so anything tested after dispatch would never see it.
`RackWidget::onButton` checks the handle first, which is the same "handle before children"
pattern `ScrollWidget` uses to claim Option-click.

## Showing on one end or both

Only the nearer end shows a handle. Showing both would be harmless anyway, because leaving
the stretch hides it immediately rather than after a timeout — so it clears the moment the
pointer moves away.

## Cancelling a grabbed cable, and the trap it exposed

Cancelling is a click on a panel, Escape, or Delete.

Detaching a cable is a **real change to the patch**, unlike picking up a fresh one. The
first version of `cableClickCancel` deleted the carried cable and *discarded* the pending
history action, which is correct for a cable that never existed — but for a grabbed cable it
would have destroyed a connection with no way back.

So a carry now knows whether it was grabbed or created. Cancelling a grabbed carry commits
the `CableRemove` rather than discarding it: the cable is thrown away, matching what Rack
does when a dragged cable is released over nothing, but Undo restores it.

A successful reconnection commits both the `CableRemove` and the `CableAdd` as one
ComplexAction, which is exactly the "move cable" action Rack already names.

## ABI notes

State is a single record in `src/app/cableHandle.{hpp,cpp}` behind a private header, since
`CableWidget` is an SDK class and cannot gain members. Only one handle shows at a time, so
one record suffices rather than a table.

Modified bodies only: `RackWidget::onHover`, `RackWidget::onButton`,
`CableWidget::drawLayer`, `Scene::onHoverKey`, and `getSlumpPos` renamed to
`cableSlumpPos` and made non-static.
