#include "Locations.h"

/*
* PlayerCharacter::PositionPlayer (0x0093C200) is the one function every player teleport goes through (save load,
* doors, MoveTo, fast travel, coc) and it receives the destination cell. Its 7 callers are patched, each chained to
* whatever call target is there, so Locations can apply the destination's presets before the cell loads.
*   void __thiscall PositionPlayer(PlayerCharacter*, NiPoint3 aPos, NiPoint3 aAngle, TESObjectCELL* apCell, bool abResetWeather)
* Compatibility (checked against all repos in Walls Repos, 1540 hook sites): nothing patches PositionPlayer or these call
* sites. Better Transitions hooks RequestPositionPlayer and the HandlePositionPlayerRequest call, which run before it.
*/

namespace {

const UInt32 kPositionPlayer = 0x0093C200;
const UInt32 kCallSites[7] = { 0x0084C6AB, 0x0084C8A6, 0x0084CAD2, 0x0093C025, 0x0093CDD7, 0x0093DCBB, 0x0094DD21 };

typedef void(__thiscall* PositionPlayerFn)(PlayerCharacter*, NiPoint3, NiPoint3, TESObjectCELL*, bool);
CallDetour kPositionPlayerDetours[7];

template <int N>
void __fastcall PositionPlayerHook(PlayerCharacter* apThis, void*, NiPoint3 aPos, NiPoint3 aAngle, TESObjectCELL* apCell, bool abResetWeather) {
	if (TheLocationManager) TheLocationManager->OnDestination(apCell);
	((PositionPlayerFn)kPositionPlayerDetours[N].GetOverwrittenAddr())(apThis, aPos, aAngle, apCell, abResetWeather);
}

template <int N>
void Attach() {
	UInt32 site = kCallSites[N];
	if (*(UInt8*)site != 0xE8) {
		Logger::Log("[Locations] 0x%08X is not a call, hook skipped.", site);
		return;
	}
	if (GetRelJumpAddr(site) != kPositionPlayer)
		Logger::Log("[Locations] 0x%08X calls 0x%08X, not PositionPlayer: someone else hooked it, chaining to that.", site, (UInt32)GetRelJumpAddr(site));
	kPositionPlayerDetours[N].ReplaceCall(site, PositionPlayerHook<N>);
}

}

void AttachLocationHooks() {
	Attach<0>(); Attach<1>(); Attach<2>(); Attach<3>(); Attach<4>(); Attach<5>(); Attach<6>();
}
