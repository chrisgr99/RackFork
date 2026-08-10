/** Option-held navigation mode — see design/zoom-pan.md */
#include "navMode.hpp"


namespace rack {
namespace app {


static bool active = false;


bool navModeActive() {
	return active;
}


void navModeSetActive(bool a) {
	active = a;
}


} // namespace app
} // namespace rack
