# Changelog

## [Unreleased]

### Added

- `LightFollowsHead` and `LightMultiplier` in the `[Light]` section of `FarCry6HeadTracking.ini`. v0.1.0 already turned the flashlight with your head, 1.5 times as far as the head turn, with no setting for it. Both default to exactly that, so the beam behaves as before until you change them. `LightFollowsHead=false` leaves the beam on your aim. `LightMultiplier` sets how far the beam turns: `1.0` matches the view, `0` leaves the beam on the aim, and the largest value it takes is `5`.

### Changed

- `FarCry6HeadTracking.ini` has a new layout. The first time this version starts, it converts the file once into the new layout and keeps the file as it was beside it as `FarCry6HeadTracking.ini.pre-canonical`. `FarCry6HeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The chords were always on before; now each one can be changed or removed like any other key.
- Settings moved: `[General] Port` is `[Network] UdpPort`, `[Gameplay] WorldSpaceYaw` is `[General] WorldSpaceYaw`, the `[Position]` limits are `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` and `PositionLimitZBack`, and `[Hotkeys] Toggle`, `CycleMode` and `YawMode` are `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`. `LimitY` bounded both directions, so it becomes both `PositionLimitY` and `PositionLimitYDown`. `[Position] Enabled` chose the tracking mode at startup; that is now the pair `RotationEnabled` and `PositionEnabled`. The conversion carries every one of these values over.
- The tracking mode that Page Up or Ctrl+Shift+G selects is now saved and comes back at the next start, as the yaw mode already did.
- An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `FarCry6HeadTracking.ini.pre-canonical` back over `FarCry6HeadTracking.ini`, which restores the old file.
- `uninstall.cmd` keeps `bin\FarCry6HeadTracking.ini` and its `.pre-canonical` copies, so your settings survive a reinstall.
- Since v0.1.0, `[Gameplay] AdsMode` and `[Hotkeys] AdsMode` are no longer read, and neither Insert nor Ctrl+Shift+U cycles an ADS mode: head tracking carries on through the sights in every case, and the lean eases out while they are up (faefd67).
- Since v0.1.0, a `[Hotkeys] YawMode` on the same key as `[Hotkeys] AdsMode` switches the yaw mode; v0.1.0 left it unbound (faefd67).

### Removed

- The sensitivity and axis inversion settings (`[Sensitivity]` and `[Position] SensitivityX/Y/Z`, `InvertX/Y/Z`). Set these in your tracker app instead. They shipped at 1 and off, so with them at their shipped values the camera moves as it did before.

## [0.0.0] - 2026-09-08

### Added

- Added head tracking for Far Cry 6, driven by OpenTrack over UDP on port 4242.
  Your head moves the view; your mouse or controller keeps the aim. Far Cry 6
  applies head yaw and pitch, and ignores roll and positional lean.
- Added a tracking toggle on `End` or `Ctrl+Shift+Y`, and a tracking mode cycle
  on `Page Up` or `Ctrl+Shift+G`.
- Added `FarCry6HeadTracking.ini` next to the mod, covering the port, per-axis
  sensitivity and inversion, the two smoothing values, the positional limits and
  the hotkeys.
- Added a co-op gate that holds the view still once a second player is in the
  session, so co-op runs the stock camera. Turn it off with `DisableInCoop`.
- Added window centring, so windowed play comes up with the game window centred
  on the work area of its monitor, whatever window position the game last saved.
  A window as large as the work area is left where the game places it.
- Added tracker port recovery, so starting Far Cry 6 while another game still
  holds the port no longer costs a relaunch. The mod checks the port twice a
  second and starts tracking within about a second of the other program closing.
