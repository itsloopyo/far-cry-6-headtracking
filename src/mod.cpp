// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod.h"

#include "camera_adapter.h"
#include "coop_gate.h"
#include "hotkeys.h"
#include "logging.h"
#include "window_centering.h"

#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <string>
#include <vector>

namespace FarCry6HeadTracking {

namespace {
constexpr wchar_t kLogName[] = L"FarCry6HeadTracking.log";

std::string Utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<size_t>(size), ' ');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr,
                        nullptr);
    return out;
}

void LogLines(const std::vector<std::string>& lines, const std::string& reason) {
    for (const std::string& line : lines) Log::Line("%s", line.c_str());
    if (!reason.empty()) Log::Line("%s", reason.c_str());
}

// Keeps this DLL mapped for the rest of the process.
//
// Everything below hands out something that outlives the call: the game keeps
// the vtable pointers GetApi returned, the co-op gate writes detours into
// upc_r2_loader64.dll that jump back into this image, and the receiver, the
// hotkey poller and the centring wait each leave a thread running here. A host
// that resolved this DLL through the SDK's dynamic loader - which is why
// GetApiDynamic is exported at all - can FreeLibrary it, and every one of those
// then points into unmapped memory. Stopping them from DllMain is not the
// answer either: that runs under the loader lock, and each of those threads
// needs the same lock to exit, so the join deadlocks the game on the way out.
//
// Taken from the first GetApi call rather than from DllMain, like everything
// else here, so it is outside the loader lock. PIN cannot be combined with
// UNCHANGED_REFCOUNT - pinning IS a reference the loader never gives back.
void PinModule() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
                                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&PinModule), &self)) {
        Log::Line("WARN: could not pin this DLL in the process (%lu). Head tracking runs "
                  "normally; a host that unloads it mid-session would take the game with "
                  "it.", GetLastError());
    }
}
}  // namespace

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

void Mod::OpenLog() {
    const std::wstring dir = cameraunlock::os::SelfModuleDirectory();
    if (dir.empty()) {
        return;
    }
    Log::Open(dir + L"\\" + kLogName);
}

bool Mod::LoadConfiguration() {
    const std::wstring dir = cameraunlock::os::SelfModuleDirectory();
    if (dir.empty()) {
        Log::Line("ERROR: could not resolve the directory this DLL was loaded from. "
                  "The mod will not start and the game will run as if no eye tracker "
                  "were present.");
        return false;
    }
    const std::wstring path = dir + L"\\" + kConfigFileName;
    m_owner.emplace(ConfigOwnerOptionsFor(dir, cameraunlock::config::DefaultsFile::PerUser()));
    cameraunlock::config::ConfigLoadResult<Config> loaded = m_owner->Load();
    // install.cmd and uninstall.cmd tell this DLL from the game's own by these words
    // (SHIM_MARKER), so every build has to carry them.
    Log::Line("Far Cry 6 - Head Tracking configuration: %s, %s", Utf8(path).c_str(),
              cameraunlock::config::ConfigLoadStatusName(loaded.status));
    LogLines(loaded.log, loaded.reason);
    // The build before the canonical format did not start on a file it refused, so
    // this one does not either.
    if (loaded.status == cameraunlock::config::ConfigLoadStatus::LegacyRefused) {
        return false;
    }
    m_cfg = std::move(loaded.config);

    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", m_cfg.world_space_yaw ? "world" : "camera-local");
    Log::Line("Port %d, enable on startup %s, rotation %s, position %s, disable in co-op %s",
              m_cfg.udp_port,
              m_cfg.enable_on_startup ? "yes" : "no",
              m_cfg.rotation_enabled ? "on" : "off",
              m_cfg.position_enabled ? "on" : "off",
              m_cfg.disable_in_coop ? "yes" : "no");
    return true;
}

void Mod::Save(const std::function<void(Config&)>& change) {
    const cameraunlock::config::ConfigSaveResult saved = m_owner->Save(change);
    LogLines(saved.log, saved.reason);
}

void Mod::StartSubsystems() {
    m_runtime.Start(m_cfg);
    StartHotkeys(m_cfg);

    const bool coopGateActive = StartCoopGate();
    m_coopGateActive.store(coopGateActive, std::memory_order_relaxed);
    if (m_cfg.disable_in_coop && !coopGateActive) {
        Log::Line("WARN: DisableInCoop is on but the co-op watch could not be "
                  "installed, so head tracking will stay on in a co-op session.");
    }

    StartWindowCentering(GetModuleHandleW(L"FC_m64d3d12.dll"));
}

bool Mod::EnsureStarted() {
    // One attempt per process. A second one would re-bind the socket and start a
    // second hotkey thread, and the game calls GetApi more than once.
    bool expected = false;
    if (!m_startAttempted.compare_exchange_strong(expected, true)) {
        const bool started = m_started.load(std::memory_order_acquire);
        if (!started) {
            // Two ways to be here, and they need different answers: the mod shut
            // down, or it never came up because the configuration would not load.
            // Reporting the second as the first sends the user looking for a
            // shutdown that never happened, in the file the README tells them to
            // read first.
            static bool s_reported = false;
            if (!s_reported) {
                s_reported = true;
                if (m_shutdownSeen.load(std::memory_order_relaxed)) {
                    Log::Line("ERROR: the game asked for head tracking again after "
                              "shutting it down, and this mod does not restart within a "
                              "session. The game will run as if no eye tracker were "
                              "present until it is relaunched.");
                } else {
                    Log::Line("ERROR: head tracking never started - see the error above "
                              "this line for why - so the game is running as if no eye "
                              "tracker were present.");
                }
            }
        }
        return started;
    }

    // Before anything that outlives this call exists, and unconditionally: the
    // caller hands the game vtable pointers into this image whether or not the
    // configuration below loads.
    PinModule();

    if (!LoadConfiguration()) {
        return false;
    }
    if (!StartCameraAdapter(m_cfg)) {
        return false;
    }
    StartSubsystems();

    m_started.store(true, std::memory_order_release);
    return true;
}

void Mod::Shutdown() {
    if (!m_started.exchange(false)) {
        return;
    }
    StopHotkeys();
    StopCoopGate();
    StopWindowCentering();
    m_runtime.Stop();

    // The gate flags describe subsystems that no longer exist, so they are cleared
    // with them. Leaving m_coopGateActive set would have the heartbeat reporting a
    // co-op watch that has been uninstalled, which is the one thing that flag is
    // there to distinguish.
    m_coopGateActive.store(false, std::memory_order_relaxed);
    m_inCoop.store(false, std::memory_order_relaxed);
    m_paused.store(false, std::memory_order_relaxed);
    m_shutdownSeen.store(true, std::memory_order_relaxed);

    // m_startAttempted deliberately stays set. Restarting is not supported and
    // must not be made to look supported: StartHotkeys would push a second copy
    // of every binding onto a poller that has no way to drop the first, so one
    // Ctrl+Shift+G press would step the mode cycle twice, and LoadConfiguration
    // would rewrite the non-atomic Config while the render thread reads it.
    // EnsureStarted says so out loud instead.
    Log::Line("Shutdown");
}

void Mod::SetPaused(bool paused) {
    if (m_paused.exchange(paused) == paused) {
        return;
    }
    Log::Line("Game %s the view: head tracking %s", paused ? "paused" : "resumed",
              paused ? "held still" : "live");
}

void Mod::SetInCoopSession(bool in_coop) {
    if (m_inCoop.exchange(in_coop) == in_coop) {
        return;
    }
    Log::Line("Head tracking %s: the session now has %s",
              in_coop ? "held still" : "live",
              in_coop ? "another player in it" : "one player in it");
}

bool Mod::TrackingAllowed() const {
    if (!m_started.load(std::memory_order_acquire)) return false;
    if (m_paused.load(std::memory_order_relaxed)) return false;
    if (m_cfg.disable_in_coop && m_inCoop.load(std::memory_order_relaxed)) return false;
    return true;
}

void Mod::ToggleYawMode() {
    const bool world = !WorldSpaceYaw();
    m_worldSpaceYaw.store(world, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", world ? "world" : "camera-local");
    Save([world](Config& c) { c.world_space_yaw = world; });
}

void Mod::CycleTrackingMode() {
    const cameraunlock::TrackingModeChannels channels =
        cameraunlock::EncodeTrackingMode(m_runtime.CycleTrackingMode());
    Save([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace FarCry6HeadTracking
