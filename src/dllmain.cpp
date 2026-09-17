// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include <windows.h>

// Nothing runs from here but the module-handle bookkeeping. The config read, the
// socket and the hotkey thread all start from the game's first GetApi call, which
// is outside the loader lock; doing any of it here deadlocks a process that is
// still loading DLLs.
//
// Teardown is not attempted here either, in EITHER detach case. Mod::Shutdown
// joins the hotkey and centring threads, and a thread cannot exit while the
// calling thread holds the loader lock, because its own DLL_THREAD_DETACH needs
// that lock - so the join would hang the game on the way out. The mod is pinned
// from the first GetApi call precisely so this path cannot be reached with
// anything running, and the game's own ITobiiGameIntegrationApi::Shutdown is
// where an orderly stop actually happens.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
