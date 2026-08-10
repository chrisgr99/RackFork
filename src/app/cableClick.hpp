#pragma once
/** Click-to-connect cables — see design/cable-click.md

DELIBERATELY PRIVATE. Lives in src/, not include/, so none of it becomes plugin API
surface.

A cable is picked up by clicking a port and dropped by clicking another, with no button
held in between. The in-flight cable's loose end already follows the pointer for free:
CableWidget::getInputPos and getOutputPos fall back to RackWidget::getMousePos() when
there is no port, which needs no drag at all.
*/

namespace rack {
namespace history {
struct ComplexAction;
}

namespace app {

struct PortWidget;


/** True while a cable is in flight, i.e. picked up and not yet dropped. */
bool cableClickActive();

/** Picks up a NEW cable from this port. Never grabs an existing cable off the port: with
no button held, a stray click on a patched port would otherwise silently unplug it. */
void cableClickStart(PortWidget* port);

/** Attempts to drop the in-flight cable on this port.

Outputs accept another connection, since an output may feed many inputs. An input accepts
only one, so dropping on an occupied input is refused — and a refused drop DISCARDS the
cable rather than leaving it stuck to the pointer.
*/
void cableClickFinish(PortWidget* port);

/** Records that a port has just handled a button press.

Needed because a port consumes its click with a NULL target, deliberately, to suppress
onDragStart. That erases the evidence the "did this click land on a port?" test would
otherwise rely on, so without this flag the cancel-on-click path fires immediately after
every pick-up and nothing appears to happen at all.
*/
void cableClickNotePortClick();

/** Returns whether a port handled the current press, and clears the flag. */
bool cableClickTakePortClick();

/** Takes over a cable that was just lifted off a port by a grab handle.

`remainingPort` is the end still connected, which becomes the origin for the carry.
`h` is the history action already holding the CableRemove for the detachment. Ownership
passes here.
*/
void cableClickAdopt(app::PortWidget* remainingPort, history::ComplexAction* h);

/** Recolours every existing cable from its destination port.

Needed because the colour is set at connect time, deliberately, so cables made before the
feature existed (or before it was switched on) keep whatever colour they were given.
Without this, an old patch stays in Rack's default red/green/blue rotation and looks like
the feature is broken.
*/
void cableRecolourAll();

/** Abandons the in-flight cable. Right-click, Escape, or a click that lands on anything
that is not a port. */
void cableClickCancel();


} // namespace app
} // namespace rack
