/** Render-path wiring for panel brightness reduction — see design/panel-dimming.md */
#include "panelDim.hpp"
#include "panelFilter.hpp"

#include <widget/FramebufferWidget.hpp>
#include <app/SvgPanel.hpp>
#include <app/ModuleWidget.hpp>
#include <plugin/Model.hpp>
#include <plugin/Plugin.hpp>
#include <window/Window.hpp>
#include <app/Scene.hpp>
#include <settings.hpp>
#include <context.hpp>

#include <map>
#include <vector>


namespace rack {
namespace window {


/** Cached filtered image for one framebuffer widget.

Kept in a side table keyed by widget pointer rather than as a member on
FramebufferWidget, because that class is in the SDK and plugins subclass it — adding a
member would break the plugin ABI. See CLAUDE.md.
*/
struct PanelDimCache {
	int image = -1;
	/** Framebuffer size the image was built for. A size change means a new render
	resolution, so the filtered image is stale. */
	math::Vec size;
	/** The parameters the image was built with, so a settings change invalidates it. */
	PanelFilterParams params;
	float strength = -1.f;
};


static std::map<widget::FramebufferWidget*, PanelDimCache> caches;


static bool paramsEqual(const PanelFilterParams& a, const PanelFilterParams& b) {
	return a.blurRadius == b.blurRadius
		&& a.threshold == b.threshold
		&& a.maxAttenuation == b.maxAttenuation
		&& a.contrastGain == b.contrastGain
		&& a.contrastLimit == b.contrastLimit
		&& a.edgeSensitivity == b.edgeSensitivity;
}


/** Returns the ModuleWidget whose panel this framebuffer draws, or NULL if this
framebuffer is not a panel.

The gate is that the framebuffer's parent is an SvgPanel. That covers SvgPanel and
ThemedSvgPanel, which is how nearly every plugin builds its panel. Plugins that assemble
a custom panel widget are not covered yet; widening this is a one-line change.
*/
static app::ModuleWidget* panelModuleWidget(widget::FramebufferWidget* fbw) {
	if (!fbw || !fbw->parent)
		return NULL;
	if (!dynamic_cast<app::SvgPanel*>(fbw->parent))
		return NULL;
	return fbw->getAncestorOfType<app::ModuleWidget>();
}


/** Builds the effective parameters for one panel, folding in its per-panel strength.

Strength scales the dimming only, not the contrast gain: legend text needs to be legible
on every panel, whereas how hard to dim is exactly the thing that varies between a white
slab and an already-dark panel.
*/
static PanelFilterParams effectiveParams(float strength) {
	PanelFilterParams p;
	p.blurRadius = settings::panelDimBlurRadius;
	p.threshold = settings::panelDimThreshold;
	p.contrastGain = settings::panelDimContrastGain;
	p.edgeSensitivity = settings::panelDimEdgeSensitivity;
	// maxAttenuation is a multiplier where 1 means no dimming, so scaling by strength
	// interpolates from "no dimming" toward the configured attenuation.
	const float full = settings::panelDimMaxAttenuation;
	p.maxAttenuation = 1.f - strength * (1.f - full);
	return p;
}


void panelDimProcess(widget::FramebufferWidget* fbw) {
	if (!settings::panelDimEnabled)
		return;

	app::ModuleWidget* mw = panelModuleWidget(fbw);
	if (!mw)
		return;

	NVGLUframebuffer* fb = fbw->getFramebuffer();
	if (!fb)
		return;

	const math::Vec size = fbw->getFramebufferSize();
	const int w = (int) size.x;
	const int h = (int) size.y;
	if (w <= 0 || h <= 0)
		return;

	// A stored override wins. Otherwise -1 means "derive it from the panel's own
	// measured brightness", which is what keeps this usable across hundreds of modules
	// from dozens of developers without hand-tuning each one.
	float strength = -1.f;
	std::string pluginSlug, moduleSlug;
	if (mw->model && mw->model->plugin) {
		pluginSlug = mw->model->plugin->slug;
		moduleSlug = mw->model->slug;
		strength = settings::getPanelDimStrength(pluginSlug, moduleSlug);
	}

	PanelDimCache& cache = caches[fbw];

	// Reuse the cached image unless something it depends on changed. Panel artwork is
	// static, so this is the normal path and the filter runs rarely.
	if (cache.image >= 0 && cache.size.equals(size)
		&& cache.strength == strength
		&& paramsEqual(cache.params, effectiveParams(strength >= 0.f ? strength : 1.f)))
		return;

	NVGcontext* vg = APP->window->vg;

	// Read the rendered panel back from the framebuffer. glReadPixels returns rows
	// bottom-up, which is the same order the framebuffer's own texture is stored in, so
	// recreating an image from this buffer with the same NVG_IMAGE_FLIPY flag reproduces
	// the original orientation exactly.
	std::vector<uint8_t> pixels((size_t) w * h * 4);
	nvgluBindFramebuffer(fb);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	nvgluBindFramebuffer(NULL);

	// First pass on a panel with no override: measure it, derive a strength, and only
	// then filter. Measuring costs one extra filter run per panel, once.
	if (strength < 0.f) {
		PanelStats stats;
		std::vector<uint8_t> probe = pixels;
		PanelFilterParams measure = effectiveParams(1.f);
		panelFilterApply(probe.data(), w, h, measure, 4, &stats);
		strength = panelFilterAutoStrength(stats);
		INFO("Panel dim: %s/%s measured mean %.3f bright %.3f -> strength %.2f",
			pluginSlug.c_str(), moduleSlug.c_str(),
			stats.meanLuminance, stats.brightFraction, strength);
	}

	const PanelFilterParams params = effectiveParams(strength);
	panelFilterApply(pixels.data(), w, h, params, 4, NULL);

	if (cache.image >= 0)
		nvgDeleteImage(vg, cache.image);

	cache.image = nvgCreateImageRGBA(vg, w, h,
		NVG_IMAGE_FLIPY | NVG_IMAGE_PREMULTIPLIED, pixels.data());
	cache.size = size;
	cache.params = params;
	cache.strength = strength;

	if (cache.image < 0)
		WARN("Panel dim: could not create filtered image for %s/%s", pluginSlug.c_str(), moduleSlug.c_str());
}


int panelDimImage(widget::FramebufferWidget* fbw) {
	if (!settings::panelDimEnabled)
		return -1;
	auto it = caches.find(fbw);
	if (it == caches.end())
		return -1;
	// A size mismatch means the framebuffer was re-rendered at a new resolution and the
	// cached image has not caught up yet. Drawing it would stretch, so fall back to the
	// raw framebuffer for this frame.
	if (!it->second.size.equals(fbw->getFramebufferSize()))
		return -1;
	return it->second.image;
}


void panelDimInvalidate(widget::FramebufferWidget* fbw) {
	auto it = caches.find(fbw);
	if (it == caches.end())
		return;
	if (it->second.image >= 0 && APP && APP->window && APP->window->vg)
		nvgDeleteImage(APP->window->vg, it->second.image);
	caches.erase(it);
}


void panelDimInvalidateAll() {
	NVGcontext* vg = (APP && APP->window) ? APP->window->vg : NULL;
	for (auto& pair : caches) {
		if (pair.second.image >= 0 && vg)
			nvgDeleteImage(vg, pair.second.image);
	}
	caches.clear();
}


void panelDimRefresh() {
	panelDimInvalidateAll();
	if (APP && APP->scene) {
		// Recurses through the whole widget tree; FramebufferWidget::onDirty marks each
		// one for re-render, which is what actually re-runs the filter.
		widget::Widget::DirtyEvent eDirty;
		APP->scene->onDirty(eDirty);
	}
}


} // namespace window
} // namespace rack
