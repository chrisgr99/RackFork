#include <app/RackScrollWidget.hpp>
#include "navMode.hpp"
#include <app/Scene.hpp>
#include <app/RackWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/PortWidget.hpp>
#include <window/Window.hpp>
#include <context.hpp>
#include <settings.hpp>


namespace rack {
namespace app {


struct RackScrollWidget::Internal {
	/** For viewport expanding */
	float oldZoom = 0.f;
	math::Vec oldOffset;

	// ---- Option-held navigation. See design/zoom-pan.md.
	// This struct is opaque, defined here rather than in the SDK header, so adding fields
	// is invisible to plugins and carries no ABI risk.
	bool navActive = false;
	/** Last pointer position seen by onHover, in this widget's coordinates. */
	math::Vec navMousePos;
	/** Whether a hover arrived recently. Edge-scrolling reads navMousePos, and must not
	act on a stale coordinate: move the pointer onto the menu bar and hovers stop
	arriving, so edge-scrolling should stop rather than run away. */
	bool navHoverFresh = false;
};


RackScrollWidget::RackScrollWidget() {
	internal = new Internal;

	zoomWidget = new widget::ZoomWidget;
	container->addChild(zoomWidget);

	rackWidget = new RackWidget;
	rackWidget->box.size = RACK_OFFSET.mult(2);
	zoomWidget->addChild(rackWidget);

	reset();
}


RackScrollWidget::~RackScrollWidget() {
	delete internal;
}


void RackScrollWidget::reset() {
	offset = RACK_OFFSET * zoomWidget->getZoom() - math::Vec(30, 30);
}


math::Vec RackScrollWidget::getGridOffset() {
	return (offset / zoomWidget->getZoom() - RACK_OFFSET) / RACK_GRID_SIZE;
}


void RackScrollWidget::setGridOffset(math::Vec gridOffset) {
	offset = (gridOffset * RACK_GRID_SIZE + RACK_OFFSET) * zoomWidget->getZoom();
}


float RackScrollWidget::getZoom() {
	return zoomWidget->getZoom();
}


void RackScrollWidget::setZoom(float zoom) {
	setZoom(zoom, getSize().div(2));
}


void RackScrollWidget::setZoom(float zoom, math::Vec pivot) {
	zoom = math::clamp(zoom, std::pow(2.f, -2), std::pow(2.f, 2));

	offset = (offset + pivot) * (zoom / zoomWidget->getZoom()) - pivot;
	zoomWidget->setZoom(zoom);
}


void RackScrollWidget::zoomToModules() {
	widget::Widget* moduleContainer = rackWidget->getModuleContainer();
	math::Rect bound = moduleContainer->getChildrenBoundingBox();
	zoomToBound(bound);
}


void RackScrollWidget::zoomToBound(math::Rect bound) {
	if (!bound.pos.isFinite())
		return;
	bound = bound.grow(math::Vec(24, 24));
	math::Vec size = getSize();
	float zoom = std::min(size.x / bound.size.x, size.y / bound.size.y);
	offset = bound.getCenter() * zoom - size / 2;
	zoomWidget->setZoom(zoom);
}


void RackScrollWidget::step() {
	float zoom = getZoom();

	// Compute module bounding box
	math::Rect moduleBox = rackWidget->getModuleContainer()->getChildrenBoundingBox();
	if (!moduleBox.size.isFinite())
		moduleBox = math::Rect(RACK_OFFSET, math::Vec(0, 0));

	// Expand moduleBox by a screen size
	math::Rect scrollBox = moduleBox;
	scrollBox.pos = scrollBox.pos.mult(zoom);
	scrollBox.size = scrollBox.size.mult(zoom);
	scrollBox = scrollBox.grow(box.size.mult(0.9));

	// Expand to the current viewport box so that moving modules (and thus changing the module bounding box) doesn't clamp the scroll offset.
	if (zoom == internal->oldZoom) {
		math::Rect viewportBox;
		viewportBox.pos = internal->oldOffset;
		viewportBox.size = box.size;
		scrollBox = scrollBox.expand(viewportBox);
	}

	// Reposition widgets
	zoomWidget->box = scrollBox;
	rackWidget->box.pos = scrollBox.pos.div(zoom).neg();

	// Scroll rack if dragging certain widgets near the edge of the screen
	math::Vec pos = APP->scene->mousePos - box.pos;
	math::Rect viewport = getViewport(box.zeroPos());
	widget::Widget* dw = APP->event->getDraggedWidget();
	if (dw && APP->event->dragButton == GLFW_MOUSE_BUTTON_LEFT &&
		(dynamic_cast<RackWidget*>(dw) || dynamic_cast<ModuleWidget*>(dw) || dynamic_cast<PortWidget*>(dw))) {
		float margin = 1.0;
		float speed = 15.0;
		if (pos.x <= viewport.pos.x + margin)
			offset.x -= speed;
		if (pos.x >= viewport.pos.x + viewport.size.x - margin)
			offset.x += speed;
		if (pos.y <= viewport.pos.y + margin)
			offset.y -= speed;
		if (pos.y >= viewport.pos.y + viewport.size.y - margin)
			offset.y += speed;
	}

	// Hide scrollbars if fullscreen
	hideScrollbars = APP->window->isFullScreen();

	ScrollWidget::step();

	internal->oldOffset = offset;
	internal->oldZoom = zoom;

	navStep();
}


/** Enters and leaves Option-held navigation, and runs the edge scroll.

Mode state is POLLED here rather than driven by key events, so releasing Option while the
window is unfocused cannot strand you in the mode.
*/
void RackScrollWidget::navStep() {
	const bool wasActive = internal->navActive;
	const bool active = settings::navPanEnabled
		&& (APP->window->getMods() & RACK_MOD_MASK) == GLFW_MOD_ALT;

	if (active != wasActive) {
		internal->navActive = active;
		// While active, framebuffers must not invalidate on scale or subpixel change.
		// Letting the existing textures stretch is what makes zooming smooth, and it also
		// stops panel brightness reduction re-filtering every panel every frame.
		navModeSetActive(active);
		if (!active)
			internal->navHoverFresh = false;
	}

	if (!active)
		return;

	// Edge scroll: a steady rate while the pointer sits within the margin of an edge, so a
	// rack wider than one pointer sweep can be crossed without releasing. Steady, not
	// ramped — Wcoast chose that deliberately.
	if (!internal->navHoverFresh)
		return;
	internal->navHoverFresh = false;

	const math::Vec size = getSize();
	const float margin = settings::navEdgeMargin;
	// Per second, not per frame: Rack limits itself to 30 Hz on macOS while the browser
	// this came from runs at 60, so a per-frame step would travel at half speed.
	const float step = settings::navEdgeRate * (float) APP->window->getLastFrameDuration();

	math::Vec delta;
	if (internal->navMousePos.x <= margin)
		delta.x = -step;
	else if (internal->navMousePos.x >= size.x - margin)
		delta.x = step;
	if (internal->navMousePos.y <= margin)
		delta.y = -step;
	else if (internal->navMousePos.y >= size.y - margin)
		delta.y = step;

	// Same sign as the pointer motion below: at the left edge you reveal what is to the
	// left, which is also what moving the pointer left does.
	offset = offset.plus(delta);
}


void RackScrollWidget::draw(const DrawArgs& args) {
	ScrollWidget::draw(args);
}


void RackScrollWidget::onHoverKey(const HoverKeyEvent& e) {
	ScrollWidget::onHoverKey(e);
}


void RackScrollWidget::onHoverScroll(const HoverScrollEvent& e) {
	int mods = APP->window->getMods();
	bool doZoom = mods & RACK_MOD_CTRL;
	if (settings::mouseWheelZoom)
		doZoom ^= true;
	// While navigating, the wheel always zooms, and children are not offered it — they are
	// non-interactive for the duration anyway. Cmd keeps working exactly as in stock Rack.
	const bool navZoom = internal->navActive;
	if (navZoom)
		doZoom = true;

	if (doZoom) {
		if (!navZoom) {
			// Dispatch to children first and zoom only if they don't consume
			OpaqueWidget::onHoverScroll(e);
			if (e.isConsumed())
				return;
		}
		// Increase zoom
		float zoomDelta = e.scrollDelta.y / 50 / 4;
		if (settings::invertZoom)
			zoomDelta *= -1;
		float zoom = getZoom() * std::pow(2.f, zoomDelta);
		setZoom(zoom, e.pos);
		e.consume(this);
		return;
	}

	ScrollWidget::onHoverScroll(e);
}


void RackScrollWidget::onHover(const HoverEvent& e) {
	if (internal->navActive) {
		internal->navMousePos = e.pos;
		internal->navHoverFresh = true;

		// THE VIEW CHASES THE POINTER: move the pointer toward what you want to see, so
		// the content slides the opposite way and nothing stays synchronised underneath.
		// This is deliberately NOT Rack's Option-drag, which is grab-and-drag. Mixing the
		// two conventions would make edge-scrolling reverse direction. See design/zoom-pan.md.
		if (!e.mouseDelta.isZero()) {
			offset = offset.plus(e.mouseDelta.mult(settings::navPanGain).div(getAbsoluteZoom()));
		}
		// Consumed so children get no hover: no tooltips, no knob highlights while
		// navigating.
		e.consume(this);
		return;
	}

	ScrollWidget::onHover(e);

	// Hide menu bar if fullscreen and moving mouse over the RackScrollWidget
	if (APP->window->isFullScreen()) {
		APP->scene->menuBar->hide();
	}
}


void RackScrollWidget::onButton(const ButtonEvent& e) {
	// Block clicks from reaching modules while navigating, so a stray click cannot grab a
	// knob mid-gesture. Releases are let through so anything already in flight can finish.
	// Mostly this only adds right and middle click: ScrollWidget already steals
	// Option-plus-left before children.
	if (internal->navActive && e.action == GLFW_PRESS) {
		e.consume(this);
		return;
	}

	ScrollWidget::onButton(e);
	if (e.isConsumed())
		return;

	// Zoom in/out with extra mouse buttons
	if (e.action == GLFW_PRESS) {
		if (e.button == GLFW_MOUSE_BUTTON_4) {
			float zoom = getZoom() * std::pow(2.f, -0.5f);
			setZoom(zoom, e.pos);
			e.consume(this);
		}
		if (e.button == GLFW_MOUSE_BUTTON_5) {
			float zoom = getZoom() * std::pow(2.f, 0.5f);
			setZoom(zoom, e.pos);
			e.consume(this);
		}
	}
}


} // namespace app
} // namespace rack
