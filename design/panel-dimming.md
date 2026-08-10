# Panel brightness reduction

Modification 1. Makes Rack usable with low vision by attacking two problems at once:
large bright areas that dazzle, and legend text whose contrast against its own panel is
too low to read. Both are solved from a single computed quantity, which is why they
belong in one feature rather than two.

## Provenance

The algorithm is ported from GXW, `src/imageTransform.js`, spec'd in that project's
DESIGN.md section 26, where it dims imported canvas images. Read that file before
changing anything here. Its tuned defaults are blur radius 50 pixels, threshold 0.5,
maximum attenuation 0.5.

**Do not replace the edge-aware blur with a plain Gaussian.** GXW started with a
Gaussian via a canvas filter and it failed in two specific ways: faint halos around
high-contrast boundaries, and uneven dimming across what should have been one
continuous bright region, because bright pixels near a dark shape had their regional
estimate dragged down by that shape and so received less dimming than bright pixels
further away. The fix was the Gastál–Oliveira domain transform recursive filter, three
iterations. Reference: Gastál & Oliveira (2011), *Domain Transform for Edge-Aware Image
and Video Processing*, SIGGRAPH 2011, equations 11, 14 and 21.


## The core idea

Dim by *region*, not by pixel. A conventional brightness control cannot tell a huge
white bezel from a small white legend, because both are white. This transform can,
because the amount of dimming applied to a pixel is driven by the brightness of its
neighbourhood rather than its own.

1. Compute per-pixel luminance (Rec. 709 weights on gamma-encoded sRGB — the
   non-linearity is absorbed by threshold tuning, and skipping linearisation keeps the
   inner loops tight).
2. Blur that luminance with the edge-aware filter. The result is the **regional
   luminance estimate**: how bright the area around each pixel is.
3. Apply two corrections to the *original, unblurred* pixel, both driven by the
   regional estimate. Because the multiplier comes from the neighbourhood but is
   applied to the sharp pixel, local contrast survives the dimming.

Blurring is also what lets a cluster of small bright things count as one bright region,
which is the reason for choosing the pixel domain over a cheaper vector-domain approach:
the vector approach cannot see clusters at all.


## The edge-sensitivity dial, and why it matters more here than in GXW

This is the least obvious thing in the feature, and it was found by testing rather than
by reading. **Edge respect and size sensitivity are in direct tension**, and the
`edgeSensitivity` parameter is the dial between them.

The "small bright features stay bright" property comes from the blur *averaging a small
feature down* toward its dark surround. That is what a plain Gaussian does. But an
edge-aware filter deliberately refuses to bridge a high-contrast boundary, so a small
bright dot on a dark field keeps its own high value as its regional estimate — and
therefore gets dimmed exactly like a large bright region.

So the upgrade that fixed GXW's halos also traded away some of the size sensitivity. On
photographs that barely shows, because edges there are soft and gradual. Panel artwork
is hard-edged, so here it dominates.

Measured on a synthetic panel, both behaviours are real and reachable:

| edgeSensitivity | small bright dot on dark | large bright field |
| --- | --- | --- |
| 0.2 (GXW's photographic default) | 245 → 155, dimmed | 245 → 124, dimmed |
| 2.0 (gaussian-ish) | 245 → 245, untouched | 245 → 126, dimmed |

Both rows are asserted by the test, so a future change cannot silently break either.

Which is wanted depends on the complaint. Clusters of bright jacks that need taming want
the low value, where everything bright gets dimmed regardless of size. A panel whose
small indicators must stay legible while its background is tamed wants the high value.
Expose it as a tuning control rather than picking one.

One consequence of the downsampling below: box-averaging softens hard edges slightly
before the edge-aware filter sees them, which very slightly weakens edge respect. That
is why a uniform dark field can shift by a unit or two rather than being bit-identical.

### Correction one: regional dimming

Where the regional estimate is bright, scale the pixel down. Below `threshold`, nothing
happens at all. Above it, a smoothstep ramp reaches `maxAttenuation` at full brightness,
so there is no visible edge where the effect switches on.

### Correction two: local contrast gain

Where a pixel differs only slightly from its regional estimate, amplify that
difference. Grey legend text on a grey panel is exactly "a small deviation from the
local average", so pushing it away from that average is what makes it legible.

Formally this is unsharp masking on luminance. Doing it against the *edge-aware* blur
rather than a Gaussian is what stops it ringing — the same reason the upgrade was worth
making for the dimming.

Apply the gain to luminance only and leave hue alone, so blue text on a blue panel
separates without going grey. The gain needs a ceiling: it also amplifies JPEG
artefacts in raster panels, which reads as grit.


## Where it hooks in

`SvgPanel` (`include/app/SvgPanel.hpp`) owns a child `FramebufferWidget`, so every
module's panel artwork is already rendered into its own texture, per module instance.
That is the hook: after `FramebufferWidget::render()` completes, if this framebuffer
belongs to a panel, read it back, filter it, and keep the filtered result.

Three reasons this is the right place rather than the SVG load path:

- **Per-panel strength is only possible here.** The SVG cache is keyed by filename, and
  component-library graphics are *shared objects* — one cached jack graphic is drawn on
  hundreds of panels. Anything done at the SVG level is global by construction.
- **It covers raster panels.** By this stage everything is pixels, so a PNG panel is
  treated identically to a vector one. There is no shape list to inspect and no need
  for one.
- **It sees the composite**, so clusters register as regions.

### What this hook does NOT cover, and the plan for it

Jacks, knobs and screws are children of the `ModuleWidget`, **siblings of the panel,
not inside its framebuffer**. So they are not in the filtered image. If the dazzle
turns out to be the jack surrounds rather than the panel artwork, that is a second,
separate job with a different shape: those graphics are shared across every plugin and
are individually tiny, so a regional estimate is meaningless for them. They want a
global lightness ceiling applied in the SVG colour path instead — one rule for all
component graphics, no per-panel variation. Keep the two jobs separate; they have
different data, different scope and different controls.

Plugins that build a custom panel widget instead of using `SvgPanel` will not be
covered by the v1 gate. Widening it later is a one-line change.


## Caching and cost

The domain transform is inherently sequential along rows and then columns, so it cannot
be a shader. It runs on the CPU, which makes caching the whole design.

- A panel's artwork is **static**. So the filtered image is computed once per widget per
  framebuffer size and reused. Invalidate on size change, on theme change, and on
  parameter change.
- Dragging a module does not invalidate anything: `ModuleWidget` already disables
  subpixel framebuffer redraws while dragging (see the HACK comment around
  `ModuleWidget.cpp:446`). Zoom does change resolution, and is the case to watch.
- The regional estimate is low-frequency by definition, so compute it at reduced
  resolution and upsample. Measured on this machine, per filter run:

  | panel (retina pixels) | full res | quarter res | eighth res |
  | --- | --- | --- | --- |
  | 10HP, 300x760 | 11.9 ms | 2.1 ms | 1.7 ms |
  | 20HP, 600x760 | 21.8 ms | 4.3 ms | 3.1 ms |
  | 40HP, 1200x760 | 57.9 ms | 8.0 ms | 6.6 ms |
  | 80HP, 2400x760 | 103.2 ms | 16.0 ms | 12.6 ms |

  Quarter resolution is the default: full resolution would visibly hitch on a wide
  panel, and eighth buys little beyond it because by then the remaining cost is the
  full-resolution per-pixel apply loop, not the blur.
- Prefer `expf` over `powf` in the carry-coefficient inner loop.


## Settings

Four parameters, global defaults with per-panel overrides:

| Parameter | Meaning | Default |
| --- | --- | --- |
| enabled | master switch | on |
| blurRadius | spatial scale of the regional estimate, in pixels | 50 |
| threshold | regional luminance below which nothing is dimmed, 0–1 | 0.5 |
| maxAttenuation | multiplier at maximum regional luminance; 0.5 halves it, 1.0 disables | 0.5 |
| contrastGain | local contrast amplification; 0 disables | tune by eye |

### Per-panel storage

Rack already persists per-module state keyed by plugin slug then module slug — see
`settings::moduleInfos` in `include/settings.hpp`. Use the same key shape, but in a
**separate map**, not by adding fields to `ModuleInfo`: that struct is in the SDK header,
and a brand-new global has zero ABI surface. Persist in settings.json alongside the
rest.

### Auto-default per panel

Hand-tuning hundreds of modules from dozens of developers is not realistic, so a
panel's strength defaults to something *measured*, not to the global value. On first
filter, the panel's own statistics are computed and a starting strength derived.

**The obvious version of this rule is wrong, and measurement caught it.** The first
attempt scaled strength up with how much of the panel was bright, tempered by overall
lightness, reasoning that a white slab should be dimmed hard and an already-dark panel
barely at all. Real numbers killed it. With dark panels enabled, Core/AudioInterface2
measured a mean luminance of 0.21 with 10% of its area bright, and Fundamental/VCO 0.22
with 11%. Both were therefore left completely untouched — yet that 10% is exactly what
dazzles: bright jack surrounds on an otherwise dark panel. **Judging a panel by its
average is judging it by the part that does not hurt.**

The `threshold` parameter already spares dark regions, pixel by pixel, far more precisely
than any whole-panel average could. So the auto rule only has to answer a much narrower
question: is there any bright region here worth acting on at all? Below 1% of the area,
no — that is a stray highlight or an antialiased edge, and filtering would cost time for
nothing. Above 5%, full configured strength, and let the threshold decide which pixels
get touched.

Store an override only for panels where the user actually disagrees. That makes
per-panel tuning an exception rather than a chore, which is the difference between a
feature that gets used and one that does not.


## ABI notes

Everything here obeys the rules in CLAUDE.md:

- The filter lives in its own files under `src/window/`, with a **private** header that
  is deliberately not in `include/`, so none of it becomes plugin API surface.
- No new members on `FramebufferWidget`, `SvgPanel` or any other SDK class. The cached
  filtered image lives in a side table in the filter's own translation unit, keyed by
  widget pointer, cleaned up from the framebuffer destructor and on context destroy.
- No new virtual methods anywhere. The hook is a call added to the body of an existing
  non-inline function.
- The gate uses `dynamic_cast` on an already-polymorphic type, which changes no layout.


## Build order

1. The pure filter, with no wiring: luminance, edge-aware blur, both corrections.
   Verify numerically on a synthetic image before it touches the render path — a large
   bright field must be dimmed, a small bright dot on dark must not be, and a
   low-contrast grey-on-grey patch must gain separation.
2. Settings globals and persistence.
3. The render-path hook and the image cache.
4. Menu UI: global defaults in the View menu, per-panel overrides in the module's
   right-click menu.
5. Tune the defaults by eye against real panels, including a deliberately bright
   third-party one.
