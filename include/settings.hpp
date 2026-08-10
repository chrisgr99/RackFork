#pragma once
#include <vector>
#include <set>
#include <map>
#include <list>
#include <tuple>

#include <jansson.h>

#include <common.hpp>
#include <math.hpp>
#include <color.hpp>


namespace rack {
/** Process-scope globals, most of which are persisted across launches */
namespace settings {


// Runtime state, not serialized.

/** Path to settings.json */
extern std::string settingsPath;
extern bool devMode;
extern bool headless;
extern bool isPlugin;
/** Requests to restart the application on exit. */
extern bool restart;

// Persistent state, serialized to settings.json.

/** ISO 639-1 language code for string translations. */
extern std::string language;
/** Launches Rack without loading plugins or the autosave patch. Always set to false when settings are saved. */
extern bool safeMode;
/** vcvrack.com user token */
extern std::string token;
/** Whether the window is maximized */
extern bool windowMaximized;
/** Size of window in pixels */
extern math::Vec windowSize;
/** Position in window in pixels */
extern math::Vec windowPos;
/** Reverse the zoom scroll direction */
extern bool invertZoom;
/** Mouse wheel zooms instead of pans. */
extern bool mouseWheelZoom;
/** Ratio between UI pixel and physical screen pixel.
0 for auto.
*/
extern float pixelRatio;
/** Name of UI theme, specified in ui::refreshTheme() */
extern std::string uiTheme;
/** Opacity of cables in the range [0, 1] */
extern float cableOpacity;
/** Straightness of cables in the range [0, 1]. Unitless and arbitrary. */
extern float cableTension;
extern float rackBrightness;
extern float spotlightBrightness;
extern float spotlightRadius;
extern float haloBrightness;
/** Allows rack to hide and lock the cursor position when dragging knobs etc. */
extern bool allowCursorLock;
enum KnobMode {
	KNOB_MODE_LINEAR,
	KNOB_MODE_SCALED_LINEAR,
	KNOB_MODE_ROTARY_ABSOLUTE,
	KNOB_MODE_ROTARY_RELATIVE,
};
extern KnobMode knobMode;
extern bool knobScroll;
extern float knobLinearSensitivity;
extern float knobScrollSensitivity;
extern float sampleRate;
extern int threadCount;
extern bool tooltips;
extern bool cpuMeter;
extern bool lockModules;
extern bool squeezeModules;
extern bool preferDarkPanels;

/** Panel brightness reduction — see design/panel-dimming.md.
Dims large bright panel regions and lifts low-contrast legend text, for low vision.
These are new standalone globals rather than fields on an existing struct, so they add
no ABI surface. Appending here is safe for the same reason. */
extern bool panelDimEnabled;
/** Spatial scale of the regional luminance estimate, in pixels. */
extern float panelDimBlurRadius;
/** Regional luminance below which nothing is dimmed, 0-1. */
extern float panelDimThreshold;
/** Multiplier at maximum regional luminance. 0.5 halves the brightest large regions,
1.0 disables dimming. */
extern float panelDimMaxAttenuation;
/** Local contrast amplification, which is what rescues grey-on-grey legend text. */
extern float panelDimContrastGain;
/** Edge respect of the blur, 0-1. This is the size-sensitivity dial: low dims small
bright features too, high spares them while still dimming large areas. */
extern float panelDimEdgeSensitivity;
/** Per-panel strength override, pluginSlug -> (moduleSlug -> strength 0-1).
Absent means "derive a strength from the panel's own measured brightness". Present means
the user overrode it. Deliberately a separate map rather than a field on ModuleInfo,
which lives in this SDK header. */
extern std::map<std::string, std::map<std::string, float>> panelDimStrengths;
/** Draws replacement knob and jack graphics over every module's own artwork, so controls
are legible and consistent regardless of who drew them. See design/control-appearance.md. */
extern bool controlAppearanceEnabled;
/** Returns the stored override, or -1 if this panel has none. */
float getPanelDimStrength(const std::string& pluginSlug, const std::string& moduleSlug);
void setPanelDimStrength(const std::string& pluginSlug, const std::string& moduleSlug, float strength);
void clearPanelDimStrength(const std::string& pluginSlug, const std::string& moduleSlug);
/** Maximum screen redraw frequency in Hz, or 0 for unlimited. */
extern float frameRateLimit;
/** Interval between autosaves in seconds. */
extern float autosaveInterval;
extern bool skipLoadOnLaunch;
extern std::string lastPatchDirectory;
extern std::string lastSelectionDirectory;
extern std::list<std::string> recentPatchPaths;
extern std::vector<NVGcolor> cableColors;
extern std::vector<std::string> cableLabels;
extern bool cableAutoRotate;
extern bool autoCheckUpdates;
extern bool verifyHttpsCerts;
extern bool showTipsOnLaunch;
extern int tipIndex;
enum BrowserSort {
	BROWSER_SORT_UPDATED,
	BROWSER_SORT_LAST_USED,
	BROWSER_SORT_MOST_USED,
	BROWSER_SORT_BRAND,
	BROWSER_SORT_NAME,
	BROWSER_SORT_RANDOM,
};
extern BrowserSort browserSort;
extern float browserZoom;
extern json_t* pluginSettingsJ;

struct ModuleInfo {
	bool enabled = true;
	bool favorite = false;
	int added = 0;
	double lastAdded = NAN;
};
/** pluginSlug -> (moduleSlug -> ModuleInfo) */
extern std::map<std::string, std::map<std::string, ModuleInfo>> moduleInfos;
/** Returns a ModuleInfo if exists for the given slugs.
*/
ModuleInfo* getModuleInfo(const std::string& pluginSlug, const std::string& moduleSlug);

/** The VCV JSON API returns the data structure
{pluginSlug: [moduleSlugs] or true}
where "true" represents that the user is subscribed to the plugin (all modules and future modules).
C++ isn't weakly typed, so we need the PluginWhitelist data structure to store this information.
*/
struct PluginWhitelist {
	bool subscribed = false;
	std::set<std::string> moduleSlugs;
};
extern std::map<std::string, PluginWhitelist> moduleWhitelist;

bool isModuleWhitelisted(const std::string& pluginSlug, const std::string& moduleSlug);
void resetCables();

PRIVATE void init();
PRIVATE void destroy();
PRIVATE json_t* toJson();
PRIVATE void fromJson(json_t* rootJ);
PRIVATE void save(std::string path = "");
PRIVATE void load(std::string path = "");


} // namespace settings
} // namespace rack
