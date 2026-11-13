#include "../tray-core.h"
#include "../hdr-state.h"

static_assert(hdrStateFromCount(0) == HdrState::Inactive);
static_assert(hdrStateFromCount(1) == HdrState::Active);
static_assert(automaticTarget(Mode::Gamma22, HdrState::Inactive) == Mode::Srgb);
static_assert(automaticTarget(Mode::Gamma22, HdrState::Active) == Mode::Gamma22);
static_assert(automaticTarget(Mode::Gamma22, HdrState::Unavailable) == Mode::Unknown);
static_assert(automaticTarget(Mode::Srgb, HdrState::Active) == Mode::Srgb);
static_assert(canSelectMode(Mode::Srgb, HdrState::Inactive, Mode::Gamma22, false));
static_assert(!canSelectMode(Mode::Gamma22, HdrState::Inactive, Mode::Srgb, false));
static_assert(!canSelectMode(Mode::Gamma22, HdrState::Unavailable, Mode::Srgb, false));
static_assert(!canSelectMode(Mode::Gamma22, HdrState::Active, Mode::Unknown, false));
static_assert(!canSelectMode(Mode::Gamma22, HdrState::Active, Mode::Srgb, true));

int main() {
    puts("HDR policy checks passed: no HDR -> Native; unknown -> no automatic change.");
}
