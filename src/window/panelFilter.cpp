/** Panel brightness reduction — see design/panel-dimming.md

Ported from GXW src/imageTransform.js. Depends on nothing from Rack so that it can be
compiled and tested standalone; keep it that way.
*/
#include "panelFilter.hpp"

#include <cmath>
#include <vector>
#include <algorithm>


namespace rack {
namespace window {


// Iterations of the recursive filter. Three is the paper's standard recommendation: a
// much closer approximation to true Gaussian smoothing within smooth regions than one
// pass, at a cost small enough not to matter for our cached, per-panel use.
static const int NUM_ITERATIONS = 3;

// Ceiling on the per-pixel RGB scale factor. Without it, a nearly-black pixel whose
// regional estimate is bright could be handed an enormous ratio by the contrast gain
// and explode into a coloured speck.
static const float MAX_SCALE = 4.f;


static inline float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}


/** Loose Rec. 709 weights applied directly to gamma-encoded sRGB. The small
non-linearity of skipping linearisation is absorbed by threshold tuning, and the saved
per-pixel pow() keeps the inner loops tight.

Reads premultiplied RGB as-is. Panels are effectively opaque rectangles — only a
hairline of antialiased border is not — so un-premultiplying would buy nothing and would
need an arbitrary answer for fully transparent pixels.
*/
static inline float luminanceOf(const uint8_t* p) {
	return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}


void panelFilterEdgeAwareBlur(float* J, int width, int height, float sigmaS,
	float edgeSensitivity) {

	if (width < 2 || height < 2 || sigmaS <= 0.f)
		return;

	const size_t n = (size_t) width * height;

	// sigma_r is specified in 0-1 terms in the paper; we work in 0-255, so scale up.
	const float sigmaR = clampf(edgeSensitivity, 0.01f, 10.f) * 255.f;
	const float ratio = sigmaS / sigmaR;
	const int N = NUM_ITERATIONS;

	// The domain-transform increments are computed once from the SOURCE luminance and
	// reused across iterations. Only the carry coefficients change per iteration,
	// because the recursive filter's alpha changes with each iteration's sigma_H.
	std::vector<float> source(J, J + n);

	// dRow[i] is the transformed distance between pixel i-1 and pixel i within their
	// shared row: 1 plus a luminance-gradient contribution. Column 0 of each row is
	// left at 0 and never read, since the passes start at x=1.
	std::vector<float> dRow(n, 0.f);
	for (int y = 0; y < height; y++) {
		const size_t off = (size_t) y * width;
		for (int x = 1; x < width; x++) {
			const size_t i = off + x;
			dRow[i] = 1.f + ratio * std::fabs(source[i] - source[i - 1]);
		}
	}

	// Same along columns, with the first row left at 0.
	std::vector<float> dCol(n, 0.f);
	for (int y = 1; y < height; y++) {
		const size_t off = (size_t) y * width;
		for (int x = 0; x < width; x++) {
			const size_t i = off + x;
			dCol[i] = 1.f + ratio * std::fabs(source[i] - source[i - (size_t) width]);
		}
	}

	// V[i] = a^d[i], the per-pixel carry coefficient for the current iteration.
	std::vector<float> V(n, 0.f);

	for (int iter = 1; iter <= N; iter++) {
		// Paper Eq. 14: progressively smaller spatial scales across iterations.
		const float sigmaH = sigmaS * std::sqrt(3.f) *
			std::pow(2.f, (float) (N - iter)) / std::sqrt(std::pow(4.f, (float) N) - 1.f);
		// a^d == exp(d * ln a), and expf is cheaper than powf in a hot loop.
		const float lnA = -std::sqrt(2.f) / sigmaH;

		// --- Row direction ---
		for (size_t i = 0; i < n; i++)
			V[i] = std::exp(dRow[i] * lnA);

		for (int y = 0; y < height; y++) {
			const size_t off = (size_t) y * width;
			// Left to right. V[i] carries the step from column i-1 to column i.
			for (int x = 1; x < width; x++) {
				const size_t i = off + x;
				J[i] += V[i] * (J[i - 1] - J[i]);
			}
			// Right to left. V[i+1] carries the step between columns i and i+1, which
			// is the same distance travelled backwards.
			for (int x = width - 2; x >= 0; x--) {
				const size_t i = off + x;
				J[i] += V[i + 1] * (J[i + 1] - J[i]);
			}
		}

		// --- Column direction ---
		for (size_t i = 0; i < n; i++)
			V[i] = std::exp(dCol[i] * lnA);

		const size_t w = (size_t) width;
		for (int x = 0; x < width; x++) {
			for (int y = 1; y < height; y++) {
				const size_t i = (size_t) y * w + x;
				J[i] += V[i] * (J[i - w] - J[i]);
			}
			for (int y = height - 2; y >= 0; y--) {
				const size_t i = (size_t) y * w + x;
				J[i] += V[i + w] * (J[i + w] - J[i]);
			}
		}
	}
}


/** Box-average `lum` down by `factor`, writing a (dw x dh) image. */
static void downsampleLum(const float* lum, int width, int height, int factor,
	std::vector<float>& out, int& dw, int& dh) {

	dw = std::max(1, (width + factor - 1) / factor);
	dh = std::max(1, (height + factor - 1) / factor);
	out.assign((size_t) dw * dh, 0.f);

	for (int dy = 0; dy < dh; dy++) {
		const int y0 = dy * factor;
		const int y1 = std::min(height, y0 + factor);
		for (int dx = 0; dx < dw; dx++) {
			const int x0 = dx * factor;
			const int x1 = std::min(width, x0 + factor);
			float sum = 0.f;
			int count = 0;
			for (int y = y0; y < y1; y++) {
				const float* row = lum + (size_t) y * width;
				for (int x = x0; x < x1; x++) {
					sum += row[x];
					count++;
				}
			}
			out[(size_t) dy * dw + dx] = count ? sum / count : 0.f;
		}
	}
}


/** Bilinear sample of a (dw x dh) image at full-resolution pixel (x, y). */
static inline float sampleUpscaled(const std::vector<float>& small, int dw, int dh,
	int x, int y, int factor) {

	// Centre of full-res pixel (x, y) in small-image coordinates.
	const float sx = ((float) x + 0.5f) / factor - 0.5f;
	const float sy = ((float) y + 0.5f) / factor - 0.5f;

	int x0 = (int) std::floor(sx);
	int y0 = (int) std::floor(sy);
	const float fx = sx - x0;
	const float fy = sy - y0;
	int x1 = x0 + 1;
	int y1 = y0 + 1;

	x0 = std::max(0, std::min(dw - 1, x0));
	x1 = std::max(0, std::min(dw - 1, x1));
	y0 = std::max(0, std::min(dh - 1, y0));
	y1 = std::max(0, std::min(dh - 1, y1));

	const float v00 = small[(size_t) y0 * dw + x0];
	const float v10 = small[(size_t) y0 * dw + x1];
	const float v01 = small[(size_t) y1 * dw + x0];
	const float v11 = small[(size_t) y1 * dw + x1];

	const float top = v00 + (v10 - v00) * fx;
	const float bottom = v01 + (v11 - v01) * fx;
	return top + (bottom - top) * fy;
}


void panelFilterApply(uint8_t* data, int width, int height,
	const PanelFilterParams& params, int downsample, PanelStats* stats) {

	if (!data || width <= 0 || height <= 0)
		return;
	if (downsample < 1)
		downsample = 1;

	const size_t n = (size_t) width * height;

	// Per-pixel luminance of the original.
	std::vector<float> lum(n);
	for (size_t i = 0; i < n; i++)
		lum[i] = luminanceOf(data + i * 4);

	// Regional luminance estimate: edge-aware blur, computed at reduced resolution
	// because the estimate is low-frequency by construction.
	std::vector<float> small;
	int dw = 0, dh = 0;
	downsampleLum(lum.data(), width, height, downsample, small, dw, dh);
	panelFilterEdgeAwareBlur(small.data(), dw, dh, params.blurRadius / downsample,
		params.edgeSensitivity);

	const float threshold = clampf(params.threshold, 0.f, 0.999f);
	const float denom = std::max(1e-6f, 1.f - threshold);
	const float attenRange = 1.f - clampf(params.maxAttenuation, 0.f, 1.f);
	const float gain = params.contrastGain;
	const float limit = std::max(0.f, params.contrastLimit);

	double lumSum = 0.0;
	size_t opaqueCount = 0;
	size_t brightCount = 0;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const size_t i = (size_t) y * width + x;
			uint8_t* p = data + i * 4;

			const float regional = sampleUpscaled(small, dw, dh, x, y, downsample);
			const float lumValue = lum[i];

			if (p[3] > 0) {
				opaqueCount++;
				lumSum += lumValue;
				if (regional / 255.f > threshold)
					brightCount++;
			}

			// Regional dimming: smoothstep from threshold up to full brightness.
			float factor = 1.f;
			const float r = regional / 255.f;
			if (r > threshold) {
				float t = clampf((r - threshold) / denom, 0.f, 1.f);
				const float s = t * t * (3.f - 2.f * t);
				factor = 1.f - s * attenRange;
			}

			// Local contrast gain: amplify this pixel's deviation from its
			// neighbourhood. This is what rescues low-contrast legend text, and it is
			// applied before the dimming so the two compose predictably.
			float target = lumValue;
			if (gain > 0.f) {
				const float delta = clampf(gain * (lumValue - regional), -limit, limit);
				target = lumValue + delta;
			}
			target *= factor;

			if (lumValue <= 0.5f) {
				// Black stays black: a multiplicative scale cannot lift it, and trying
				// would only amplify noise.
				continue;
			}

			// Scale RGB by the luminance ratio, which preserves hue and saturation.
			const float scale = clampf(target / lumValue, 0.f, MAX_SCALE);
			for (int c = 0; c < 3; c++) {
				const float v = p[c] * scale;
				p[c] = (uint8_t) clampf(v, 0.f, 255.f);
			}
			// Alpha is never touched.
		}
	}

	if (stats) {
		stats->meanLuminance = opaqueCount ? (float) (lumSum / opaqueCount) / 255.f : 0.f;
		stats->brightFraction = opaqueCount ? (float) brightCount / opaqueCount : 0.f;
	}
}


float panelFilterAutoStrength(const PanelStats& stats) {
	// Corrected against real measurements — see design/panel-dimming.md. The first
	// version scaled strength up with how much of the panel was bright and tempered it
	// by overall lightness. Measured on actual dark-themed panels, both parts were
	// wrong: Core/AudioInterface2 and Fundamental/VCO came in at a mean luminance of
	// 0.21 with only a tenth of their area bright, and so were left untouched. But that
	// tenth is precisely what dazzles — bright jack surrounds on an otherwise dark
	// panel. Judging a panel by its average is judging it by the part that does not
	// hurt.
	//
	// The threshold parameter already spares dark regions, pixel by pixel and far more
	// precisely than a whole-panel average ever could. So this only needs to answer a
	// narrower question: is there any bright region here worth acting on?
	//
	// Below one percent, no: that is a stray highlight or an antialiased edge, and
	// filtering would cost time for nothing. Above five percent, full configured
	// strength, and let the threshold decide which pixels it touches.
	const float lo = 0.01f;
	const float hi = 0.05f;
	float t = clampf((stats.brightFraction - lo) / (hi - lo), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}


} // namespace window
} // namespace rack
