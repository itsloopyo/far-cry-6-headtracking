# Changelog

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
