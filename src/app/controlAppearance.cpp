/** Drawn knobs and jacks — see design/control-appearance.md

The visual language comes from Wcoast. Its governing rule, worth keeping in view:
colour carries signal family, shape carries direction. A jack told apart only by hue
cannot be told apart in peripheral vision, under magnification, or by anyone whose colour
vision differs. Never fold one into the other.
*/
#include "controlAppearance.hpp"

#include <app/ModuleWidget.hpp>
#include <app/Knob.hpp>
#include <app/PortWidget.hpp>
#include <plugin/Model.hpp>
#include <plugin/Plugin.hpp>
#include <engine/ParamQuantity.hpp>
#include <engine/PortInfo.hpp>
#include <settings.hpp>
#include <context.hpp>
#include <asset.hpp>
#include <system.hpp>
#include <string.hpp>

#include <jansson.h>
#include <map>
#include <string>
#include <vector>
#include <cmath>


namespace rack {
namespace appearance {


// ---- The Wcoast colour code. One colour per SIGNAL FAMILY, the same for an input and an
// output, because direction is carried by the dashed ring instead. ----

static NVGcolor familyColor(const std::string& family) {
	if (family == "audio")   return nvgRGB(0xf3, 0xc4, 0x0b);   // yellow
	if (family == "cv")      return nvgRGB(0xff, 0x73, 0x00);   // orange
	if (family == "trigger") return nvgRGB(0x5a, 0xa0, 0xe6);   // light blue
	if (family == "pitch")   return nvgRGB(0x39, 0xa8, 0x5a);   // green
	if (family == "luma")    return nvgRGB(0xba, 0xba, 0xb6);   // video, one channel
	if (family == "rgb")     return nvgRGB(0xe0, 0x35, 0x9b);   // video, three channels
	return nvgRGB(0xf3, 0xc4, 0x0b);
}

static const NVGcolor JACK_HOLE = nvgRGB(0x2f, 0x2f, 0x33);
static const NVGcolor JACK_RING = nvgRGB(0x00, 0x00, 0x00);

// Knob defaults, matching Wcoast's blueRing gradient and cap proportions.
static const NVGcolor KNOB_RIM_INNER = nvgRGB(0x16, 0x88, 0xcc);
static const NVGcolor KNOB_RIM_OUTER = nvgRGB(0x00, 0x3d, 0x62);
static const NVGcolor KNOB_RIM_STROKE = nvgRGB(0x6f, 0xa8, 0xd6);   // dark theme ringStroke
static const float KNOB_CAP_RATIO = 0.72f;
static const int KNOB_DEFAULT_TICKS = 7;


/** Parses "#rrggbb". Returns false and leaves the colour alone if it cannot. */
static bool parseHexColor(const std::string& s, NVGcolor& out) {
	if (s.size() != 7 || s[0] != '#')
		return false;
	unsigned r, g, b;
	if (std::sscanf(s.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3)
		return false;
	out = nvgRGB((uint8_t) r, (uint8_t) g, (uint8_t) b);
	return true;
}


struct ControlStyle {
	bool hasColor = false;
	NVGcolor color = nvgRGB(0, 0, 0);
	int ticks = KNOB_DEFAULT_TICKS;
};

/** One module's authored appearance. Missing entries are normal — the automatic defaults
cover everything, and a definition file only corrects what it needs to. */
struct ModuleDefinition {
	/** Keyed by parameter id as text, so a file can also key by name later. */
	std::map<std::string, ControlStyle> knobs;
	/** Keyed by "in:N" or "out:N", because input 0 and output 0 are different ports. */
	std::map<std::string, ControlStyle> ports;
};

/** pluginSlug/moduleSlug -> definition. An entry that exists but is empty means "looked,
found nothing", which stops us stat-ing the disk on every frame. */
static std::map<std::string, ModuleDefinition> definitions;
static std::map<std::string, bool> definitionLoaded;


static std::string definitionDir() {
	return system::join(asset::systemDir, "appearance");
}


static void parseStyle(json_t* styleJ, ControlStyle& style) {
	if (!styleJ || !json_is_object(styleJ))
		return;
	json_t* familyJ = json_object_get(styleJ, "family");
	if (familyJ && json_is_string(familyJ)) {
		style.color = familyColor(json_string_value(familyJ));
		style.hasColor = true;
	}
	// An explicit colour wins over a family name, since it is more specific.
	json_t* colorJ = json_object_get(styleJ, "color");
	if (colorJ && json_is_string(colorJ)) {
		if (parseHexColor(json_string_value(colorJ), style.color))
			style.hasColor = true;
	}
	json_t* ticksJ = json_object_get(styleJ, "ticks");
	if (ticksJ && json_is_number(ticksJ))
		style.ticks = math::clamp((int) json_number_value(ticksJ), 0, 64);
}


static const ModuleDefinition* getDefinition(const std::string& pluginSlug, const std::string& moduleSlug) {
	const std::string key = pluginSlug + "/" + moduleSlug;

	auto loadedIt = definitionLoaded.find(key);
	if (loadedIt != definitionLoaded.end()) {
		auto it = definitions.find(key);
		return it == definitions.end() ? NULL : &it->second;
	}
	definitionLoaded[key] = true;

	// Announce the search root once, so authoring a definition does not require guessing
	// the path. Per-module misses are silent on purpose: a module without a definition is
	// the normal case, not a problem, and logging each one would bury everything else.
	static bool announced = false;
	if (!announced) {
		announced = true;
		INFO("Appearance: definitions read from %s/<PluginSlug>/<ModuleSlug>.json", definitionDir().c_str());
	}

	const std::string path = system::join(definitionDir(), pluginSlug, moduleSlug + ".json");
	if (!system::isFile(path))
		return NULL;

	json_error_t error;
	json_t* rootJ = json_load_file(path.c_str(), 0, &error);
	if (!rootJ) {
		WARN("Appearance: %s has invalid JSON at %d:%d %s", path.c_str(), error.line, error.column, error.text);
		return NULL;
	}
	DEFER({json_decref(rootJ);});

	ModuleDefinition def;

	json_t* knobsJ = json_object_get(rootJ, "knobs");
	if (knobsJ && json_is_object(knobsJ)) {
		const char* id;
		json_t* styleJ;
		json_object_foreach(knobsJ, id, styleJ) {
			ControlStyle style;
			parseStyle(styleJ, style);
			def.knobs[id] = style;
		}
	}

	json_t* portsJ = json_object_get(rootJ, "ports");
	if (portsJ && json_is_object(portsJ)) {
		const char* id;
		json_t* styleJ;
		json_object_foreach(portsJ, id, styleJ) {
			ControlStyle style;
			parseStyle(styleJ, style);
			def.ports[id] = style;
		}
	}

	INFO("Appearance: loaded %s (%d knobs, %d ports)", path.c_str(),
		(int) def.knobs.size(), (int) def.ports.size());
	definitions[key] = def;
	return &definitions[key];
}


void reloadDefinitions() {
	definitions.clear();
	definitionLoaded.clear();
	INFO("Appearance: definitions cleared, will reload on next draw");
}


/** Guesses a port's signal family from its name.

Rack has no concept of signal family, but it does expose port names. Guessing first is
what makes this useful on a rack full of plugins nobody has authored a definition for; a
definition file then corrects whatever the guess gets wrong.
*/
static std::string guessFamily(const std::string& name) {
	const std::string n = string::uppercase(name);
	if (n.find("1V/OCT") != std::string::npos || n.find("V/OCT") != std::string::npos
		|| n.find("PITCH") != std::string::npos || n.find("NOTE") != std::string::npos)
		return "pitch";
	if (n.find("GATE") != std::string::npos || n.find("TRIG") != std::string::npos
		|| n.find("CLOCK") != std::string::npos || n.find("CLK") != std::string::npos
		|| n.find("RESET") != std::string::npos || n.find("SYNC") != std::string::npos)
		return "trigger";
	if (n.find("CV") != std::string::npos || n.find("MOD") != std::string::npos
		|| n.find("FM") != std::string::npos || n.find("AM") != std::string::npos)
		return "cv";
	return "audio";
}


/** Draws one jack: coloured disc, dark hole, and a dashed ring whose POSITION states
direction — hugging the outer edge of the band for an output, hugging the hole for an
input. The band is hole-to-rim, the ring a third of it wide, dash and gap equal, and the
dash count derived from the circumference so the rhythm reads evenly at any size. */
static void drawJack(NVGcontext* vg, math::Vec c, float r, NVGcolor color, bool isOutput) {
	const float rh = r * 0.53f;   // Wcoast: hole 1.6 against radius 3.0

	// Coloured surround.
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r);
	nvgFillColor(vg, color);
	nvgFill(vg);
	nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 200));
	nvgStrokeWidth(vg, r * 0.1f);
	nvgStroke(vg);

	// Centre plug hole.
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, rh);
	nvgFillColor(vg, JACK_HOLE);
	nvgFill(vg);

	// Direction ring.
	const float band = r - rh;
	if (band <= 0.f)
		return;
	const float w = band / 3.f;
	const float ringR = isOutput ? (r - w / 2.f) : (rh + w / 2.f);
	if (ringR <= 0.f)
		return;

	// Equal dashes and gaps, sized so one full cycle closes on the circumference.
	const float circ = 2.f * M_PI * ringR;
	const int n = std::max(6, (int) std::round(circ / (w * 1.6f)));
	const float step = 2.f * M_PI / n;

	nvgStrokeColor(vg, JACK_RING);
	nvgStrokeWidth(vg, w);
	nvgLineCap(vg, NVG_BUTT);
	for (int i = 0; i < n; i++) {
		const float a0 = i * step;
		const float a1 = a0 + step / 2.f;
		nvgBeginPath(vg);
		nvgArc(vg, c.x, c.y, ringR, a0, a1, NVG_CW);
		nvgStroke(vg);
	}
}


static NVGcolor lighten(NVGcolor c, float amount) {
	return nvgRGBAf(
		math::clamp(c.r + (1.f - c.r) * amount * 0.45f, 0.f, 1.f),
		math::clamp(c.g + (1.f - c.g) * amount * 0.45f, 0.f, 1.f),
		math::clamp(c.b + (1.f - c.b) * amount * 0.45f, 0.f, 1.f),
		c.a);
}

static NVGcolor darken(NVGcolor c, float amount) {
	return nvgRGBAf(c.r * (1.f - amount), c.g * (1.f - amount), c.b * (1.f - amount), c.a);
}


/** A disc shaded by three colour stops at radii 0, `mid`, and 1.

nanovg gradients carry two colours, so a multi-stop radial has to be built from
concentric bands: fill the whole disc with the outer band's gradient, then overdraw a
smaller disc with the inner band's. Drawn largest first so each overwrites cleanly.
*/
static void drawShadedDisc(NVGcontext* vg, math::Vec c, float r, float mid,
	NVGcolor inner, NVGcolor middle, NVGcolor outer) {

	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r);
	nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, r * mid, r, middle, outer));
	nvgFill(vg);

	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r * mid);
	nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, 0.f, r * mid, inner, middle));
	nvgFill(vg);
}


/** The knob cap, as a four-stop metal disc.

These are Wcoast's actual dark-theme values from panel/theme.js, not approximations, and
the shape of the ramp matters more than the values do. It runs MONOTONICALLY LIGHTER from
centre to rim, in four closely spaced steps. Both properties are load-bearing: invent a
bright-dip-bright pattern instead, or space the stops widely, and the concentric bands
stop reading as brushed metal and start reading as a target.
*/
static void drawCapDome(NVGcontext* vg, math::Vec c, float cap) {
	const NVGcolor s0 = nvgRGB(0x3a, 0x3d, 0x43);
	const NVGcolor s1 = nvgRGB(0x4c, 0x50, 0x58);
	const NVGcolor s2 = nvgRGB(0x5a, 0x5f, 0x67);
	const NVGcolor s3 = nvgRGB(0x6b, 0x70, 0x79);

	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, cap);
	nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, cap * 0.62f, cap, s2, s3));
	nvgFill(vg);

	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, cap * 0.62f);
	nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, cap * 0.4f, cap * 0.62f, s1, s2));
	nvgFill(vg);

	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, cap * 0.4f);
	nvgFillPaint(vg, nvgRadialGradient(vg, c.x, c.y, 0.f, cap * 0.4f, s0, s1));
	nvgFill(vg);
}


/** Draws one knob: rim disc, cap, then the ticks and pointer which rotate TOGETHER.

The ticks are grip texture rather than a calibration scale, which is why they turn with
the pointer — the detail most easily got wrong by copying a picture.
*/
static void drawKnob(NVGcontext* vg, math::Vec c, float r, float angle,
	NVGcolor rimInner, int ticks, bool isDefaultRim) {

	const float cap = r * KNOB_CAP_RATIO;

	// Rim: Wcoast's blueRing is three stops (0, 0.55, 1), and nanovg gradients carry only
	// two colours, so it is drawn as concentric bands — largest first, each smaller disc
	// overdrawing the last. Derived from the base colour rather than hardcoded, so a
	// per-knob colour from a definition file still gets the same shading.
	if (isDefaultRim) {
		// Wcoast's blueRing, exactly: #1688cc, #006da8, #003d62 with the middle stop at
		// 0.55. Deriving these by lightening and darkening a base colour gave a glowing
		// donut instead — the real values are not symmetric about the middle.
		drawShadedDisc(vg, c, r, 0.55f,
			nvgRGB(0x16, 0x88, 0xcc), nvgRGB(0x00, 0x6d, 0xa8), nvgRGB(0x00, 0x3d, 0x62));
	}
	else {
		// A colour from a definition file still gets shading, derived from that colour.
		drawShadedDisc(vg, c, r, 0.55f,
			lighten(rimInner, 1.f), rimInner, darken(rimInner, 0.45f));
	}
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r);
	// Light blue in the dark theme. A dark rim outline disappears against a dark panel,
	// which is the whole problem this feature exists to solve.
	nvgStrokeColor(vg, KNOB_RIM_STROKE);
	nvgStrokeWidth(vg, r * 0.077f);
	nvgStroke(vg);

	// Cap: four stops (0, 0.4, 0.62, 1) reproducing Wcoast's brushed-metal dome — a
	// bright centre, a dip, a bright band just inside the rim, then a dark edge. The dark
	// theme's values, since the panels this sits on are dark. Flattening this to a single
	// two-stop gradient is what made the centre look unshaded.
	drawCapDome(vg, c, cap);

	// The light ring around the cap. Wcoast strokes the cap explicitly, and it does real
	// work: it separates the cap from the rim, so the knob reads as two concentric parts
	// rather than one blurry disc.
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, cap);
	nvgStrokeColor(vg, nvgRGB(0xb8, 0xb8, 0xbc));   // dark theme capStroke
	nvgStrokeWidth(vg, r * 0.06f);
	nvgStroke(vg);

	// Ticks and pointer, both rotating with the value.
	const float tIn = r * 0.78f;
	const float tOut = r * 1.04f;
	nvgSave(vg);
	nvgTranslate(vg, c.x, c.y);
	nvgRotate(vg, angle);

	nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
	nvgStrokeWidth(vg, r * 0.1f);
	nvgLineCap(vg, NVG_BUTT);
	// Ticks span the knob's own sweep, so a narrow-sweep knob keeps its grip marks in
	// front of the pointer rather than wrapping them all the way round.
	for (int k = 0; k < ticks; k++) {
		const float frac = (ticks == 1) ? 0.5f : (float) k / (ticks - 1);
		const float a = (frac - 0.5f) * 2.f * (150.f * M_PI / 180.f);
		nvgBeginPath(vg);
		nvgMoveTo(vg, std::sin(a) * tIn, -std::cos(a) * tIn);
		nvgLineTo(vg, std::sin(a) * tOut, -std::cos(a) * tOut);
		nvgStroke(vg);
	}

	// Pointer, from centre to the cap's edge. Bright, because this is the one mark that
	// states the value.
	nvgBeginPath(vg);
	nvgMoveTo(vg, 0.f, 0.f);
	nvgLineTo(vg, 0.f, -cap);
	nvgStrokeColor(vg, nvgRGB(0xb8, 0xb8, 0xbc));   // dark theme ink, not pure white
	nvgStrokeWidth(vg, r * 0.12f);
	nvgLineCap(vg, NVG_ROUND);
	nvgStroke(vg);

	nvgRestore(vg);
}


/** Walks the widget tree, accumulating each child's offset, and draws over every knob and
port it finds. Recursive because plugins sometimes nest controls inside sub-widgets rather
than parenting them directly to the module. */
static void drawRecursive(widget::Widget* w, const widget::Widget::DrawArgs& args,
	math::Vec offset, const ModuleDefinition* def) {

	for (widget::Widget* child : w->children) {
		if (!child->isVisible())
			continue;
		const math::Vec pos = offset.plus(child->box.pos);
		const math::Vec centre = pos.plus(child->box.size.div(2));

		if (app::Knob* knob = dynamic_cast<app::Knob*>(child)) {
			const float r = std::min(child->box.size.x, child->box.size.y) / 2.f;
			if (r > 1.f) {
				// Honour the knob's own angle range so it still sweeps through the arc
				// its developer intended; only the look is borrowed from Wcoast, not the
				// measurements.
				float frac = 0.5f;
				if (engine::ParamQuantity* pq = knob->getParamQuantity())
					frac = math::clamp(pq->getScaledValue(), 0.f, 1.f);
				const float angle = knob->minAngle + frac * (knob->maxAngle - knob->minAngle);

				NVGcolor rim = KNOB_RIM_INNER;
				int ticks = KNOB_DEFAULT_TICKS;
				bool defaultRim = true;
				if (def) {
					auto it = def->knobs.find(string::f("%d", knob->paramId));
					if (it != def->knobs.end()) {
						if (it->second.hasColor) {
							rim = it->second.color;
							defaultRim = false;
						}
						ticks = it->second.ticks;
					}
				}
				drawKnob(args.vg, centre, r, angle, rim, ticks, defaultRim);
			}
			continue;
		}

		if (app::PortWidget* port = dynamic_cast<app::PortWidget*>(child)) {
			const float r = std::min(child->box.size.x, child->box.size.y) / 2.f;
			if (r > 1.f) {
				const bool isOutput = (port->type == engine::Port::OUTPUT);

				// Family guessed from the port's name, then corrected by the definition.
				std::string name;
				if (engine::PortInfo* info = port->getPortInfo())
					name = info->getName();
				NVGcolor color = familyColor(guessFamily(name));

				if (def) {
					const std::string key = (isOutput ? "out:" : "in:") + string::f("%d", port->portId);
					auto it = def->ports.find(key);
					if (it != def->ports.end() && it->second.hasColor)
						color = it->second.color;
				}
				drawJack(args.vg, centre, r, color, isOutput);
			}
			continue;
		}

		// Not a control: descend, in case controls are nested inside it.
		drawRecursive(child, args, pos, def);
	}
}


/** Resolves a port's family once, honouring a definition override. */
static std::string resolveFamily(app::PortWidget* port, NVGcolor* colorOut) {
	std::string name;
	if (engine::PortInfo* info = port->getPortInfo())
		name = info->getName();
	std::string family = guessFamily(name);
	if (colorOut)
		*colorOut = familyColor(family);

	app::ModuleWidget* mw = port->getAncestorOfType<app::ModuleWidget>();
	if (!mw || !mw->model || !mw->model->plugin)
		return family;

	const ModuleDefinition* def = getDefinition(mw->model->plugin->slug, mw->model->slug);
	if (!def)
		return family;

	const bool isOutput = (port->type == engine::Port::OUTPUT);
	const std::string key = (isOutput ? "out:" : "in:") + string::f("%d", port->portId);
	auto it = def->ports.find(key);
	if (it != def->ports.end() && it->second.hasColor && colorOut)
		*colorOut = it->second.color;
	return family;
}


NVGcolor portColor(app::PortWidget* port) {
	if (!port)
		return familyColor("audio");
	NVGcolor c = familyColor("audio");
	resolveFamily(port, &c);
	return c;
}


float portFlowDashLength(app::PortWidget* port) {
	if (!port)
		return 3.4f;
	const std::string family = resolveFamily(port, NULL);
	if (family == "audio")
		return 1.6f;
	if (family == "trigger")
		return 5.6f;
	// control and pitch share a length in Wcoast's table.
	return 3.4f;
}


void drawControls(app::ModuleWidget* mw, const widget::Widget::DrawArgs& args) {
	if (!settings::controlAppearanceEnabled)
		return;
	if (!mw)
		return;

	const ModuleDefinition* def = NULL;
	if (mw->model && mw->model->plugin)
		def = getDefinition(mw->model->plugin->slug, mw->model->slug);

	drawRecursive(mw, args, math::Vec(), def);
}


} // namespace appearance
} // namespace rack
