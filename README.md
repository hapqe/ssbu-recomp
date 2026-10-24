# OpenSmash-Ultimate for Android

**Super Smash Bros. Ultimate on Android, recompiled to native arm64 code and drawn with Vulkan.**
An experimental port of the [OpenSmash-Ultimate](https://github.com/Denkishi/ssbu-recomp) PC project, with an on-device
launcher that sets the game up from your own Switch files.

![Platform](https://img.shields.io/badge/platform-Android%20arm64-lightgrey)
![Game versions](https://img.shields.io/badge/game-13.0.4%20%7C%2013.0.5-green)
![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)

> **Mostly made with AI.** Most of this port (the launcher, the runtime changes, the build scripts and this README) was
> written with AI coding assistants (Claude), directed and tested by one person. Expect rough edges, and read the code
> before you trust it with anything you care about.

> **This repository contains no game code, no game assets, no keys and no firmware.** You need your own legally obtained
> copy of the game (13.0.4 or 13.0.5) and your own keys. Everything derived from them is made on your device or PC,
> from your own files, and never leaves it.

## What it is

The game's AArch64 machine code is translated to C ahead of time and compiled for arm64, so there is no CPU interpreter
while you play. The console's system libraries are re-implemented by a small runtime (`android/runtime`, `android/compat`)
on top of Android, and the picture is drawn with **Vulkan**. The APK holds no game code and no game data: the launcher
unpacks the game from your Switch files on the device, and imports the game's recompiled code from a `.zip` made once
on a PC.

The PC version (Windows and Linux), its installer and the modding work are in the original project,
[Denkishi/ssbu-recomp](https://github.com/Denkishi/ssbu-recomp). This repository is based on it.

## Requirements

- An **arm64 Android 11+** phone or tablet with **8 GB of RAM** or more, and about **25 GB free**.
- A **Vulkan 1.3** GPU. On Adreno GPUs install a **Mesa Turnip** driver package (an AdrenoTools `.zip`) in the launcher.
  The stock Adreno driver lacks features the renderer needs.
- Your own Switch files: `prod.keys` (and `title.keys`), the base game `.nsp`, the update `.nsp` (13.0.4 or 13.0.5), and
  optionally your add-on content (DLC) `.nsp` files and your save data.
- **Once, a PC** (Mac or Linux) with the Android NDK, to make the game libraries (step 1 below). This cannot be done on
  the phone.

Tested on a **Poco F5** (Snapdragon 7+ Gen 2, Adreno 725, Mesa Turnip): title, menus and battles, **59-60 fps in a fight**
at the default render resolution. The port was first brought up on a Xiaomi Pad 6 (Adreno 650). Other devices are untested.

## Install

### 1. On a PC, once: the game libraries

```
python3 android/tools/build_game_android.py --dump <your dump folder> --package
```

This writes `android/build/game/OpenSmash-libraries-<version>.zip` (about 1 GB). Copy it to the phone by any means (USB,
cloud, network share). Build the APK with `android/build_apk.sh` (Android SDK and NDK, no Gradle) and install it.
Details: [docs/ANDROID.md](docs/ANDROID.md).

### 2. On the device: the setup walkthrough

Open **OpenSmash**, allow access to files when asked, then go to **Game files > Set up from Switch files…**. It asks for
your files one step at a time:

1. **The main game.** Choose the base game `.nsp` (title 0100…6E000). *Skip* is allowed.
2. **The update or DLC.** Choose the update `.nsp` (13.0.4 or 13.0.5), or a DLC `.nsp`. Choose as many DLC packages as you
   have, one at a time, then press *Next*. Every choice is optional: *Next* alone skips the step.
3. **The keys.** Choose `prod.keys`. A `title.keys` next to it is used too. *Skip* is allowed.
4. **Review.** The launcher lists what it took, where the game goes and the free space. Press **Set up** to unpack the
   game data (RomFS, about 15 GB; it can be stopped and started again, finished files stay).

A first setup needs the main game, its update and the keys. If one of them was skipped, the review says what is missing
and offers to start again. DLC alone can be added at any time, from *Add-on content* (see below).

The walkthrough takes `.nsp` files that sit on the phone's shared storage (for example in *Download* or in the game folder).
Compressed packages (`.nsz` / `.xcz`) must be decompressed first.

### 3. Then, in the launcher

- **Game files > Import libraries (.zip)…**: the file from step 1. It is copied into the app's private storage.
- **GPU driver > Install a driver (.zip)…**: a Mesa Turnip package, on Adreno GPUs.
- **Save data > Import a folder… / Import a .zip…**, optional: a folder or `.zip` holding `save_data` (from JKSV, Checkpoint,
  yuzu, Eden or Ryujinx).
- **Play.** The first start prepares every shader of the game behind a *PREPARING SHADERS* screen: about 15-25 minutes on a
  phone, once for each resolution and picture shape.

### Add-on content (DLC), later

**Add-on content > Import files / Import a folder** reads the DLC ids from the names of your packages (or from inside a
`.rar` / `.zip` of them) and unlocks the fighters, stages, costumes and spirits. Nothing is unpacked for this. The
game announces what was unlocked at the next start.

## Launcher settings

| Setting | What it does |
|---|---|
| Render resolution | `SSBU_VK_SCALE`: 540p to 2160p; lower is faster and uses less memory |
| Picture shape | 16:9 with bars; *Fill* (stretched to the screen); *Wide* (the game renders in the screen's shape, e.g. 20:9) |
| On-screen controls | Auto (off while a controller is connected), on, off |
| Show the frame rate | frames per second and frame time in the top left corner |
| Extra settings | any `SSBU_*` variable, one per line |

Controllers: Android gamepads through SDL3 (an Xbox 360-type pad was tested). A connected controller is player 1.
The game asks the system for 60 Hz, so a 120 Hz phone does not make a 60 fps game look uneven when you touch the screen.

## Limits

- The game libraries need a PC once (step 1).
- Function hooks of mods (`fighter_sdk`) do not work on Android: the system does not allow patching code in memory.
- Under memory pressure Android may close background apps while the game runs.
- If the screen locks while the game runs, the game waits until it is unlocked.
- Only the devices named above were tried.

## Build from source

[docs/ANDROID.md](docs/ANDROID.md) has the full list. In short: `android/build_apk.sh` needs the Android SDK (build tools
and platform 36), NDK 29, JDK 17 and the source packages of SDL3, glslang, libopus, Vulkan-Headers and libadrenotools,
fetched by `android/tools/build_deps.sh` beside the repository.

<!-- OWNER: a screenshot of the launcher and of a fight on a phone would fit here (media/android-*.png, your own captures). -->

## Credits

- **Denkishi** and the contributors of [OpenSmash-Ultimate](https://github.com/Denkishi/ssbu-recomp): the PC port this
  project is based on, its recompiler, runtime and tools. The license and most of the code are theirs.
- **Mesa Turnip** (the open-source Vulkan driver for Adreno GPUs) and **libadrenotools** (loading such drivers).
- **SDL3**, **glslang**, **libopus** and **Vulkan-Headers**, built for Android by `android/tools/build_deps.sh`.
- **yuzu and suyu**: the open-source system fonts in `runtime/fonts_open` ([README](runtime/fonts_open/README.md)).

## Legal

OpenSmash-Ultimate is not affiliated with, endorsed or sponsored by Nintendo, Bandai Namco or Sora Ltd. *Super Smash
Bros.* and *Nintendo Switch* are trademarks of their owners and are named only to say what the software works with.
The repository contains no copyrighted game data, no console keys and no firmware, and does not help anyone obtain
them. **You must own the game and dump it yourself.** No Nintendo logos or artwork are used.

Licensed under the GNU General Public License, version 3 or later (see [LICENSE](LICENSE)).
