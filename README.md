# Far Cry 6 Head Tracking

![Far Cry 6 running with this mod](https://raw.githubusercontent.com/itsloopyo/far-cry-6-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Far Cry 6 that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - your head moves the view, your mouse or controller keeps the aim
- **Six-axis head tracking** - yaw, pitch, roll and positional lean
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android
- **Aim dot** - a small dot marks where you are aiming whenever the game's own crosshair is not on screen

## Requirements

- **[Far Cry 6](https://store.ubisoft.com/us/far-cry-6/)** on PC, from Ubisoft
  Connect. The game folder must contain `bin\FarCry6.exe` and
  `bin\tobii_gameintegration_x64.dll`.
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

The mod listens on 4242 unless `Port` in the INI says otherwise. If something else on
the PC already has that port, change it in both places.

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

Two equivalent binding sets, use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

`FarCry6HeadTracking.ini` is written next to the mod DLL in `<Far Cry 6>\bin` the
first time the game runs. It is read once, when the game starts.

```ini
; Far Cry 6 - Head Tracking configuration
; Lives next to tobii_gameintegration_x64.dll in the game bin folder.

[General]
EnableOnStartup=1
; UDP port the tracker sends OpenTrack packets to. Point your tracker
; output at this port.
Port=4242

[Sensitivity]
Yaw=1
Pitch=1
Roll=1
; Flip an axis only if your tracker reports it backwards. The sign
; conversion the game needs is already applied; these three ship off.
InvertYaw=0
InvertPitch=0
InvertRoll=0

[Smoothing]
; Chosen per connection from the source address; covers rotation and position.
; Only a loopback sender counts as local. A tracker on this PC that sends to
; this machine LAN address instead of 127.0.0.1 is classified as remote.
LocalSmoothing=0
RemoteSmoothing=0.15

[Position]
; Positional tracking. Limits are metres of head travel.
; The camera's collision query shortens a lean near an obstruction.
Enabled=1
SensitivityX=1
SensitivityY=1
SensitivityZ=1
LimitX=0.3
LimitY=0.2
LimitZ=0.4
LimitZBack=0.1
; As above: only for a tracker that reports an axis backwards. Leaving these
; off is what keeps LimitZ on leaning in and LimitZBack on pulling away.
InvertX=0
InvertY=0
InvertZ=0

[Gameplay]
; Hold the view still while another player is in the session, so co-op runs
; the stock camera. Set to 0 to keep head tracking in co-op.
DisableInCoop=1

[Hotkeys]
; Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode).
Toggle=0x23
CycleMode=0x21
; Ctrl+Shift+Y (toggle) and Ctrl+Shift+G (cycle tracking mode) fire the same
; actions on a keyboard with no navigation cluster. They are always registered.
```

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
  range); pick a different `Port` in the INI.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` for a phone or network tracker. Leave `LocalSmoothing` at
  0 for a wired tracker on this PC.
- If your phone app sends a raw feed, route it through OpenTrack rather than sending
  direct (see [Phone App Setup](#phone-app-setup)).

**The view moves the wrong way on an axis**

- Set the matching `Invert...` key in the INI. Report it as well; the shipped signs
  are meant to be right for every tracker.

**The view holds still in menus, the map and cutscenes**

- By design. Far Cry 6 only applies the head pose during gameplay.

**The view holds still in co-op**

- By design. Set `DisableInCoop=false` if you want head tracking there too.
- The log records `Multiplayer session published: N of 2 players` whenever that
  changes. Far Cry 6 publishes a joinable session in single player too, so `1 of 2`
  is normal and leaves head tracking running.

**The game window moved when I launched**

- By design, and only when you play windowed: once the game has finished placing its
  window, the mod centers it on the work area of the monitor it opened on. A window
  that is already centered, and a fullscreen or borderless one, are left where they
  are. There is no setting for this.

### Known limitations

- **The camera adapter checks the game build before starting.** An unrecognised
  build leaves tracking disabled and records `unsupported camera module` in the
  log. The adapter has been tested with the installed Ubisoft Connect build.
- **Lean is limited by nearby geometry.** The mod uses the game's collision query
  to keep the eye back from an obstruction.
- **The mod replaces a file the game ships.** A game update, or verifying the files
  in Ubisoft Connect, restores the original. Run the installer again.
- **A real Tobii eye tracker will not work while the mod is installed**, because the
  mod takes the place of the library that talks to it.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. It restores the original library from the `.backup` beside it
and removes the mod's INI and log. There is no separate mod loader to take away, so
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
a game update or a file verification in Ubisoft Connect puts the original back.
