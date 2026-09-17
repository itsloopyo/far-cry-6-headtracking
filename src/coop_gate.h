// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace FarCry6HeadTracking {

// Watches whether the game has a multiplayer session published.
//
// Far Cry 6 has no versus mode; its multiplayer is drop-in co-op, joined from
// inside a running game rather than from a lobby in the main menu, so there is no
// menu state to read. What there is, is the Ubisoft Connect API the game itself
// uses to advertise a joinable session: UPC_MultiplayerSessionSet publishes one
// and UPC_MultiplayerSessionClear takes it down. Both are ordinary exports of
// upc_r2_loader64.dll, which is a third-party library sitting next to the game
// rather than part of the protected game binary, so watching them needs no
// pattern scan and survives a game patch that moves everything else.
//
// Returns false when the hooks could not be installed, and says so in the log:
// a gate that has quietly stopped gating must not read as a session that is
// quietly single player.
bool StartCoopGate();
void StopCoopGate();

}  // namespace FarCry6HeadTracking
