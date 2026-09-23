# Third-Party Notices

Far Cry 6 Head Tracking is MIT licensed (see [LICENSE](LICENSE)). This file records
everything else that travels with it or that its interfaces are shaped by.

No game code, no extracted game assets and no game data files are in this
repository, with the single exception of the demo footage described at the bottom.
The mod references nothing from a game install at build time; it compiles from this
repository alone.

## cameraunlock-core

- **Version:** commit `2eded2c129ae256564dab23652bc6732eb47d8a9`
- **License:** `MIT`
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Provides the OpenTrack receiver, the pose interpolation and smoothing
  pipeline, the INI reader, the hotkey poller and the file log. Its install and
  uninstall script bodies, its game-path detection scripts and its `games.json`
  are what `install.cmd` and `uninstall.cmd` run.
- **Bundled:** yes, in two forms. The library is compiled into the shipped DLL,
  and the scripts and `games.json` named above are copied verbatim into the
  installer ZIP's `shared/` folder.

Copyright (c) itsloopyo.

---

## MinHook

- **Version:** 1.3.4, with one local change: `MH_Initialize` takes the process heap
  instead of creating a private one, and `MH_Uninitialize` no longer destroys it.
  `cameraunlock-core/vendor/minhook/LOCAL-CHANGES.md` records that against the
  upstream tag, and every other file is byte-identical to it. BSD-2-Clause does not
  require a modification to be marked; it is named here so the copy in the shipped
  DLL can be diffed against upstream.
- **License:** `BSD-2-Clause`
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Hooks the camera, reticle and flashlight interfaces, and
  watches two Ubisoft Connect entry points so head tracking can hold still in a
  co-op session.
- **Bundled:** yes. Vendored inside cameraunlock-core and compiled into the shipped
  DLL.

MinHook carries Hacker Disassembler Engine (Vyacheslav Patkov, `BSD-2-Clause`).
Both are compiled into the shipped DLL, so both licences travel with it. The
BSD-2-Clause terms require the copyright notice and the disclaimer to be
reproduced with any binary distribution, so the text is here in full rather than
by reference:

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## OpenTrack

- **Version:** no OpenTrack release is used; only the wire format its "UDP over
  network" output sends.
- **License:** `ISC`
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** The mod implements that UDP wire format: six little-endian doubles
  carrying x, y, z in centimetres and yaw, pitch, roll in degrees.
- **Bundled:** no. No OpenTrack code is used, linked or shipped.

---

## Tobii Game Integration API

- **Version:** SDK header 9.0.4.26; the game asks for API version 7.3.
- **License:** proprietary to Tobii AB, so there is no SPDX identifier. No Tobii
  source, header or binary is copied into this repository or into a release.
- **Upstream:** Tobii AB, Tobii Game Integration SDK. No public repository.
- **Usage:** Far Cry 6 loads `tobii_gameintegration_x64.dll` and resolves one
  symbol from it, `GetApi`, reaching everything else through the returned object's
  virtual tables. This mod takes the place of that DLL, so it has to present the
  same binary interface.
- **Bundled:** no. `src/tobii_abi.h` is our own declaration of that interface,
  written to match it.

Two sources establish what it has to match, and both are named here because that
is what makes the boundary auditable:

- The **Tobii Game Integration SDK header** (`tobii_gameintegration.h`, version
  9.0.4.26, distributed by Tobii AB) is where the struct layouts, the field
  meanings and the head stream units come from: rotations in degrees, positions
  in millimetres, and which direction each axis calls positive.
- The **DLL Far Cry 6 ships** is where the rest comes from. The game asks for API
  version 7.3, whose interfaces differ from the published 9.x ones, so the slot
  order of `ITrackerController`, `IExtendedView`, `IFilters`, `IStreamsProvider`
  and `IStatistics` was read out of that DLL's own vtables and its exported
  constructors, along with the sizes its settings constructors write.

Far Cry 6 consumes extended-view rotation in radians, confirmed with fixed-angle
inputs in the game. This differs from the degrees declared by the 9.x header;
the mod converts only the extended-view output.

Tobii and Tobii Game Integration are trademarks of Tobii AB; this mod is not
affiliated with or endorsed by Tobii.

---

## Ubisoft Connect

- **Version:** whichever `upc_r2_loader64.dll` the installed Ubisoft Connect
  provides; the mod pins no version.
- **License:** proprietary to Ubisoft, so there is no SPDX identifier. No Ubisoft
  code or header is included.
- **Upstream:** Ubisoft, shipped with Ubisoft Connect and with the game.
- **Usage:** The mod calls no Ubisoft Connect API of its own. It observes two
  exports the game already calls, `UPC_MultiplayerSessionSet` and
  `UPC_MultiplayerSessionClear`, forwarding each call unchanged, so that head
  tracking can hold still while a multiplayer session is published.
- **Bundled:** no.

---

## Far Cry 6 camera, reticle and flashlight interfaces

- **Rights holder:** Ubisoft.
- **Source:** the locally installed `FC_m64d3d12.dll` of the Ubisoft Connect build
  (PE timestamp `6824d119`, image size `1fb64000`, checksum `1ee4dd4b`) and of the
  Steam build (PE timestamp `644bbd74`, image size `1fe87000`, checksum `1f12fe67`).
- **Usage:** its camera parameter layout, rotation conventions, HUD quaternion
  location, gaze-aim flags, collision query interface, reticle screen position,
  flashlight component and entity transform setter establish the boundary used by
  `src/camera_adapter.cpp`, `src/camera_pose.h`, `src/reticle.cpp` and
  `src/headlight.cpp`.
- **Bundled:** no game code, headers or binaries. The adapter and its declarations
  are this project's own MIT-licensed implementation.

---

## Far Cry 6 footage

- File: `assets/readme-clip.gif`
- Rights holder: Ubisoft Toronto (developer) and Ubisoft (publisher).
- Purpose: showing what the mod does at the top of the README, which is how mod
  pages present themselves.
- It belongs in this repository, embedded by the README, and ships in no release
  ZIP. It is added before the first public release; until then the README's embed
  renders as a broken image.
- No licence over it is claimed or granted. It will be removed on request from the
  rights holder.

Far Cry is a trademark of Ubisoft. This mod is unofficial and is not affiliated
with or endorsed by Ubisoft. Naming the game, its developer and its publisher here
and in the README is nominative use.
