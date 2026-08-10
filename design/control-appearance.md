# Control appearance: drawn knobs and jacks

Modification 6. Replaces every module's knob and jack graphics with drawn controls, so
they are legible and consistent regardless of which developer's artwork is underneath.

This exists because recolouring cannot solve the two actual problems. Knobs on dark
panels are black on black — no colour mapping distinguishes them from their background,
because there is nothing there to map. And Rack's knob art carries no tick ring, so no
transform can produce one. Drawing them ourselves does both, and gives one visual
language across every plugin rather than fifty.

It also makes an earlier idea unnecessary: with jack graphics replaced outright, the
dazzling white rings simply cease to exist. No lightness ceiling on component SVGs is
needed. Panel brightness reduction (`design/panel-dimming.md`) is unaffected and
continues to handle panel backgrounds.


## Provenance

The visual language is ported from Wcoast: `panel/primitives.js` for the drawing
(`knob()`, `jack()`) and `host/panel-loader.js` for the colour code and direction ring
(`paintJack`, `addDirRing`). Read those before changing the look here.

Its governing principle, quoted from that source, is worth keeping in front of us:

> Shape, not colour: a jack told apart only by hue is a jack that cannot be told apart in
> peripheral vision, under magnification, or by anyone whose colour vision differs.

So **colour carries signal family and shape carries direction.** Never fold one into the
other.


## Jacks

A coloured disc, a dark centre hole, and a bold dashed ring whose *position* states
direction:

- **Output**: dashes hug the outer edge of the coloured band.
- **Input**: dashes hug the hole.

The band is the distance from hole to rim; the ring is a third of that band wide, with
equal dash and gap, and the dash count comes from the circumference so the rhythm stays
even at any size.

### Direction needs no authoring, ever

`PortWidget` already carries its type, so input versus output is known for every port on
every module from every developer. This is the highest-value part of the feature and it
has no maintenance cost. It must never be moved into a definition file.

### Family colours

From Wcoast, unchanged:

| Family | Colour | |
| --- | --- | --- |
| audio | `#f3c40b` | yellow |
| cv | `#ff7300` | orange |
| trigger | `#5aa0e6` | light blue |
| pitch | `#39a85a` | green |
| hole | `#2f2f33` | centre plug hole |
| ring | `#000000` | direction dashes |

### Family is guessed, then corrected

Rack has no concept of signal family, but it does expose port names. So the family is
guessed from the name — pitch for 1V/OCT and PITCH, trigger for GATE, TRIG, CLOCK and
RESET, cv for CV and MOD, audio otherwise — and a definition file corrects what the
guess gets wrong. Guessing first is what makes the feature useful on a rack full of
plugins nobody has authored a definition for.


## Knobs

Layered as in Wcoast's `knob()`:

1. Rim disc, radial gradient, with a rim outline.
2. Cap disc at 72% of the radius.
3. Tick marks, default 7, spanning slightly inside to slightly outside the rim.
4. A pointer from centre to cap edge.

**The ticks rotate with the pointer.** They are grip texture, not a calibration scale —
that was the detail most likely to be got wrong by looking at a picture. Wcoast's static
calibration marks are a separate thing (`dialScale`) and are out of scope here.

### Geometry follows the host, not Wcoast

The drawn knob fits whatever box the plugin's knob occupies, and honours that knob's own
`minAngle` and `maxAngle`, so a knob still sweeps through the arc its developer intended.
Forcing Wcoast's fixed radius and 300 degree sweep onto Rack would misreport values and
break layouts. Only the *look* is borrowed, not the measurements.


## Definitions, not right-clicking

Appearance is declared per module in a text file, not chosen through a context menu. A
file is editable, diffable, reviewable and can be authored in bulk; right-clicking is
fine for one knob and hopeless for a rack full of them.

One JSON file per module, under `appearance/<PluginSlug>/<ModuleSlug>.json`. A new
top-level folder, deliberately clear of `res/` and anything licensed. Missing files are
normal, not an error: the automatic defaults cover everything.

```json
{
  "knobs": {
    "0":  { "color": "#1688cc", "ticks": 7 },
    "FREQ": { "color": "#39a85a" }
  },
  "ports": {
    "in:0":  { "family": "pitch" },
    "out:0": { "color": "#e0359b" }
  }
}
```

Knobs are keyed by parameter id, ports by direction and port id, since input 0 and
output 0 are different ports. Either may name a `family` from the table above or an
explicit `color`. Anything unspecified falls back to the automatic behaviour.

Layering, in precedence order: automatic default, then the module's definition file.
Nothing else. If per-knob clicking is ever wanted it sits on top of this, not instead
of it.

### Reload without restarting

A menu command re-reads every definition file. Iterating on how these look is the whole
job at first, and restarting Rack for each attempt would make that unbearable.


## Where it is drawn, and the ABI trap that decided it

Drawn from the body of `ModuleWidget::draw`, immediately after `Widget::draw(args)` has
painted the module's own children. It walks the descendants, finds each `Knob` and
`PortWidget`, and draws over them.

**The obvious alternative is a trap.** Overriding `draw()` on `Knob` looks right and is
even layout-safe, since `draw` is already virtual so no vtable slot is added. But a
plugin's knob subclass had its vtable generated by *its* compiler, so its inherited draw
slot still points at the implementation that existed then. Our override would never run
for third-party knobs — precisely the ones that matter. This is a sharper form of the
inline rule in CLAUDE.md: **overriding an existing virtual is ABI-safe but does not reach
existing plugin subclasses.** Only code inside Rack's own non-inline functions reliably
runs for every plugin.

`ModuleWidget::draw` is such a function, which is why it is the hook.

Other ABI notes, all consistent with CLAUDE.md: the implementation lives in
`src/app/controlAppearance.{hpp,cpp}` with a private header not in `include/`; no new
members, virtuals or layout changes on any SDK class; the only touched existing bodies
are `ModuleWidget::draw` and the menu.


## Known limits

- Only knobs deriving from `Knob` and ports deriving from `PortWidget` are covered, which
  is nearly all of them. A plugin hand-drawing a knob in code is not.
- Sliders, switches and buttons are untouched. They are the obvious next step and want
  their own shapes, not a circle.
- Drawing over the original art flattens genuinely well-designed knobs into the same
  generic look. That is the deliberate trade: uniform and legible beats varied and
  invisible.
- Lights and displays are never covered. Only knobs and ports.
