/** Grab handles for detaching a connected cable — see design/cable-handle.md */
#include "cableHandle.hpp"
#include "cableClick.hpp"

#include <app/CableWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <window/Window.hpp>
#include <history.hpp>
#include <settings.hpp>
#include <context.hpp>
#include <string.hpp>

#include <cmath>
#include <vector>


namespace rack {
namespace app {


// Wcoast's LIT_HOVER_MS: dwell before the stretch reveals itself. Deliberately shorter than
// the one-second dwell a bend handle uses — this one should feel available, not earned.
static const double HANDLE_DWELL = 0.300;

// Hit and draw width, in cable widths. Wcoast's LIT_GRAB_W and LIT_GRAB_SHOW, both 2.2.
static const float HANDLE_WIDTH = 2.2f;

// Length as a FRACTION OF THE PORT RADIUS, not a fixed pixel count — this is the
// relationship Wcoast's constants express and the thing a hardcoded length gets wrong at
// any other jack size. Wcoast: length = ring * (LIT_GRAB_TO_R - LIT_GRAB_FROM_R), i.e.
// 2.3mm * 1.08, against an outer jack radius of 3.0mm, which is 0.83 of a radius.
//
// Deliberately 25% shorter than that here. Rack panels pack controls much closer to their
// jacks than a Wcoast faceplate does, and a pill long enough to reach a neighbouring knob
// or button is a pill that covers something you need to click.
static const float HANDLE_LEN_PER_RADIUS = 0.62f;

// Wcoast LIT_GRAB_GAP_MM = 2.0mm, in Rack pixels at 75 DPI.
static const float HANDLE_GAP = 2.0f * (75.f / 25.4f);

// The inset the cable's draw path applies to each endpoint, so the cord starts short of the
// jack centre.
static const float CABLE_END_INSET = 14.f;


struct ShownHandle {
	CableWidget* cw = NULL;
	/** Which end: OUTPUT means the handle grabs the output end. */
	engine::Port::Type type = engine::Port::INPUT;
	/** When the pointer first entered this stretch. */
	double since = 0.0;
	bool visible = false;
};

static ShownHandle shown;


void cableHandleClear() {
	shown.cw = NULL;
	shown.visible = false;
}


/** Samples a cable's curve into a polyline with cumulative arc lengths. */
struct CurveSamples {
	static const int N = 48;
	math::Vec pts[N + 1];
	float cum[N + 1];
	float total = 0.f;
};

static void sampleCable(CableWidget* cw, CurveSamples& out) {
	math::Vec p0 = cw->getOutputPos();
	math::Vec p1 = cw->getInputPos();
	math::Vec ctrl = cableSlumpPos(p0, p1);

	// Match the endpoint inset the draw path applies, so the handle sits on the cable as
	// drawn rather than on a curve that starts inside the jack.
	const float dist = 14.f;
	p0 = p0.plus(ctrl.minus(p0).normalize().mult(dist));
	p1 = p1.plus(ctrl.minus(p1).normalize().mult(dist));

	out.pts[0] = p0;
	out.cum[0] = 0.f;
	for (int i = 1; i <= CurveSamples::N; i++) {
		const float t = (float) i / CurveSamples::N;
		const float u = 1.f - t;
		out.pts[i] = p0.mult(u * u).plus(ctrl.mult(2.f * u * t)).plus(p1.mult(t * t));
		out.cum[i] = out.cum[i - 1] + out.pts[i].minus(out.pts[i - 1]).norm();
	}
	out.total = out.cum[CurveSamples::N];
}


/** Distance from a point to the segment a--b. */
static float distToSegment(math::Vec p, math::Vec a, math::Vec b) {
	math::Vec ab = b.minus(a);
	const float len2 = ab.x * ab.x + ab.y * ab.y;
	if (len2 <= 0.f)
		return p.minus(a).norm();
	float t = (p.minus(a).x * ab.x + p.minus(a).y * ab.y) / len2;
	t = math::clamp(t, 0.f, 1.f);
	return p.minus(a.plus(ab.mult(t))).norm();
}


/** Where the grab stretch sits on this cable, as arc lengths from the cable's start.

Both the clearance and the length scale with the port, and both shrink on a short cable so
that adjacent jacks still get a handle instead of none. Returns false if the cable is too
short to carry one at all.
*/
static bool stretchRange(const CurveSamples& s, PortWidget* port, float thickness,
	bool fromOutput, float& outA, float& outB) {

	if (s.total <= 0.f || !port)
		return false;

	const float radius = std::fmin(port->box.size.x, port->box.size.y) / 2.f;
	const float width = thickness * HANDLE_WIDTH;

	// START CLEAR OF THE PORT. Wcoast records that a handle whose round cap reached back over
	// the jack was exactly how a press meant for the terminal ended up catching the cord.
	// The cap reaches half a width back, so begin far enough out that its edge only touches
	// the disc. The cable's own endpoint inset already covers part of that distance.
	float from = std::fmax(0.f, radius + width / 2.f - CABLE_END_INSET) + HANDLE_GAP;
	float len = radius * HANDLE_LEN_PER_RADIUS;

	// A short cable — adjacent jacks — cannot afford the full clearance. Shrink both rather
	// than refusing a handle, which is what made short cables ungrabbable.
	const float half = s.total / 2.f;
	from = std::fmin(from, half * 0.5f);
	len = std::fmin(len, half - from);
	if (len < 2.f)
		return false;

	outA = fromOutput ? from : s.total - from - len;
	outB = outA + len;
	return true;
}


/** Closest distance from the pointer to the stretch of this cable near one end.
`fromOutput` selects which end. Returns INFINITY if the cable is too short to have one. */
static float distToStretch(const CurveSamples& s, math::Vec p, PortWidget* port,
	float thickness, bool fromOutput) {

	float a, b;
	if (!stretchRange(s, port, thickness, fromOutput, a, b))
		return INFINITY;

	float best = INFINITY;
	for (int i = 1; i <= CurveSamples::N; i++) {
		// Only segments overlapping the stretch.
		if (s.cum[i] < a || s.cum[i - 1] > b)
			continue;
		best = std::fmin(best, distToSegment(p, s.pts[i - 1], s.pts[i]));
	}
	return best;
}


void cableHandleHover(math::Vec rackPos) {
	if (!settings::cableGrabHandles) {
		cableHandleClear();
		return;
	}
	// While carrying a cable there is nothing to detach, and a handle under the pointer
	// would only compete with the port you are aiming at.
	if (cableClickActive()) {
		cableHandleClear();
		return;
	}

	CableWidget* bestCw = NULL;
	engine::Port::Type bestType = engine::Port::INPUT;
	float bestDist = INFINITY;

	for (CableWidget* cw : APP->scene->rack->getCompleteCables()) {
		CurveSamples s;
		sampleCable(cw, s);
		const float thickness = 6.f;
		const float tol = thickness * HANDLE_WIDTH / 2.f;

		const float dOut = distToStretch(s, rackPos, cw->outputPort, thickness, true);
		if (dOut < tol && dOut < bestDist) {
			bestDist = dOut;
			bestCw = cw;
			bestType = engine::Port::OUTPUT;
		}
		const float dIn = distToStretch(s, rackPos, cw->inputPort, thickness, false);
		if (dIn < tol && dIn < bestDist) {
			bestDist = dIn;
			bestCw = cw;
			bestType = engine::Port::INPUT;
		}
	}

	if (!bestCw) {
		// Leaving the stretch hides it at once, which is what makes showing it on both ends
		// harmless: it clears the moment you move away.
		cableHandleClear();
		return;
	}

	const double now = APP->window->getFrameTime();
	if (shown.cw != bestCw || shown.type != bestType) {
		shown.cw = bestCw;
		shown.type = bestType;
		shown.since = now;
		shown.visible = false;
	}
	else if (!shown.visible && now - shown.since >= HANDLE_DWELL) {
		shown.visible = true;
	}
}


bool cableHandleClick(math::Vec rackPos) {
	if (!settings::cableGrabHandles || !shown.visible || !shown.cw)
		return false;
	if (cableClickActive())
		return false;

	CableWidget* cw = shown.cw;
	const engine::Port::Type type = shown.type;

	// Confirm the click is actually on the stretch, not merely that a handle is showing.
	CurveSamples s;
	sampleCable(cw, s);
	const bool fromOutput = (type == engine::Port::OUTPUT);
	PortWidget* endPort = fromOutput ? cw->outputPort : cw->inputPort;
	const float thickness = 6.f;
	const float tol = thickness * HANDLE_WIDTH / 2.f;
	if (distToStretch(s, rackPos, endPort, thickness, fromOutput) >= tol)
		return false;

	PortWidget* remaining = (type == engine::Port::OUTPUT) ? cw->inputPort : cw->outputPort;
	if (!remaining)
		return false;

	// Detaching is a real change to the patch, so it is recorded before it happens. If the
	// carry is then cancelled, this action is committed rather than discarded, so Undo puts
	// the connection back. See cableClickCancel.
	history::ComplexAction* h = new history::ComplexAction;
	h->name = string::translate("PortWidget.history.moveCable");
	history::CableRemove* hr = new history::CableRemove;
	hr->setCable(cw);
	h->push(hr);

	cw->getPort(type) = NULL;
	cw->updateCable();

	cableClickAdopt(remaining, h);
	cableHandleClear();
	return true;
}


void cableHandleDraw(CableWidget* cw, NVGcontext* vg, float thickness) {
	if (!settings::cableGrabHandles || !shown.visible || shown.cw != cw)
		return;

	CurveSamples s;
	sampleCable(cw, s);

	const bool fromOutput = (shown.type == engine::Port::OUTPUT);
	PortWidget* endPort = fromOutput ? cw->outputPort : cw->inputPort;
	float a, b;
	if (!stretchRange(s, endPort, thickness, fromOutput, a, b))
		return;

	// A pill, not a block: round caps, drawn in the cable's own colour so it reads as a
	// swelling of the cable rather than a separate object stuck to it.
	nvgBeginPath(vg);
	bool started = false;
	for (int i = 1; i <= CurveSamples::N; i++) {
		if (s.cum[i] < a || s.cum[i - 1] > b)
			continue;
		if (!started) {
			nvgMoveTo(vg, s.pts[i - 1].x, s.pts[i - 1].y);
			started = true;
		}
		nvgLineTo(vg, s.pts[i].x, s.pts[i].y);
	}
	if (!started)
		return;

	nvgStrokeColor(vg, cw->color);
	nvgStrokeWidth(vg, thickness * HANDLE_WIDTH);
	nvgLineCap(vg, NVG_ROUND);
	nvgStroke(vg);
}


} // namespace app
} // namespace rack
