// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_adapter.h"
#include "build_profile.h"
#include "camera_pose.h"
#include "frame_pump.h"
#include "headlight.h"
#include "reticle.h"
#include "logging.h"
#include "mod.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/math/quat4.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace FarCry6HeadTracking {
namespace {

using cameraunlock::camera::LeanClamp;
using cameraunlock::camera::LeanClampSettings;
using cameraunlock::camera::LeanObstruction;
using cameraunlock::hooks::HookManager;
using cameraunlock::hooks::HookStatus;
using cameraunlock::math::Quat4;

using UpdateFn = void (*)(void*, float, unsigned);
using RotationFn = void (*)(void*, const Quat4*, Quat4*);
using SetAnglesFn = void (*)(CameraParameters*, const Vec3*);
using RenderFn = void (*)(const CameraParameters*, void*, const float*, float, const void*);

UpdateFn g_update = nullptr;
RotationFn g_rotation = nullptr;
SetAnglesFn g_setAngles = nullptr;
RenderFn g_render = nullptr;
uintptr_t g_module = 0;
const Offsets* g_offsets = nullptr;

// Set once from the settings before the hooks go in, and read-only after.
bool g_collisionEnabled = false;
LeanClampSettings g_leanClamp;
uint32_t g_collisionMask = 0;

struct UpdateContext {
    void* camera = nullptr;
    void* extended_view = nullptr;
    float dt = 0.0f;
    bool head_active = false;
    Quat4 clean_view;
    Quat4 tracked_view;
    Quat4 yaw_correction;
};

// Width over height of the drawn frame, read from the viewport the render builder
// divides for its own projection.
std::atomic<float> g_frameAspect{16.0f / 9.0f};
thread_local UpdateContext g_updateContext;

struct RenderPose {
    const CameraParameters* camera = nullptr;
    // The clean view the pose was computed for, which is what identifies a copy.
    Vec3 eye;
    Vec3 forward;
    tobii::Transformation pose{};
    Quat4 yaw_correction;
    Vec3 offset;
};

// The camera update writes two camera slots on alternate frames, and a render worker
// draws copies of them. With the sights up the frame on screen comes from such a copy,
// so a pose found only by the camera's address never reaches it. A copy carries the
// same clean eye and forward as the camera it was taken from, and the last few poses
// are kept so a copy taken a frame before the latest update still finds its own.
constexpr size_t kPoseHistory = 4;
std::mutex g_poseMutex;
std::array<RenderPose, kPoseHistory> g_poses;
size_t g_nextPose = 0;

// Published from the camera update, read by the frame pump.
std::atomic<bool> g_aiming{false};
std::atomic<unsigned long long> g_aimingStamp{0};
constexpr unsigned long long kAimingFreshMs = 200;
std::atomic<float> g_zoomFactor{1.0f};
std::atomic<unsigned long long> g_zoomStamp{0};

template <typename Fn>
Fn Native(const NativeFunction& function) {
    return reinterpret_cast<Fn>(g_module + function.rva);
}

struct QueryFilter {
    void* vtable;
    uint32_t mask;
    uint32_t flags;
};
struct QueryHits {
    const uint8_t* data = nullptr;
    uint32_t capacity = 0;
    uint32_t count = 0;
};

struct QueryContext {
    void* world;
    void* ignored_collider;
    uint32_t mask;
};

LeanObstruction QueryLean(void* context, const Vec3& start,
                         const Vec3& direction, float distance) {
    const auto& query = *static_cast<QueryContext*>(context);
    QueryFilter filter{};
    Native<void (*)(QueryFilter*, uint32_t, uint32_t)>(g_offsets->query_filter_construct)(&filter, query.mask, 0);
    QueryHits hits;
    const Vec3 segment = direction * distance;
    Native<void (*)(void*, const Vec3*, const Vec3*, QueryHits*, const QueryFilter*,
                    void*, uint32_t, bool)>(g_offsets->world_ray_query)(
        query.world, &start, &segment, &hits, &filter, query.ignored_collider, 0x13, false);

    LeanObstruction result{true, false, distance};
    for (uint32_t i = 0; i < (hits.count & 0x7fffffff); ++i) {
        Vec3 point;
        std::memcpy(&point, hits.data + i * 0x40, sizeof(point));
        const float hitDistance = std::max(0.0f, Vec3::Dot(point - start, direction));
        result.blocked = true;
        result.distance = std::min(result.distance, hitDistance);
    }
    Native<void (*)(QueryHits*)>(g_offsets->query_hits_destroy)(&hits);
    Native<void (*)(QueryFilter*)>(g_offsets->query_filter_destroy)(&filter);
    return result;
}

// How far along the clean aim the shot stops. The reticle marks that POINT: with the
// eye leaned off the gun, a direction alone projects to a different place, and the
// mark slides off the thing it sits on, worse the closer the surface.
float AimDistance(const QueryContext& query, const Vec3& start, const Vec3& direction,
                  float reach) {
    QueryFilter filter{};
    Native<void (*)(QueryFilter*, uint32_t, uint32_t)>(g_offsets->query_filter_construct)(&filter, query.mask, 0);
    QueryHits hits;
    const Vec3 segment = direction * reach;
    Native<void (*)(void*, const Vec3*, const Vec3*, QueryHits*, const QueryFilter*,
                    void*, uint32_t, bool)>(g_offsets->world_ray_query)(
        query.world, &start, &segment, &hits, &filter, query.ignored_collider, 0x13, false);
    float nearest = reach;
    for (uint32_t i = 0; i < (hits.count & 0x7fffffff); ++i) {
        Vec3 point;
        std::memcpy(&point, hits.data + i * 0x40, sizeof(point));
        // Measured from the contact's own position along the aim, so it is a distance
        // by construction rather than a field that might mean something else.
        const float distance = Vec3::Dot(point - start, direction);
        if (distance > 0.05f) nearest = std::min(nearest, distance);
    }
    Native<void (*)(QueryHits*)>(g_offsets->query_hits_destroy)(&hits);
    Native<void (*)(QueryFilter*)>(g_offsets->query_filter_destroy)(&filter);
    return nearest;
}

// The camera update resolves the owner through camera_owner and ORs two answers into
// its aim state: the byte at +0x2afa is the sights (read at sights_read) and virtual
// slot 0x230 (called at binoculars_call) is the sight device, which in this game is
// the phone - Far Cry 6 has no binoculars. That aim state is what the update pauses
// the extended view on when the profile's ExtendedViewInIronsight is off.
//
// The mod's own definition is narrower, because all it drives is easing the lean out.
// The sights byte is set by the aim button even with nothing in hand, which has no
// sight line to keep the eye on, and the phone is a screen the player reads.
void PublishAiming(void* camera) {
    // The update itself skips a camera whose +0x1a is clear before it asks for the
    // owner. A camera with no owner has no aim state, and must not overwrite the one
    // that does.
    if (static_cast<uint8_t*>(camera)[0x1a] == 0) return;
    void* owner = Native<void* (*)(void*)>(g_offsets->camera_owner)(camera);
    if (!owner) return;
    using SightDeviceFn = bool (*)(void*);
    const auto* vtable = *reinterpret_cast<SightDeviceFn**>(owner);
    const bool sights = static_cast<uint8_t*>(owner)[0x2afa] != 0;
    const bool phone = vtable[0x230 / 8](owner);
    const bool weapon = ReticleWeaponOut();
    const bool aiming = sights && weapon;
    static thread_local int lastState = -1;
    const int state = (sights ? 1 : 0) | (phone ? 2 : 0) | (weapon ? 4 : 0);
    if (state != lastState) {
        lastState = state;
        Log::Line("Aim state: sights=%d phone=%d weapon=%d", sights ? 1 : 0, phone ? 1 : 0,
                  weapon ? 1 : 0);
    }
    g_aiming.store(aiming, std::memory_order_relaxed);
    g_aimingStamp.store(GetTickCount64(), std::memory_order_relaxed);
}

void HookedUpdate(void* camera, float dt, unsigned flags) {
    auto* extendedView = *reinterpret_cast<uint8_t**>(g_module + g_offsets->extended_view_instance);
    uint8_t inIronsight = 0;
    uint8_t aimAtGaze = 0;
    uint8_t binocularsAtGaze = 0;
    if (extendedView) {
        // This camera path ignores gaze capability and redirects aim to the tracked view.
        // Suppress it for this update without changing the player's saved settings.
        aimAtGaze = extendedView[0x62];
        binocularsAtGaze = extendedView[0x64];
        extendedView[0x62] = 0;
        extendedView[0x64] = 0;
        // +0x61 is ExtendedViewInIronsight. Off, this update pauses the extended view
        // while aiming, and head tracking has to carry on through the aim, so the
        // update always sees it on.
        inIronsight = extendedView[0x61];
        extendedView[0x61] = 1;
    }
    g_updateContext = UpdateContext{};
    g_updateContext.camera = camera;
    g_updateContext.dt = dt;
    g_update(camera, dt, flags);
    PublishAiming(camera);
    g_updateContext = {};
    if (extendedView) {
        extendedView[0x61] = inIronsight;
        extendedView[0x62] = aimAtGaze;
        extendedView[0x64] = binocularsAtGaze;
    }
}

void HookedRotation(void* extendedView, const Quat4* reference, Quat4* result) {
    g_rotation(extendedView, reference, result);
    g_updateContext.head_active = true;
    g_updateContext.extended_view = extendedView;
    const auto* quats = reinterpret_cast<const Quat4*>(static_cast<uint8_t*>(extendedView) + 0xf0);
    g_updateContext.clean_view = quats[0];
    g_updateContext.tracked_view = quats[1];
    const bool trackingAllowed = Mod::Instance().TrackingAllowed();
    g_updateContext.yaw_correction = Quat4::Identity();
    if (trackingAllowed && !Mod::Instance().WorldSpaceYaw()) {
        // The native rotation applies +0xd0 about reference up, then +0xd4 about
        // local X. Use its applied yaw, including its pause and ADS blending.
        float yaw;
        std::memcpy(&yaw, static_cast<const uint8_t*>(extendedView) + 0xd0, sizeof(yaw));
        g_updateContext.yaw_correction = LocalYawCorrection(quats[0], *reference, yaw);
    }
    NoteHeadDelta(quats[0], g_updateContext.yaw_correction * quats[1]);
    if (!trackingAllowed) return;

    // The native HUD projection reads this quaternion separately from the camera.
    const float halfRoll = -FramePump::Instance().Current().rotation.roll_degrees *
                           static_cast<float>(cameraunlock::math::kDegToRad) * 0.5f;
    auto* hudRotation = reinterpret_cast<Quat4*>(static_cast<uint8_t*>(extendedView) + 0x110);
    *hudRotation = *hudRotation * Quat4(0.0f, std::sin(halfRoll), 0.0f, std::cos(halfRoll));
}

// settings+0x0c is the live horizontal field of view and +0x1c the game's un-zoomed
// one. Iron sights narrow +0x0c from 1.3090 to 1.1345 while +0x1c holds 1.3090, and
// the two agree at the hip. The native extended view applies the same yaw either
// way, so without this a head turn moves the picture further through the sights.
void PublishZoom(const CameraParameters& camera) {
    float fov;
    float base;
    std::memcpy(&fov, camera.settings + 0x0c, sizeof(fov));
    std::memcpy(&base, camera.settings + 0x1c, sizeof(base));
    constexpr float kPi = 3.14159265f;
    const bool valid = std::isfinite(fov) && std::isfinite(base) && fov > 0.0f && fov < kPi &&
                       base > 0.0f && base < kPi;
    static thread_local bool logged = false;
    static thread_local bool loggedInvalid = false;
    if (!valid) {
        if (!loggedInvalid) {
            loggedInvalid = true;
            Log::Line("ERROR: field of view unreadable (live %g, base %g); head tracking is "
                      "not scaled to the zoom", fov, base);
        }
        return;
    }
    const float factor = cameraunlock::camera::FovZoomFactor(std::tan(fov * 0.5f),
                                                             std::tan(base * 0.5f));
    if (!logged) {
        logged = true;
        Log::Line("Zoom: live fov %.4f rad, base fov %.4f rad, both horizontal, frame aspect "
                  "%.4f, factor %.4f", fov, base, g_frameAspect.load(std::memory_order_relaxed),
                  factor);
    }
    g_zoomFactor.store(factor, std::memory_order_relaxed);
    g_zoomStamp.store(GetTickCount64(), std::memory_order_relaxed);
}

void HookedSetAngles(CameraParameters* camera, const Vec3* angles) {
    g_setAngles(camera, angles);
    if (!g_updateContext.camera) return;

    RenderPose next;
    if (g_updateContext.head_active) {
        PublishZoom(*camera);
        next.camera = camera;
        next.eye = camera->eye;
        next.forward = camera->forward;
        next.pose = FramePump::Instance().Current();
        next.yaw_correction = g_updateContext.yaw_correction;
        CameraParameters yawCamera = *camera;
        RotateCameraBasis(yawCamera, next.yaw_correction);
        void* ignoredCollider = nullptr;
        void* owner = Native<void* (*)(void*)>(g_offsets->camera_owner)(g_updateContext.camera);
        if (owner) {
            void* body = Native<void* (*)(void*, unsigned)>(g_offsets->owner_body)(owner, 0);
            if (body) ignoredCollider = Native<void* (*)(void*)>(g_offsets->body_collider)(body);
        }

        static thread_local LeanClamp clamp;
        static thread_local void* lastCamera = nullptr;
        if (lastCamera != g_updateContext.camera) {
            clamp.Reset();
            lastCamera = g_updateContext.camera;
        }
        float nearPlane;
        std::memcpy(&nearPlane, camera->settings + 4, sizeof(nearPlane));
        void* world = *reinterpret_cast<void**>(g_module + g_offsets->world);
        if (!g_collisionEnabled) {
            next.offset = CameraLean(yawCamera, next.pose.position);
        } else if (world) {
            clamp.SetSettings({std::max(g_leanClamp.skin, nearPlane + 0.05f), g_leanClamp.release_smoothing});
            // The world query requires the camera-update thread; rendering may run on a worker.
            QueryContext context{world, ignoredCollider, g_collisionMask};
            next.offset = clamp.Apply(camera->eye, CameraLean(yawCamera, next.pose.position),
                                      g_updateContext.dt, QueryLean, &context);
        } else {
            Log::Line("ERROR: camera world query unavailable; positional tracking suppressed");
        }

        // The reticle, projected basis to basis from the camera the frame is drawn
        // with: the leaned eye and the rolled axes the render hook writes.
        if (world) {
            constexpr float kReach = 500.0f;
            const Quat4 delta = g_updateContext.tracked_view * g_updateContext.clean_view.Inverse();
            const Vec3 aim = delta.Inverse().Rotate(camera->forward);
            QueryContext context{world, ignoredCollider, static_cast<uint32_t>(kCollisionLayerMask)};
            const float distance = AimDistance(context, camera->eye, aim, kReach);
            const Vec3 impact = camera->eye + aim * distance;
            CameraParameters nativeRolled = *camera;
            ApplyCameraRoll(nativeRolled, next.pose.rotation.roll_degrees);
            CameraParameters rolled = yawCamera;
            ApplyCameraRoll(rolled, next.pose.rotation.roll_degrees);
            float fov;
            std::memcpy(&fov, camera->settings + 0x0c, sizeof(fov));
            // +0x0c is the horizontal field of view: at yaw 15 and pitch 10 the rotation
            // term matches the weapon's own reticle position exactly.
            const float tanH = std::tan(fov * 0.5f);
            const float tanV = tanH / g_frameAspect.load(std::memory_order_relaxed);
            const Vec3 leaned = impact - (camera->eye + next.offset);
            const Vec3 straight = impact - camera->eye;
            const float depth = Vec3::Dot(leaned, rolled.forward);
            const float straightDepth = Vec3::Dot(straight, nativeRolled.forward);
            if (tanH > 0.0f && depth > 0.05f && straightDepth > 0.05f) {
                const float x = Vec3::Dot(leaned, rolled.right) / depth / tanH;
                const float y = -Vec3::Dot(leaned, rolled.up) / depth / tanV;
                const float rotX = Vec3::Dot(straight, nativeRolled.right) / straightDepth / tanH;
                const float rotY = -Vec3::Dot(straight, nativeRolled.up) / straightDepth / tanV;
                NoteReticleParallax(x - rotX, y - rotY);

                static thread_local unsigned aimFrames = 0;
                if (++aimFrames % 300 == 1) {
                    Log::Line("AIMGEO dist=%.2f lean=(%.3f,%.3f,%.3f) rot=(%.4f,%.4f) "
                              "full=(%.4f,%.4f) parallax=(%.4f,%.4f)",
                              distance, next.offset.x, next.offset.y, next.offset.z, rotX, rotY,
                              x, y, x - rotX, y - rotY);
                }
            }
        }
        static thread_local unsigned frames = 0;
        if (++frames % 600 == 1) {
            // The headlight turns by the difference of the extended view's rotations, so
            // the rotation convention is checked against the camera it produced.
            const Vec3 predicted = g_updateContext.tracked_view.Rotate(Vec3(0.0f, 1.0f, 0.0f));
            Log::Line("Yaw check: mode=%s correction=%.4f,%.4f,%.4f,%.4f forward=%.4f,%.4f,%.4f",
                      Mod::Instance().WorldSpaceYaw() ? "world" : "local",
                      next.yaw_correction.x, next.yaw_correction.y,
                      next.yaw_correction.z, next.yaw_correction.w,
                      yawCamera.forward.x, yawCamera.forward.y, yawCamera.forward.z);
            Log::Line("View check: tracked rotation forward=%.3f,%.3f,%.3f camera forward=%.3f,%.3f,%.3f",
                      predicted.x, predicted.y, predicted.z, camera->forward.x,
                      camera->forward.y, camera->forward.z);
            float fovs[4];
            std::memcpy(fovs, camera->settings + 0x0c, sizeof(fovs));
            Log::Line("View check: fov fields %.4f %.4f %.4f %.4f", fovs[0], fovs[1], fovs[2],
                      fovs[3]);
            Log::Line("Render tracking: roll=%.2f lean=%.3f,%.3f,%.3f blocked=%d near=%.3f",
                      next.pose.rotation.roll_degrees, next.offset.x, next.offset.y,
                      next.offset.z, clamp.InContact() ? 1 : 0, nearPlane);
        }
    }
    std::lock_guard<std::mutex> lock(g_poseMutex);
    if (next.camera) {
        g_poses[g_nextPose] = next;
        g_nextPose = (g_nextPose + 1) % kPoseHistory;
    } else {
        // An update of a tracked camera without the head applied (leaving gameplay)
        // takes its poses off every render of it.
        for (RenderPose& entry : g_poses) {
            if (entry.camera == camera) entry = RenderPose{};
        }
    }
}

bool SameView(const CameraParameters& camera, const RenderPose& pose) {
    return camera.eye.x == pose.eye.x && camera.eye.y == pose.eye.y &&
           camera.eye.z == pose.eye.z && camera.forward.x == pose.forward.x &&
           camera.forward.y == pose.forward.y && camera.forward.z == pose.forward.z;
}

void HookedRender(const CameraParameters* camera, void* output, const float* viewport,
                  float aspect, const void* projection) {
    RenderPose pose;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        for (size_t age = 1; age <= kPoseHistory; ++age) {
            const RenderPose& entry = g_poses[(g_nextPose + kPoseHistory - age) % kPoseHistory];
            if (entry.camera && (entry.camera == camera || SameView(*camera, entry))) {
                pose = entry;
                break;
            }
        }
    }
    if (pose.camera && viewport[1] > 0.0f) {
        g_frameAspect.store(viewport[0] / viewport[1], std::memory_order_relaxed);
    }
    if (!pose.camera || !Mod::Instance().TrackingAllowed()) {
        g_render(camera, output, viewport, aspect, projection);
        return;
    }

    // Only the render builder sees the modified copy. Repeated passes start clean.
    CameraParameters tracked = *camera;
    RotateCameraBasis(tracked, pose.yaw_correction);
    ApplyCameraRoll(tracked, pose.pose.rotation.roll_degrees);
    tracked.eye = tracked.eye + pose.offset;
    g_render(&tracked, output, viewport, aspect, projection);
}

}  // namespace

float CameraZoomFactor() {
    // Stale outside gameplay, where no pose reaches the camera anyway.
    if (GetTickCount64() - g_zoomStamp.load(std::memory_order_relaxed) > kAimingFreshMs) {
        return 1.0f;
    }
    return g_zoomFactor.load(std::memory_order_relaxed);
}

bool CameraAiming() {
    // A camera update that has stopped arriving (menu, cinematic, loading) reads as
    // not aiming, which is the stock direction to fail in.
    return g_aiming.load(std::memory_order_relaxed) &&
           GetTickCount64() - g_aimingStamp.load(std::memory_order_relaxed) <= kAimingFreshMs;
}

bool StartCameraAdapter(const Config& config) {
    g_collisionEnabled = config.collision_enabled;
    g_leanClamp = config.lean_clamp;
    // Passed through as written: which layers block is the game's to say.
    g_collisionMask = static_cast<uint32_t>(config.collision_channel);
    if (g_collisionEnabled) {
        Log::Line("Lean collision: on, margin %.3f m, release smoothing %.2f, layer mask 0x%X",
                  g_leanClamp.skin, g_leanClamp.release_smoothing, g_collisionMask);
    } else {
        Log::Line("Lean collision: off (CollisionEnabled=false), so leaning can move the view through walls");
    }
    auto* module = GetModuleHandleW(L"FC_m64d3d12.dll");
    const BuildProfile* profile = MatchRunningBuild(module);
    if (!profile) return false;
    g_module = reinterpret_cast<uintptr_t>(module);
    g_offsets = profile->offsets;
    const Offsets& o = *g_offsets;
    for (const NativeFunction& check :
         {o.camera_update, o.extended_view_rotation, o.set_camera_angles, o.render_camera,
          o.world_ray_query, o.query_filter_construct, o.query_filter_destroy,
          o.query_hits_destroy, o.camera_owner, o.owner_body, o.body_collider, o.sights_read,
          o.binoculars_call}) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(g_module + check.rva);
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < 32; ++i) hash = (hash ^ bytes[i]) * 16777619u;
        if (hash != check.prefix_hash) {
            Log::Line("ERROR: camera function verification failed at %llX; tracking disabled",
                      static_cast<unsigned long long>(check.rva));
            return false;
        }
    }
    auto& hooks = HookManager::Instance();
    auto status = hooks.Initialize();
    if (status != HookStatus::Ok && status != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("ERROR: camera hook initialization: %s", HookStatusToString(status));
        return false;
    }
    struct Hook { uintptr_t rva; void* detour; void** original; };
    const std::array<Hook, 4> targets{{
        {o.camera_update.rva, reinterpret_cast<void*>(&HookedUpdate), reinterpret_cast<void**>(&g_update)},
        {o.extended_view_rotation.rva, reinterpret_cast<void*>(&HookedRotation), reinterpret_cast<void**>(&g_rotation)},
        {o.set_camera_angles.rva, reinterpret_cast<void*>(&HookedSetAngles), reinterpret_cast<void**>(&g_setAngles)},
        {o.render_camera.rva, reinterpret_cast<void*>(&HookedRender), reinterpret_cast<void**>(&g_render)},
    }};
    size_t created = 0;
    for (const auto& target : targets) {
        void* address = reinterpret_cast<void*>(g_module + target.rva);
        status = hooks.CreateHook(address, target.detour, target.original);
        if (status != HookStatus::Ok) break;
        ++created;
    }
    if (status == HookStatus::Ok) {
        for (const auto& target : targets) {
            status = hooks.EnableHook(reinterpret_cast<void*>(g_module + target.rva));
            if (status != HookStatus::Ok) break;
        }
    }
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: camera hook installation: %s", HookStatusToString(status));
        for (size_t i = 0; i < created; ++i) {
            void* address = reinterpret_cast<void*>(g_module + targets[i].rva);
            hooks.DisableHook(address);
            hooks.RemoveHook(address);
        }
        return false;
    }
    Log::Line("Camera adapter active: native yaw/pitch, render roll and %s XYZ",
              g_collisionEnabled ? "collision-clamped" : "unclamped");
    return StartReticle(g_module, o) && StartHeadlight(g_module, o, config.light);
}

}  // namespace FarCry6HeadTracking
