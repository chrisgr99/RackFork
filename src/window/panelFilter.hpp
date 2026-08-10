#pragma once
/** Panel brightness reduction — see design/panel-dimming.md

DELIBERATELY PRIVATE. This header lives in src/, not include/, so none of it becomes
plugin API surface. Do not move it into include/ and do not reference it from any
header under include/.

Ported from GXW src/imageTransform.js (that project's DESIGN.md section 26).
*/
// Deliberately depends on nothing from Rack, so it can be compiled and tested
// standalone. Keep it that way.
#include <cstdint>
#include <cstddef>


namespace rack {
namespace window {


struct PanelFilterParams {
	/** Spatial scale of the regional luminance estimate, in pixels. Larger means only
	very broad bright regions are dimmed. */
	float blurRadius = 50.f;
	/** Regional luminance below which no dimming is applied, 0-1. */
	float threshold = 0.5f;
	/** Multiplier applied at maximum regional luminance. 0.5 halves the brightest large
	regions. 1.0 disables dimming. */
	float maxAttenuation = 0.5f;
	/** Local contrast amplification. 0 disables. Amplifies a pixel's deviation from its
	regional estimate, which is what rescues low-contrast legend text. */
	float contrastGain = 0.f;
	/** Ceiling on how far a pixel may be pushed by contrastGain, in 0-255 luminance
	units. Stops raster compression artefacts from turning into grit. */
	float contrastLimit = 64.f;
	/** How strongly the blur respects luminance edges, in 0-1 luminance terms.
	THIS IS THE SIZE-SENSITIVITY DIAL, and it matters more than it looks.

	Low (0.2, the value GXW tuned for photographs): edges are respected, so a small
	bright feature keeps its own high regional estimate and therefore gets dimmed just
	like a large one. Clusters of small bright things all get dimmed. Size barely
	matters.

	High (1.0 and up): the blur behaves more like a plain Gaussian and bleeds across
	edges, so a small bright feature surrounded by dark has its estimate averaged down
	and escapes dimming, while large regions still get dimmed. This is the "dim large
	areas, leave small ones bright" behaviour.

	Panel artwork is hard-edged, which makes this dial far more consequential here than
	it is on photographs. See design/panel-dimming.md. */
	float edgeSensitivity = 0.2f;
};


/** Statistics measured from a panel, used to derive a sensible per-panel default
strength so the user does not have to tune every module by hand. */
struct PanelStats {
	/** Mean luminance over non-transparent pixels, 0-1. */
	float meanLuminance = 0.f;
	/** Fraction of non-transparent area whose regional luminance exceeds the
	threshold, 0-1. */
	float brightFraction = 0.f;
};


/** Applies brightness reduction to a premultiplied-alpha RGBA image, in place.

`data` is width*height*4 bytes, RGBA, premultiplied alpha as produced by a NanoVG
framebuffer readback. Alpha is never modified.

`downsample` is the factor by which the regional luminance estimate is computed at
reduced resolution. The estimate is low-frequency by construction, so 4 costs about a
sixteenth of full resolution and changes the result imperceptibly. Must be >= 1.

If `stats` is non-NULL it receives measurements of the *input* image.
*/
void panelFilterApply(uint8_t* data, int width, int height,
	const PanelFilterParams& params, int downsample = 4, PanelStats* stats = NULL);


/** Derives a per-panel attenuation strength from measured statistics, in 0-1 where 0
means leave the panel alone and 1 means apply the full configured attenuation.

A mostly-white panel gets the full treatment; an already-dark one gets almost none.
*/
float panelFilterAutoStrength(const PanelStats& stats);


/** Edge-aware blur of a single-channel image, exposed for testing.

Gastal-Oliveira domain transform recursive filter, three iterations. `lum` is
width*height values in the 0-255 range and is filtered in place.

NOT a Gaussian, and must not be replaced by one: see design/panel-dimming.md for the
two artefacts that caused.
*/
void panelFilterEdgeAwareBlur(float* lum, int width, int height, float sigmaS,
	float edgeSensitivity = 0.2f);


} // namespace window
} // namespace rack
