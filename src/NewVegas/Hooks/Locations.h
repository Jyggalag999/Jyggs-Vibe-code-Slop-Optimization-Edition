#pragma once

// Call-site hooks on the callers of PlayerCharacter::PositionPlayer, so Locations picks the presets for
// the destination before it loads.
void AttachLocationHooks();
