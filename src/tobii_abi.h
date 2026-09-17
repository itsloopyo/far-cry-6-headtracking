// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// The binary interface Far Cry 6 uses to talk to a head tracker.
//
// The game resolves one symbol, GetApi, out of tobii_gameintegration_x64.dll and
// reaches everything else through the returned object's virtual tables. It accepts
// only API major version 7, minor <= 3, so the layouts below are the 7.3 ones and
// not the later 9.x ones that the published SDK header describes: 7.3 carries two
// extra stream accessors on IStreamsProvider, splits the extended view settings
// into a simple and an advanced struct, and has no IsStreamSupported.
//
// The slot orders here were read out of the DLL the game ships rather than assumed,
// and the interfaces that differ from the published 9.x header carry a note saying
// how. See THIRD-PARTY-NOTICES.md for the two sources behind this file.
//
// Every slot is declared with a unique name. The overload groups the real header
// uses would be emitted in reverse declaration order by MSVC, so spelling them out
// individually is what keeps this file's reading order and the vtable's the same.

namespace tobii {

// The version contract the layouts below encode, and the one the shipped DLL's
// own GetApi enforces: major must be exactly 7, minor at most 3. It is checked
// rather than assumed because the slots differ between 7.3 and the published
// 9.x - answering a 9.x request with these tables would send the game through a
// vtable whose entries are in other places, which is a crash in the player's
// game rather than a missing feature.
constexpr int kApiMajor = 7;
constexpr int kApiMaxMinor = 3;

inline bool IsSupportedApiVersion(int major, int minor) {
    return major == kApiMajor && minor <= kApiMaxMinor;
}

// Degrees. Yaw increases turning the head right, pitch increases looking up, roll
// increases tilting the head right.
struct Rotation {
    float yaw_degrees;
    float pitch_degrees;
    float roll_degrees;
};

// X increases moving the head right, Y moving up, Z moving away from the screen.
struct Position {
    float x;
    float y;
    float z;
};

struct Transformation {
    Rotation rotation;
    Position position;
};

// Far Cry 6 consumes extended-view angles in radians; the head stream uses degrees.
struct ExtendedViewTransformation {
    struct {
        float yaw_radians;
        float pitch_radians;
        float roll_radians;
    } rotation;
    Position position;
};

struct HeadPose {
    Rotation rotation;
    Position position;
    int64_t timestamp_microseconds;
};

struct GazePoint {
    int64_t timestamp_microseconds;
    float x;
    float y;
};

struct Rectangle {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct Dimensions {
    int32_t width;
    int32_t height;
};

struct TrackerInfo {
    int32_t type;
    int32_t capabilities;
    Rectangle display_rect_in_os_coordinates;
    Dimensions display_size_mm;
    const char* url;
    const char* friendly_name;
    const char* monitor_name_in_os;
    const char* model_name;
    const char* generation;
    const char* serial_number;
    const char* firmware_version;
    bool is_attached;
};

// TrackerInfo::type. The game reads it to decide what the device can do; a screen
// mounted eye tracker is the shape whose capability set includes head pose.
enum TrackerType : int32_t {
    kTrackerTypeNone = 0,
    kTrackerTypeEyeTracker = 1,
};

// TrackerInfo::capabilities, as stream bit flags. A flag is 1 << the stream's own
// id, and the two below are the only ones this mod claims, so they are the only
// ones spelled out: the higher ids differ between 7.3 and the published 9.x - the
// eye-info pair is stream 7 here and absent there - and a constant nothing writes
// is not worth being wrong about.
//
// Getting the shift wrong changes nothing the compiler can see. A dense
// from-zero numbering makes this pair 0x0C rather than 0x03, which the game reads
// as OS gaze and gaze: the two streams the mod exists to leave switched off, and
// neither of the two it means to offer.
enum StreamFlags : int32_t {
    kStreamNone = 0,
    kStreamPresence = 1 << 0,
    kStreamHeadPose = 1 << 1,
};

struct IExtendedView {
    virtual ExtendedViewTransformation GetTransformation() = 0;
    virtual bool UpdateAdvancedSettings(const void* settings) = 0;
    virtual void ResetDefaultHeadPose() = 0;
    virtual void SetPaused(bool paused) = 0;
    virtual void SetPausedAfterRecentering() = 0;
    virtual bool GetPaused() = 0;
    virtual bool UpdateSimpleSettings(const void* settings) = 0;
    virtual void GetAdvancedSettings(void* settings) const = 0;
    virtual void GetSimpleSettings(void* settings) const = 0;
    virtual int GetSensitivityGradientAsPolyLine(void* points, int max_points,
                                                 const void* settings) const = 0;
};

struct IFeatures {
    virtual IExtendedView* GetExtendedView() = 0;
};

struct ITrackerController {
    virtual bool GetTrackerInfoByUrl(const char* url, TrackerInfo& info) = 0;
    virtual bool GetTrackerInfo(TrackerInfo& info) = 0;
    virtual void UpdateTrackerInfos() = 0;
    virtual bool GetTrackerInfos(const TrackerInfo*& infos, int& count) = 0;
    // TrackHMD is slot 4 and StopTracking slot 7, not the other way round, and
    // StopTracking returns void. Swapping them answers a TrackHMD call with
    // StopTracking's return value, which tells the game an HMD was engaged.
    virtual bool TrackHMD() = 0;
    virtual bool TrackRectangle(const Rectangle& rectangle) = 0;
    virtual bool TrackWindow(void* window_handle) = 0;
    virtual void StopTracking() = 0;
    virtual bool IsConnected() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual bool TrackTracker(const char* url) = 0;
};

// Slot order comes from the stream each shipped implementation subscribes to: the
// pairs are (stream 1) head pose, (stream 3) gaze, (stream 7) eye info, (stream 6)
// HMD gaze. The eye-info pair is 7.3 only and is gone from the published 9.x
// header, which is why it sits in the middle here rather than at the end.
struct IStreamsProvider {
    virtual int GetHeadPoses(const HeadPose*& poses) = 0;
    virtual bool GetLatestHeadPose(HeadPose& pose) = 0;
    virtual int GetGazePoints(const GazePoint*& points) = 0;
    virtual bool GetLatestGazePoint(GazePoint& point) = 0;
    virtual int GetEyeInfos(const void*& infos) = 0;
    virtual bool GetLatestEyeInfo(void* info) = 0;
    virtual int GetHMDGaze(const void*& gaze) = 0;
    virtual bool GetLatestHMDGaze(void* gaze) = 0;
    virtual bool IsPresent() = 0;
    virtual void SetAutoUnsubscribe(int stream, float timeout_seconds) = 0;
    virtual void UnsetAutoUnsubscribe(int stream) = 0;
    virtual void ConvertGazePoint(const GazePoint& from, GazePoint& to, int from_unit,
                                  int to_unit) = 0;
};

struct Feature {
    int id;
    const char* name;
    bool enabled;
};

// Eight slots, not the seven the 9.x header declares: the shipped 7.3 DLL's own
// SendStatistics wrapper dispatches through slot 7.
struct IStatistics {
    virtual void SetFeatureList(const Feature* features, int count) = 0;
    virtual const char* GetLiteral(int literal) = 0;
    virtual void SendFeatureEnabled(int feature_id) = 0;
    virtual void SendFeatureDisabled(int feature_id) = 0;
    virtual void SendFeaturesState() = 0;
    virtual void StopAllLogging() = 0;
    virtual void ResumeAllLogging() = 0;
    virtual void SendStatistics() = 0;
};

// Sizes are the ones the shipped constructors write.
struct ResponsiveFilterSettings {  // 12 bytes
    bool enabled;
    float responsiveness;
    float threshold;
};

struct AimAtGazeFilterSettings {  // 8 bytes
    bool enabled;
    float threshold;
};

struct BilateralFilterSettings {  // 20 bytes
    int32_t reserved;
    int32_t mode;
    float sigma;
    float radius;
    float limit;
};

// Nine slots: three settings pairs and three filtered gaze points, one set per
// filter. The published 9.x header has only the responsive and aim-at-gaze halves.
//
// Both setters take their struct BY VALUE in the real header, and the two sizes
// pass differently on x64 - the 8-byte one arrives whole in a register while the
// 12- and 20-byte ones arrive as a hidden pointer. Taking each as an opaque
// pointer and never dereferencing it is what makes one declaration safe for both.
struct IFilters {
    virtual const ResponsiveFilterSettings* GetResponsiveFilterSettings() const = 0;
    virtual void SetResponsiveFilterSettings(const void* settings) = 0;
    virtual const AimAtGazeFilterSettings* GetAimAtGazeFilterSettings() const = 0;
    virtual void SetAimAtGazeFilterSettings(const void* settings) = 0;
    virtual const BilateralFilterSettings* GetBilateralFilterSettings() const = 0;
    virtual void SetBilateralFilterSettings(const void* settings) = 0;
    virtual void GetResponsiveFilterGazePoint(GazePoint& point) const = 0;
    virtual void GetAimAtGazeFilterGazePoint(GazePoint& point, float& stability) const = 0;
    virtual void GetBilateralFilterGazePoint(GazePoint& point) const = 0;
};

struct ITobiiGameIntegrationApi {
    virtual ITrackerController* GetTrackerController() = 0;
    virtual IStreamsProvider* GetStreamsProvider() = 0;
    virtual IFeatures* GetFeatures() = 0;
    virtual IStatistics* GetStatistics() = 0;
    virtual IFilters* GetFilters() = 0;
    virtual bool IsInitialized() = 0;
    virtual void Update() = 0;
    virtual void Shutdown() = 0;
};

}  // namespace tobii
