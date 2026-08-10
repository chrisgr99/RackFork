/** Click-to-connect cables — see design/cable-click.md */
#include "cableClick.hpp"

#include <app/PortWidget.hpp>
#include <app/CableWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <engine/Port.hpp>
#include <history.hpp>
#include <context.hpp>
#include <string.hpp>
#include <settings.hpp>
#include "controlAppearance.hpp"
#include "cableHandle.hpp"


namespace rack {
namespace app {


/** The port the in-flight cable was picked up from, or NULL when nothing is in flight.
Global rather than per-port state, because the click that completes a connection lands on
a different widget than the one that started it. */
static PortWidget* originPort = NULL;
static history::ComplexAction* pendingHistory = NULL;
/** Set by a port that handled the current press, taken by RackWidget after dispatch. */
static bool portClickHandled = false;
/** True when the carried cable was lifted off a port rather than created fresh. Decides
whether cancelling has something to record. */
static bool grabbedExisting = false;


void cableClickNotePortClick() {
	portClickHandled = true;
}


bool cableClickTakePortClick() {
	const bool handled = portClickHandled;
	portClickHandled = false;
	return handled;
}


bool cableClickActive() {
	return originPort != NULL;
}


/** Removes and deletes every incomplete cable. */
static void discardIncomplete() {
	// Forget any shown grab handle first. It holds a raw CableWidget pointer, and one of the
	// cables about to be deleted could be the one it names — after which every hover and
	// click would be reading freed memory.
	cableHandleClear();
	for (CableWidget* cw : APP->scene->rack->getIncompleteCables()) {
		APP->scene->rack->removeCable(cw);
		delete cw;
	}
}


/** Commits or discards the pending history action, following the same rules
PortWidget::onDragEnd uses: nothing for an empty action, the bare action if there is only
one, otherwise the ComplexAction. */
static void commitHistory() {
	if (!pendingHistory)
		return;
	if (pendingHistory->isEmpty()) {
		delete pendingHistory;
	}
	else if (pendingHistory->actions.size() == 1) {
		APP->history->push(pendingHistory->actions[0]);
		pendingHistory->actions.clear();
		delete pendingHistory;
	}
	else {
		APP->history->push(pendingHistory);
	}
	pendingHistory = NULL;
}


void cableClickStart(PortWidget* port) {
	if (!port)
		return;
	// Already carrying one: the caller should have routed this to cableClickFinish.
	if (cableClickActive())
		return;

	pendingHistory = new history::ComplexAction;
	pendingHistory->name = string::translate("PortWidget.history.moveCable");
	grabbedExisting = false;

	CableWidget* cw = new CableWidget;
	// While carried there is no destination yet, so take the source's colour. It is
	// replaced by the destination's once the cable lands.
	cw->color = settings::cableAutoColor
		? appearance::portColor(port)
		: APP->scene->rack->getNextCableColor();
	cw->getPort(port->type) = port;
	APP->scene->rack->addCable(cw);

	originPort = port;
}


void cableClickAdopt(PortWidget* remainingPort, history::ComplexAction* h) {
	// Anything already in flight would be orphaned by this.
	if (cableClickActive())
		cableClickCancel();

	originPort = remainingPort;
	pendingHistory = h;
	grabbedExisting = true;
}


void cableClickFinish(PortWidget* port) {
	if (!port || !cableClickActive())
		return;

	// Which side of the carried cable still needs a port. For a cable picked up fresh this
	// is the far end; for one lifted off a jack by a grab handle it is the end that was
	// detached. Deriving it from the cable rather than from where the carry started is what
	// makes both cases behave the same.
	CableWidget* carried = NULL;
	for (CableWidget* cw : APP->scene->rack->getIncompleteCables()) {
		carried = cw;
		break;
	}
	if (!carried) {
		// Nothing actually in flight: the state and the rack disagree, so reset rather than
		// stay stuck refusing every click.
		cableClickCancel();
		return;
	}

	// A REFUSED DROP KEEPS THE CABLE IN HAND. Only the case that was asked for explicitly —
	// an already-occupied input — throws it away. Everything else just declines, because
	// destroying a connection because someone clicked slightly wrong is not a refusal, it
	// is a data loss.
	if (carried->getPort(port->type)) {
		// That side is already connected: output to output, or the end still plugged in.
		return;
	}

	if (port->type == engine::Port::INPUT && APP->scene->rack->getTopCable(port)) {
		// Inputs take one cable only. This is the discard case.
		cableClickCancel();
		return;
	}

	PortWidget* other = (port->type == engine::Port::OUTPUT) ? carried->inputPort : carried->outputPort;
	if (!other)
		return;
	if (port->type == engine::Port::OUTPUT) {
		if (APP->scene->rack->getCable(port, other))
			return;   // that exact connection already exists
		carried->outputPort = port;
	}
	else {
		if (APP->scene->rack->getCable(other, port))
			return;
		carried->inputPort = port;
	}

	carried->hoveredOutputPort = NULL;
	carried->hoveredInputPort = NULL;

	// The cable takes its destination's colour. Set HERE, at connect time, rather than
	// computed during draw: that way it is stored on the cable, saved with the patch, and
	// the user can still change it afterwards through Rack's own cable colour menu.
	if (settings::cableAutoColor && carried->inputPort)
		carried->color = appearance::portColor(carried->inputPort);

	carried->updateCable();

	history::CableAdd* h = new history::CableAdd;
	h->setCable(carried);
	if (pendingHistory)
		pendingHistory->push(h);
	else
		delete h;

	commitHistory();
	grabbedExisting = false;
	originPort = NULL;
	cableHandleClear();
}


void cableRecolourAll() {
	int count = 0;
	for (CableWidget* cw : APP->scene->rack->getCompleteCables()) {
		if (!cw->inputPort)
			continue;
		cw->color = appearance::portColor(cw->inputPort);
		count++;
	}
	INFO("Cables: recoloured %d by destination", count);
}


void cableClickCancel() {
	if (!cableClickActive())
		return;

	discardIncomplete();

	if (grabbedExisting) {
		// The cable existed and has now been detached and thrown away, which matches what
		// Rack does when you drop a dragged cable over nothing. But it IS a change, so the
		// pending CableRemove is committed rather than discarded — otherwise an accidental
		// cancel would destroy a connection with no way back. Undo restores it.
		commitHistory();
	}
	else if (pendingHistory) {
		// Freshly created and never connected: nothing happened, nothing to record.
		delete pendingHistory;
		pendingHistory = NULL;
	}

	grabbedExisting = false;
	originPort = NULL;
}


} // namespace app
} // namespace rack
