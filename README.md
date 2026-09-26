# Far Cry 6 Head Tracking

![Far Cry 6 running with this mod](https://raw.githubusercontent.com/itsloopyo/far-cry-6-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Far Cry 6 that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - your head moves the view, your mouse or controller keeps the aim
- **Six-axis head tracking** - yaw, pitch, roll and positional lean
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- **[Far Cry 6](https://store.ubisoft.com/us/far-cry-6/)** on PC, from Steam or from
  Ubisoft Connect. The PC Game Pass copy installs and launches through Ubisoft
  Connect, so it is the Ubisoft Connect build. The game folder must contain
  `bin\FarCry6.exe` and `bin\tobii_gameintegration_x64.dll`.
- **Something that sends the OpenTrack UDP pose** -
  [OpenTrack](https://github.com/opentrack/opentrack/releases) itself with a
  webcam, or a phone app or hardware tracker configured to send that protocol.
- **Windows 10 or 11**, 64-bit.

There is no mod loader to install. The mod is a single DLL that takes the place of
the head tracking library the game already loads, and the installer keeps the
original beside it as `tobii_gameintegration_x64.dll.backup`.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Far Cry 6**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the
   [Releases](https://github.com/itsloopyo/far-cry-6-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds the game, backs up the original library and
   copies the mod in.
4. Configure OpenTrack to output UDP to `127.0.0.1` on port **4242**, as described
   under [Setting Up OpenTrack](#setting-up-opentrack).
5. Launch the game.

**If the installer cannot find your game**, point it at the folder yourself. Either
set the environment variable, or pass the path as the first argument:

```powershell
$env:FAR_CRY_6_PATH = 'D:\Games\Far Cry 6'
.\install.cmd
```

```powershell
.\install.cmd "D:\Games\Far Cry 6"
```

Mod managers do not deploy this mod. Vortex and Mod Organizer deploy into one fixed
folder inside a game, and this mod has to replace a file the game ships in its own
`bin` folder, which no manager can do or roll back. Use the installer.

**Success looks like:** `bin\tobii_gameintegration_x64.dll.backup` appears next to
`bin\FarCry6.exe`, and after launching the game once,
`bin\FarCry6HeadTracking.log` opens with a line naming the head tracking API the
game asked for.

### Manual Installation

The installer ZIP contains `plugins\tobii_gameintegration_x64.dll`.

1. Close the game.
2. In `<Far Cry 6>\bin`, rename `tobii_gameintegration_x64.dll` to
   `tobii_gameintegration_x64.dll.backup`.
3. Copy the mod's `tobii_gameintegration_x64.dll` into that folder.

Some installs also carry a `bin_plus` folder holding a second copy of the game
binaries. The installer always deploys into `bin`. If the log never appears after a
launch, repeat the two steps above in `bin_plus` as well.

## Setting Up OpenTrack

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. **Input:** pick your tracker.
3. **Output:** `UDP over network`.
4. Open the output's settings and set the address to `127.0.0.1` and the port to
   **4242**.
5. Press **Start**.

The mod listens on 4242 unless `UdpPort` in `CameraUnlock.ini` says otherwise. If
something else on the PC already has that port, change it in both places.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on `UDP over network`, address `127.0.0.1`, port **4242**.

### Webcam Setup

Set OpenTrack's **Input** to `neuralnet tracker`. It tracks your face from a plain
webcam and needs no markers, no clip and no IR hardware. Leave the output as above.

### Phone App Setup

A phone app is usable here only if it sends the OpenTrack UDP protocol itself, or
ships a PC-side companion that does. Check your app against that first.

For an app that does send it, what decides the wiring is how much filtering the app
does on the phone before the packet leaves it:

- **Filters on-device:** point the app straight at this PC's LAN address on port
  **4242**. The mod listens on every interface, so the phone only needs the address
  and the port.
- **Raw or lightly filtered:** send it into OpenTrack on the PC as an input instead,
  and let OpenTrack's filters and curves clean the feed up before its UDP output
  goes to `127.0.0.1:4242`. The mod's smoothing is sized to take the edge off a
  clean signal, not to rescue a noisy one.

The test is quicker than the reading: try direct, hold your head still, and if the
view drifts or shakes, route it through OpenTrack instead.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a
phone already in their pocket. It filters on-device, so it can send direct.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a tracker
running on this same PC that sends to this machine's LAN address instead of
`127.0.0.1`, because the classifier reads the transport, not the machine.

### Centering

Center in your tracker: OpenTrack's own Center bind, your phone app's center
button, or SteamVR's view reset. The mod applies whatever pose the tracker sends.

## Controls

Each action has a nav-cluster key and a Ctrl+Shift chord, so a keyboard without a nav
cluster still reaches all of them:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Switch world/local yaw | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` switches yaw between the game's reference up axis (world) and the
camera's up axis (local). The difference is visible when looking up or down with
the mouse or controller, then turning your head.

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` the moment
you change them, so the game starts in the mode you left it in. `End` changes the
current session only: the game starts with head tracking on or off as
`EnableOnStartup` says.

Each hotkey is a list of keys in the `[Hotkeys]` section of the settings file, chords
included, and every item in a list can be changed or removed:
`ToggleKey=End, Ctrl+Shift+Y`.

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or
controller points it, so with your head turned it sits off to one side with its
sights still lined up, and your rounds land where those sights point. Head
movement is scaled to the zoom, so a scope does not magnify it. Leaning eases out
while the sights are up, because it would move your eye off them.

### Flashlight

Your flashlight follows your head rather than your aim, and turns a little
further than the view does. When you turn your head your eyes end up past the
centre of the screen, so a beam matched to the view alone lands short of what
you are looking at.

| Setting | Default | What it does |
| --- | --- | --- |
| `LightFollowsHead` | `true` | Point the light where you are looking |
| `LightMultiplier` | `1.5` | How far it turns relative to your head. `1.0` matches the view, `0` leaves the beam on the aim |

Both are in the `[Light]` section of the settings file.

## Configuration

Settings live in `bin\CameraUnlock.ini` from this version on. `FarCry6HeadTracking.ini`,
the file earlier versions used, is read once to fill it and is never changed.

The mod reads its settings when the game starts. Apart from creating the file then, it
writes to it only when a hotkey changes the tracking mode or the yaw mode.

<!-- cameraunlock:config -->
The mod reads its settings from `bin\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

Earlier versions of the mod kept these settings in `FarCry6HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `FarCry6HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `FarCry6HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

A setting that the defaults below set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it. `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod reads `FarCry6HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `FarCry6HeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `FarCry6HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`
- `LightFollowsHead=true`
- `LightMultiplier=1.5`

With every setting at its default, the file reads:

```ini
; Far Cry 6 head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Light]
; true: a light you carry points where you look instead of where you aim.
LightFollowsHead=default
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=default

[Gameplay]
; true: head tracking holds the view still while another player is in the session,
; so co-op runs the game's own camera.
DisableInCoop=true
```
<!-- /cameraunlock:config -->

## Troubleshooting

Read `<Far Cry 6>\bin\FarCry6HeadTracking.log` first. It always records the port,
the settings that were loaded, whether packets are arriving, and a heartbeat line
every 600 frames carrying the pose being handed to the game.

**The mod is not loading: nothing in the log, or no log at all**

- The mod is not in the folder the game launches from. Check that
  `<Far Cry 6>\bin\tobii_gameintegration_x64.dll.backup` exists; if it does not, the
  installer did not run against this copy of the game.
- If your install has a `bin_plus` folder, put the mod there too (see Manual
  Installation).

**No tracking response: the log says the receiver is listening but nothing moves**

- The heartbeat line shows `listening=1 receiving=0`: the mod has the port but no
  packets are arriving. Check the tracker is sending to port 4242 and to this
  machine.
- The heartbeat shows a moving pose but the view is still: eye tracking is off in
  the game. Turn on eye tracking and Extended View in the game's own settings. The
  game stores those two as `EyeTrackingEnabled` and
  `EyeTrackingExtendedViewEnabled` in its `gamerprofile.xml`, so you can check
  there which one is off.

**No tracking response: the heartbeat shows `listening=0`**

- Something else on this PC has port 4242. The usual cause is another game with a
  head tracking mod still running. The log line above the heartbeat carries what the
  OS said, for example `bind failed with error 10048`.
- Close the other program. The mod checks the port twice a second and picks it up
  within about a second of it coming free, so there is no need to restart Far Cry 6.
  The log says `Bound UDP port 4242 ... tracking is live` when it does.
- If nothing is holding the port, read the error the log quotes. Error 10013 means
  Windows has that port reserved (Hyper-V and WSL each reserve blocks of the high
  range); pick a different `UdpPort` in `CameraUnlock.ini`.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` for a phone or network tracker. Leave `LocalSmoothing` at
  0 for a wired tracker on this PC.
- If your phone app sends a raw feed, route it through OpenTrack rather than sending
  direct (see [Phone App Setup](#phone-app-setup)).

**The view moves the wrong way on an axis**

- Invert that axis in your tracker, and report it: the mod's own signs are meant to
  be right for every tracker, and it has no inversion setting of its own.

**The view holds still in menus, the map and cutscenes**

- By design. Far Cry 6 only applies the head pose during gameplay.

**The view holds still in co-op**

- By design. Set `DisableInCoop=false` if you want head tracking there too.
- The log records `Multiplayer session published: N of 2 players` whenever that
  changes. Far Cry 6 publishes a joinable session in single player too, so `1 of 2`
  is normal and leaves head tracking running.

**The weapon is off to one side when I aim down sights**

- Your head is turned: the weapon stays on your aim and you are looking past it.
  Turn back to it, or move your aim to where you are looking.

**The game window moved when I launched**

- By design, and only when you play windowed: once the game has finished placing its
  window, the mod centers it on the work area of the monitor it opened on. A window
  that is already centered, and a fullscreen or borderless one, are left where they
  are. There is no setting for this.

### Known limitations

- **The mod checks the game build before starting.** It knows one Steam build and
  one Ubisoft Connect build. On any other build it leaves the game alone, and the
  log records the build's fingerprint and whether it is newer or older than the
  builds the mod knows.
- **Lean is limited by nearby geometry.** The mod uses the game's collision query
  to keep the eye back from an obstruction.
- **The mod replaces a file the game ships.** A game update, or verifying the files
  in Steam or Ubisoft Connect, restores the original. Run the installer again.
- **A real Tobii eye tracker will not work while the mod is installed**, because the
  mod takes the place of the library that talks to it.

## Updating

Download the new release and run `install.cmd` again. Your settings files are kept.

## Uninstalling

Run `uninstall.cmd`. It restores the original library from the `.backup` beside it
and removes the mod's log. `bin\CameraUnlock.ini` and `bin\FarCry6HeadTracking.ini`
are kept, so your settings are still there if you install again, and `Defaults.ini` is
not touched. There is no separate mod loader to take away, so
`uninstall.cmd /force` is accepted but has nothing extra to remove here.

If you copied the mod into `bin_plus` by hand, undo that by hand as well: rename
`tobii_gameintegration_x64.dll.backup` back over the mod. The uninstaller only
touches the folder the installer used.

## Building from Source

Needs CMake and Visual Studio 2022 or newer. The build never touches the game, there
is nothing to reference from it.

```powershell
git clone --recursive https://github.com/itsloopyo/far-cry-6-headtracking.git
cd far-cry-6-headtracking
pixi run build-release
pixi run test
pixi run package
```

`pixi run install` deploys the built DLL into a local install, and `pixi run package`
produces the installer ZIP.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- **Far Cry 6** is developed by Ubisoft Toronto and published by Ubisoft. This mod
  is unofficial and is not affiliated with or endorsed by them.
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC) - the tracking protocol
  this mod speaks.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause) - used for camera
  hooks and the game's multiplayer session calls.

## Disclaimer

This is an unofficial mod. It is not affiliated with, endorsed by, or supported by
Ubisoft, and it is not affiliated with or endorsed by Tobii. Use it at your own
risk. It replaces a file inside your game folder and keeps the original beside it;
a game update or a file verification in Steam or Ubisoft Connect puts the original
back.
