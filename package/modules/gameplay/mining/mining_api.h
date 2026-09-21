#pragma once
// gameplay/mining events (HUD toasts, stats, achievements later).
#include <string>

namespace gameplay {

// Ore was collected into the hold (only the amount that fitted).
struct OreMined { std::string ore; int amount = 0; };

} // namespace gameplay
