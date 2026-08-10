# Cable appearance: colour by destination, and flow direction

Two changes to how cables read, both ported from Wcoast.

## Colour by destination

A new cable takes the colour of the port it plugs INTO. A cable leaving a yellow audio
output and landing in a blue gate input is blue, because what matters when you follow a
cable is what it is feeding.

**Set at connect time, not computed at draw time.** This is the important design decision.
Computing the colour every frame from the destination would be simpler, but it would
silently override Rack's existing cable-colour feature forever — the palette in settings,
the per-cable colour saved in the patch, the user's right to recolour a cable. Writing the
colour once, at the moment the connection is made, uses that feature rather than replacing
it: the colour persists in the patch and can still be changed afterwards.

Applied at both connect sites, the click path and the drag path, so the two cannot
disagree. A cable in flight has no destination yet, so it wears its source's colour until
it lands.

Colour resolution is shared with the drawn jacks through
`appearance::portColor` — a definition-file override first, then the name heuristic. A
cable can therefore never disagree with the jack it is plugged into.

## Flow direction: marching ants

Black dashes crawl along the cable from source to destination, at half the cable's width,
butt-capped. Dash length is keyed to the **destination's** signal family, ported from
Wcoast's `FLOW_DASH`:

| Family | Dash length (cable widths) |
| --- | --- |
| trigger / gate | 5.6 |
| control (CV) | 3.4 |
| pitch | 3.4 |
| audio | 1.6 |

Gap between dashes is 2.6 cable widths (`FLOW_GAP`). The crawl is 5.5 mm/s
(`FLOW_SPEED`) — converted here to about 16 px/s, since Rack works in its own pixels at 75
DPI.

**The crawl is not synchronised with the signal.** It states direction and nothing more.
Do not be tempted to drive it from audio levels; that was never the intent.

### nanovg has no dashes

Not one mention in the header. What is a single `stroke-dasharray` attribute in SVG has to
be built by hand: sample the quadratic curve into a polyline, accumulate arc length, then
emit each "on" stretch as its own stroke with a time-driven phase offset.

48 samples is plenty — dashes are short relative to the curve, so the error within one
dash is far below a pixel. Each dash follows the polyline vertices it spans rather than
cutting a straight chord, so dashes on a steeply bent cable do not cut the corner.

Only complete cables get dashes, since the dash length comes from a destination that a
carried cable does not yet have.

Cables are not framebuffered, so they already redraw every frame and the animation costs
nothing extra in invalidation. The per-frame work is one 48-point sampling and a handful of
short strokes per cable.

## Settings

`cableAutoColor` and `cableFlowDashes`, both on by default, in the View menu under Cables
alongside the click-to-connect toggle.
