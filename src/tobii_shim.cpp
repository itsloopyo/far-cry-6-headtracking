// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "frame_pump.h"
#include "logging.h"
#include "mod.h"
#include "pose_bridge.h"
#include "tobii_abi.h"
#include "tracker_info.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace FarCry6HeadTracking {

namespace {

// Reports the first time the game reaches a given entry point. What the game
// actually calls is the only evidence for which of its features are live.
//
// The load ahead of the exchange is what keeps the cost to a read and a branch
// once a slot has been seen. Update and GetTransformation carry this macro and
// run once per frame for the whole session, so an unconditional read-modify-write
// would have every frame writing a flag that has been set since startup back to
// the same value, dirtying the line for every core that reads it.
#define TRACE_ONCE(what)                                              \
    do {                                                              \
        static std::atomic<bool> traced{false};                       \
        if (!traced.load(std::memory_order_relaxed) &&                \
            !traced.exchange(true, std::memory_order_relaxed)) {      \
            Log::Line("game -> %s", what);                            \
        }                                                             \
    } while (0)

// Marks a slot the game is not expected to reach. Reaching one means either the
// game changed or a vtable slot is in the wrong place, and both are worth a loud
// line rather than a plausible-looking return value.
#define TRACE_UNEXPECTED(what)                                                     \
    do {                                                                           \
        static std::atomic<bool> traced{false};                                    \
        if (!traced.load(std::memory_order_relaxed) &&                             \
            !traced.exchange(true, std::memory_order_relaxed)) {                   \
            Log::Line("WARN: the game called %s, which this mod does not model. "  \
                      "Head tracking still runs; report this line.", what);        \
        }                                                                          \
    } while (0)

// The window the game asked to be tracked, so the TrackerInfo it is handed
// describes the monitor that window is on. Set once from TrackWindow.
HWND g_trackedWindow = nullptr;

struct ExtendedView final : tobii::IExtendedView {
    tobii::ExtendedViewTransformation GetTransformation() override {
        TRACE_ONCE("IExtendedView::GetTransformation");
        FramePump& pump = FramePump::Instance();
        pump.NoteTransformationRead();
        return ToExtendedViewTransformation(pump.Current());
    }

    bool UpdateAdvancedSettings(const void* settings) override {
        TRACE_ONCE("IExtendedView::UpdateAdvancedSettings");
        (void)settings;
        // The pose is already shaped by the tracker and the mod pipeline, so the
        // game's own sensitivity curve is not applied on top of it. Accepting the
        // call keeps the settings screen working.
        return true;
    }

    void ResetDefaultHeadPose() override {
        TRACE_ONCE("IExtendedView::ResetDefaultHeadPose");
        // Deliberately does nothing. The mod keeps no centre of its own - the
        // tracker owns that - so there is nothing here to reset. Acting on this
        // would put a second centre in series with the tracker's own.
    }

    void SetPaused(bool paused) override {
        TRACE_ONCE("IExtendedView::SetPaused");
        Mod::Instance().SetPaused(paused);
    }

    // The shipped DLL sets a PENDING flag here and leaves the paused state alone,
    // so its own GetPaused still reads false afterwards. Nothing is modelled for
    // that distinction because the game is not seen using either slot; reaching
    // one means the assumption is wrong and is worth a line rather than a guess.
    void SetPausedAfterRecentering() override {
        TRACE_UNEXPECTED("IExtendedView::SetPausedAfterRecentering");
    }

    bool GetPaused() override { return Mod::Instance().IsPaused(); }

    bool UpdateSimpleSettings(const void* settings) override {
        TRACE_ONCE("IExtendedView::UpdateSimpleSettings");
        if (settings) {
            const float* f = static_cast<const float*>(settings);
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true)) {
                Log::Line("game extended view settings: %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
                          f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
            }
        }
        return true;
    }

    void GetAdvancedSettings(void* settings) const override {
        TRACE_ONCE("IExtendedView::GetAdvancedSettings");
        if (!settings) {
            TRACE_UNEXPECTED("IExtendedView::GetAdvancedSettings with no buffer");
            return;
        }
        std::memset(settings, 0, kAdvancedSettingsBytes);
    }

    void GetSimpleSettings(void* settings) const override {
        TRACE_ONCE("IExtendedView::GetSimpleSettings");
        if (!settings) {
            TRACE_UNEXPECTED("IExtendedView::GetSimpleSettings with no buffer");
            return;
        }
        std::memset(settings, 0, kSimpleSettingsBytes);
    }

    int GetSensitivityGradientAsPolyLine(void* points, int max_points,
                                         const void* settings) const override {
        TRACE_ONCE("IExtendedView::GetSensitivityGradientAsPolyLine");
        (void)points;
        (void)max_points;
        (void)settings;
        // No gradient is applied, so there is no curve to draw. Zero points is the
        // honest answer and is what an empty settings preview expects.
        return 0;
    }

private:
    // Sizes read out of the shipped 7.3 DLL: its simple-settings constructor writes
    // 0x1C bytes and its advanced-settings constructor 0x7C.
    static constexpr size_t kSimpleSettingsBytes = 0x1C;
    static constexpr size_t kAdvancedSettingsBytes = 0x7C;
};

ExtendedView g_extendedView;

struct Features final : tobii::IFeatures {
    tobii::IExtendedView* GetExtendedView() override {
        TRACE_ONCE("IFeatures::GetExtendedView");
        return &g_extendedView;
    }
};

Features g_features;

// Storage the game may hold a pointer to after GetTrackerInfos returns, so it lives
// for the process rather than on a stack.
tobii::TrackerInfo g_trackerInfo = {};

struct TrackerController final : tobii::ITrackerController {
    // is_attached is refreshed on the way out rather than served from the snapshot.
    // The struct is built once at startup, and IsConnected below answers live, so
    // without this the two disagree the moment the mod stops.
    bool GetTrackerInfoByUrl(const char* url, tobii::TrackerInfo& info) override {
        TRACE_ONCE("ITrackerController::GetTrackerInfoByUrl");
        (void)url;
        info = g_trackerInfo;
        info.is_attached = Mod::Instance().IsStarted();
        return true;
    }

    bool GetTrackerInfo(tobii::TrackerInfo& info) override {
        TRACE_ONCE("ITrackerController::GetTrackerInfo");
        info = g_trackerInfo;
        info.is_attached = Mod::Instance().IsStarted();
        return true;
    }

    void UpdateTrackerInfos() override {
        TRACE_ONCE("ITrackerController::UpdateTrackerInfos");
        g_trackerInfo = MakeTrackerInfo(g_trackedWindow, Mod::Instance().IsStarted());
    }

    bool GetTrackerInfos(const tobii::TrackerInfo*& infos, int& count) override {
        TRACE_ONCE("ITrackerController::GetTrackerInfos");
        // Refreshed in place, for the same reason the two above are: this hands out
        // a pointer to the stored struct, so the value has to be current rather
        // than whatever was true when it was built.
        g_trackerInfo.is_attached = Mod::Instance().IsStarted();
        infos = &g_trackerInfo;
        count = 1;
        return true;
    }

    bool TrackHMD() override {
        TRACE_UNEXPECTED("ITrackerController::TrackHMD");
        return false;
    }

    bool TrackRectangle(const tobii::Rectangle& rectangle) override {
        TRACE_ONCE("ITrackerController::TrackRectangle");
        (void)rectangle;
        return true;
    }

    bool TrackWindow(void* window_handle) override {
        TRACE_ONCE("ITrackerController::TrackWindow");
        g_trackedWindow = static_cast<HWND>(window_handle);
        g_trackerInfo = MakeTrackerInfo(g_trackedWindow, Mod::Instance().IsStarted());
        return true;
    }

    // Not modelled, and deliberately so. The traced session never reaches this
    // slot, and the three calls that would credibly mean "start again"
    // (TrackWindow, TrackRectangle, TrackTracker) all happen once in the startup
    // prologue, so a flag set here could never be cleared and would hold the view
    // still for the rest of the session. Say so loudly instead of guessing.
    void StopTracking() override {
        TRACE_UNEXPECTED("ITrackerController::StopTracking");
    }

    bool IsConnected() const override { return Mod::Instance().IsStarted(); }
    bool IsEnabled() const override { return Mod::Instance().IsStarted(); }

    bool TrackTracker(const char* url) override {
        TRACE_ONCE("ITrackerController::TrackTracker");
        (void)url;
        return true;
    }
};

TrackerController g_trackerController;

struct StreamsProvider final : tobii::IStreamsProvider {
    int GetHeadPoses(const tobii::HeadPose*& poses) override {
        TRACE_ONCE("IStreamsProvider::GetHeadPoses");
        m_pose = LatestPose();
        poses = &m_pose;
        return 1;
    }

    bool GetLatestHeadPose(tobii::HeadPose& pose) override {
        TRACE_ONCE("IStreamsProvider::GetLatestHeadPose");
        pose = LatestPose();
        return true;
    }

    int GetGazePoints(const tobii::GazePoint*& points) override {
        TRACE_ONCE("IStreamsProvider::GetGazePoints");
        points = nullptr;
        return 0;
    }

    bool GetLatestGazePoint(tobii::GazePoint& point) override {
        TRACE_ONCE("IStreamsProvider::GetLatestGazePoint");
        // Cleared rather than left alone. A caller that reads the struct without
        // branching on the return consumes whatever was on its stack, which this
        // game has already been seen doing once through the aim-at-gaze filter.
        point = tobii::GazePoint{};
        // No gaze stream. Everything the game keys off gaze - aim at gaze, enemy
        // tagging, dynamic light adaptation - stays off, which is what keeps head
        // tracking from touching where a shot goes.
        return false;
    }

    int GetEyeInfos(const void*& infos) override {
        TRACE_ONCE("IStreamsProvider::GetEyeInfos");
        infos = nullptr;
        return 0;
    }

    // Left untouched, unlike the gaze point above: the 7.3 size of this struct is
    // not known here, and clearing a guessed number of bytes in the game's buffer
    // is worse than clearing none.
    bool GetLatestEyeInfo(void* info) override {
        TRACE_ONCE("IStreamsProvider::GetLatestEyeInfo");
        (void)info;
        return false;
    }

    int GetHMDGaze(const void*& gaze) override {
        TRACE_ONCE("IStreamsProvider::GetHMDGaze");
        gaze = nullptr;
        return 0;
    }

    // Untouched for the same reason as GetLatestEyeInfo above.
    bool GetLatestHMDGaze(void* gaze) override {
        TRACE_ONCE("IStreamsProvider::GetLatestHMDGaze");
        (void)gaze;
        return false;
    }

    // Presence is what the game uses to decide the player is at the screen. It
    // follows the tracker actually sending, so walking away stops it the same way
    // an eye tracker losing the face would.
    bool IsPresent() override { return Mod::Instance().Runtime().IsReceiving(); }

    void SetAutoUnsubscribe(int stream, float timeout_seconds) override {
        TRACE_ONCE("IStreamsProvider::SetAutoUnsubscribe");
        (void)stream;
        (void)timeout_seconds;
    }

    void UnsetAutoUnsubscribe(int stream) override {
        TRACE_ONCE("IStreamsProvider::UnsetAutoUnsubscribe");
        (void)stream;
    }

    void ConvertGazePoint(const tobii::GazePoint& from, tobii::GazePoint& to,
                          int from_unit, int to_unit) override {
        TRACE_UNEXPECTED("IStreamsProvider::ConvertGazePoint");
        (void)from_unit;
        (void)to_unit;
        to = from;
    }

private:
    // The timestamp comes from the frame the pose was published on, not from the
    // moment the game asked. Reading the counter here instead would label two
    // reads of one frame's pose with two different times, so anything deriving a
    // velocity from them sees the head stall.
    static tobii::HeadPose LatestPose() {
        tobii::HeadPose pose{};
        const FramePump& pump = FramePump::Instance();
        const tobii::Transformation& current = pump.Current();
        pose.rotation = current.rotation;
        pose.position = current.position;
        pose.timestamp_microseconds = pump.CurrentTimestampMicroseconds();
        return pose;
    }

    tobii::HeadPose m_pose{};
};

StreamsProvider g_streamsProvider;

struct Statistics final : tobii::IStatistics {
    void SetFeatureList(const tobii::Feature* features, int count) override {
        TRACE_ONCE("IStatistics::SetFeatureList");
        (void)features;
        (void)count;
    }
    const char* GetLiteral(int literal) override {
        TRACE_ONCE("IStatistics::GetLiteral");
        (void)literal;
        return "";
    }
    void SendFeatureEnabled(int feature_id) override {
        TRACE_ONCE("IStatistics::SendFeatureEnabled");
        (void)feature_id;
    }
    void SendFeatureDisabled(int feature_id) override {
        TRACE_ONCE("IStatistics::SendFeatureDisabled");
        (void)feature_id;
    }
    void SendFeaturesState() override { TRACE_ONCE("IStatistics::SendFeaturesState"); }
    void StopAllLogging() override { TRACE_ONCE("IStatistics::StopAllLogging"); }
    void ResumeAllLogging() override { TRACE_ONCE("IStatistics::ResumeAllLogging"); }
    void SendStatistics() override { TRACE_ONCE("IStatistics::SendStatistics"); }
};

Statistics g_statistics;

tobii::ResponsiveFilterSettings g_responsiveFilterSettings{};
tobii::AimAtGazeFilterSettings g_aimAtGazeFilterSettings{};
tobii::BilateralFilterSettings g_bilateralFilterSettings{};

// A gaze point at the origin. Gaze arrives signed-normalized, so the origin is
// screen centre, which is where the game already aims. The game polls the
// aim-at-gaze filter every frame whatever the tracker reports, so answering with
// centre and zero stability is what keeps head tracking from nudging aim: there is
// no offset to apply and nothing that says the reading is trustworthy.
tobii::GazePoint CentreGaze() { return tobii::GazePoint{}; }

struct Filters final : tobii::IFilters {
    const tobii::ResponsiveFilterSettings* GetResponsiveFilterSettings() const override {
        TRACE_ONCE("IFilters::GetResponsiveFilterSettings");
        return &g_responsiveFilterSettings;
    }
    void SetResponsiveFilterSettings(const void* settings) override {
        TRACE_ONCE("IFilters::SetResponsiveFilterSettings");
        (void)settings;
    }
    const tobii::AimAtGazeFilterSettings* GetAimAtGazeFilterSettings() const override {
        TRACE_ONCE("IFilters::GetAimAtGazeFilterSettings");
        return &g_aimAtGazeFilterSettings;
    }
    void SetAimAtGazeFilterSettings(const void* settings) override {
        TRACE_ONCE("IFilters::SetAimAtGazeFilterSettings");
        (void)settings;
    }
    const tobii::BilateralFilterSettings* GetBilateralFilterSettings() const override {
        TRACE_ONCE("IFilters::GetBilateralFilterSettings");
        return &g_bilateralFilterSettings;
    }
    void SetBilateralFilterSettings(const void* settings) override {
        TRACE_ONCE("IFilters::SetBilateralFilterSettings");
        (void)settings;
    }
    void GetResponsiveFilterGazePoint(tobii::GazePoint& point) const override {
        TRACE_ONCE("IFilters::GetResponsiveFilterGazePoint");
        point = CentreGaze();
    }
    void GetAimAtGazeFilterGazePoint(tobii::GazePoint& point,
                                     float& stability) const override {
        TRACE_ONCE("IFilters::GetAimAtGazeFilterGazePoint");
        point = CentreGaze();
        stability = 0.0f;
    }
    void GetBilateralFilterGazePoint(tobii::GazePoint& point) const override {
        TRACE_ONCE("IFilters::GetBilateralFilterGazePoint");
        point = CentreGaze();
    }
};

Filters g_filters;

struct Api final : tobii::ITobiiGameIntegrationApi {
    tobii::ITrackerController* GetTrackerController() override {
        TRACE_ONCE("ITobiiGameIntegrationApi::GetTrackerController");
        return &g_trackerController;
    }
    tobii::IStreamsProvider* GetStreamsProvider() override {
        TRACE_ONCE("ITobiiGameIntegrationApi::GetStreamsProvider");
        return &g_streamsProvider;
    }
    tobii::IFeatures* GetFeatures() override {
        TRACE_ONCE("ITobiiGameIntegrationApi::GetFeatures");
        return &g_features;
    }
    tobii::IStatistics* GetStatistics() override {
        TRACE_ONCE("ITobiiGameIntegrationApi::GetStatistics");
        return &g_statistics;
    }
    tobii::IFilters* GetFilters() override {
        TRACE_ONCE("ITobiiGameIntegrationApi::GetFilters");
        return &g_filters;
    }
    bool IsInitialized() override { return Mod::Instance().IsStarted(); }

    // The game's per-frame pump, and so the mod's frame boundary. Sampling the
    // pipeline here rather than in GetTransformation is what keeps one frame of
    // head motion to one advance of the interpolator, whatever number of times the
    // game reads the transformation back within the frame.
    void Update() override {
        TRACE_ONCE("ITobiiGameIntegrationApi::Update");
        FramePump::Instance().Advance();
    }

    void Shutdown() override {
        TRACE_ONCE("ITobiiGameIntegrationApi::Shutdown");
        Mod::Instance().Shutdown();
    }
};

Api g_apiInstance;

tobii::ITobiiGameIntegrationApi* Acquire(const char* game_name, int major, int minor,
                                         int revision) {
    Mod& mod = Mod::Instance();
    mod.OpenLog();
    static std::atomic<bool> announced{false};
    if (!announced.exchange(true)) {
        Log::Line("Far Cry 6 Head Tracking - the game asked for head tracking API "
                  "%d.%d.%d as \"%s\"",
                  major, minor, revision, game_name ? game_name : "");
    }
    // A version this file does not model is the one case that DOES hand back
    // nothing. The vtables here are the 7.3 ones; serving a request for another
    // version would route the game's calls through slots that hold other
    // functions. Standing down is the same code path the game takes on a machine
    // with no tracker at all, so it carries on running normally - and nothing is
    // started, so no socket is bound and no thread is spawned for an API the mod
    // will not answer.
    if (!tobii::IsSupportedApiVersion(major, minor)) {
        Log::Line("ERROR: this mod speaks head tracking API %d.%d and the game asked for "
                  "%d.%d.%d. It is standing down, and the game will run as if no eye "
                  "tracker were present.",
                  tobii::kApiMajor, tobii::kApiMaxMinor, major, minor, revision);
        return nullptr;
    }

    // A start failure is not a reason to hand back nothing: a null API is a code
    // path the game takes on machines with no tracker at all, and it is the one
    // that leaves it running normally. Report no connected tracker instead.
    mod.EnsureStarted();
    g_trackerInfo = MakeTrackerInfo(g_trackedWindow, Mod::Instance().IsStarted());
    return &g_apiInstance;
}

}  // namespace

}  // namespace FarCry6HeadTracking

// The game resolves exactly one symbol out of this DLL and reaches the rest through
// the returned object. The two internal spellings are exported alongside it because
// the SDK's own dynamic loader has been shipped resolving either.
extern "C" {

__declspec(dllexport) tobii::ITobiiGameIntegrationApi* __cdecl GetApi(
    const char* game_name, int major, int minor, int revision, const uint16_t* license,
    uint32_t license_size, bool analytical_use) {
    (void)license;
    (void)license_size;
    (void)analytical_use;
    return FarCry6HeadTracking::Acquire(game_name, major, minor, revision);
}

__declspec(dllexport) tobii::ITobiiGameIntegrationApi* __cdecl GetApiInternal(
    const char* game_name, int major, int minor, int revision, const uint16_t* license,
    uint32_t license_size, bool analytical_use) {
    (void)license;
    (void)license_size;
    (void)analytical_use;
    return FarCry6HeadTracking::Acquire(game_name, major, minor, revision);
}

__declspec(dllexport) tobii::ITobiiGameIntegrationApi* __cdecl GetApiDynamic(
    const char* game_name, const char* dll_path, int major, int minor, int revision,
    const uint16_t* license, uint32_t license_size, bool analytical_use) {
    (void)dll_path;
    (void)license;
    (void)license_size;
    (void)analytical_use;
    return FarCry6HeadTracking::Acquire(game_name, major, minor, revision);
}

}  // extern "C"
