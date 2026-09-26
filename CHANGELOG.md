# Changelog

## [Unreleased]

### Added

- `LightFollowsHead` and `LightMultiplier` in the `[Light]` section of `CameraUnlock.ini`. v0.1.0 already turned the flashlight with your head, 1.5 times as far as the head turn, with no setting for it. Both default to exactly that, so the beam behaves as before until you change them. `LightFollowsHead=false` leaves the beam on your aim. `LightMultiplier` sets how far the beam turns: `1.0` matches the view, `0` leaves the beam on the aim, and the largest value it takes is `5`.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed

- Settings move to `bin\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `FarCry6HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `FarCry6HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `FarCry6HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- An older version of the mod reads `FarCry6HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `FarCry6HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `FarCry6HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The chords were always on before; now each one can be changed or removed like any other key.
- Settings were renamed on the way: `[General] Port` is `[Network] UdpPort`, `[Gameplay] WorldSpaceYaw` is `[General] WorldSpaceYaw`, the `[Position]` limits are `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` and `PositionLimitZBack`, and `[Hotkeys] Toggle`, `CycleMode` and `YawMode` are `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`. `LimitY` bounded both directions, so it becomes both `PositionLimitY` and `PositionLimitYDown`. `[Position] Enabled` chose the tracking mode at startup; that is now the pair `RotationEnabled` and `PositionEnabled`. The import carries every one of these values over.
- The tracking mode that Page Up or Ctrl+Shift+G selects is now saved and comes back at the next start, as the yaw mode already did.
- `uninstall.cmd` keeps `bin\CameraUnlock.ini` and `bin\FarCry6HeadTracking.ini`, so your settings survive a reinstall.
- When there is no `CameraUnlock.ini` and no `FarCry6HeadTracking.ini`, and the mod cannot create `CameraUnlock.ini` because the `bin` folder cannot be written, the mod now starts on its default settings and saves nothing that session. Earlier versions did not start at all when they could not create their settings file.
- Since v0.1.0, `[Gameplay] AdsMode` and `[Hotkeys] AdsMode` are no longer read, and neither Insert nor Ctrl+Shift+U cycles an ADS mode: head tracking carries on through the sights in every case, and the lean eases out while they are up (faefd67).
- Since v0.1.0, a `[Hotkeys] YawMode` on the same key as `[Hotkeys] AdsMode` switches the yaw mode; v0.1.0 left it unbound (faefd67).

### Removed

- The sensitivity and axis inversion settings (`[Sensitivity]` and `[Position] SensitivityX/Y/Z`, `InvertX/Y/Z`). Set these in your tracker app instead. They shipped at 1 and off, so with these settings at their shipped defaults the camera moves as it did before.

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
