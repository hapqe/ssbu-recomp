# OpenSmash on Android (experimental)

The same recompiled game, running natively on a 64-bit ARM phone or tablet: the game's code is compiled for arm64,
the runtime is the Windows one on an Android compatibility layer (`android/compat`), and the picture is drawn with
Vulkan. The APK contains no game code and no game data.

**Status:** works on the development devices; not widely tested. Measured on a Poco F5 (Snapdragon 7+ Gen 2,
Adreno 725, 8 GB RAM, Mesa Turnip driver): title, menus and battles, 59-60 fps in a fight on Big Battlefield at the
default render resolution. The port was first brought up on a Xiaomi Pad 6 (Adreno 650).

## What you need

- An arm64 Android 11+ device with **8 GB of RAM** or more (the game uses 2-4 GB) and about **25 GB free**.
- A GPU with Vulkan 1.3. Adreno GPUs: install a **Mesa Turnip** driver package (AdrenoTools .zip) in the launcher; the
  stock Adreno drivers lack features the renderer needs. The launcher's *Advanced > This device* section says whether the
  selected driver has everything.
- Your own Switch files: `prod.keys` (and `title.keys`), the base game, its update **13.0.4 or 13.0.5**, and optionally
  your add-on content (DLC) packages and save data.
- **Once, a PC** (Mac or Linux) to build the *game libraries*: the game's code recompiled for arm64. This is the one part
  that cannot be made on the phone (it is a lift of the game's code to C and a ~400 MB compile). It is made from your own
  dump and must not be shared.

## Setting up

### 1. On the PC, once: the game libraries

```
python3 android/tools/build_game_android.py --dump <your dump folder> --package
```

(`--dump` as in [BUILDING.md](../BUILDING.md); needs the Android NDK.) This writes
`android/build/game/OpenSmash-libraries-<version>.zip` (about 1 GB). Copy it to the device by any means (USB, cloud,
network share). Build the APK with `android/build_apk.sh` (Android SDK + NDK, no Gradle) and install it.

### 2. On the device: everything else

Open **OpenSmash** and:

1. **Allow access to files** when asked (the game folder, default `/sdcard/OpenSmash`, is on the shared storage).
2. **Game files > Set up from Switch files**: choose the folder holding your keys, base game, update and DLC (sub-folders
   are searched). The launcher identifies the packages by their names or, failing that, by their headers, then unpacks
   the update's code and the game data (RomFS, ~15 GB; it can be stopped and started again, finished files stay). Your
   DLC is unlocked in the same step.
3. **Game files > Import libraries (.zip)**: the file from step 1. They are copied into the app's private storage.
4. **GPU driver > Install a driver (.zip)**: a Turnip package, on Adreno.
5. Optional: **Save data > Import** a folder or `.zip` holding `save_data` (JKSV, Checkpoint, yuzu / Eden / Ryujinx).
6. **Play.**

The **first start** with a given resolution / picture shape prepares every shader of the game behind a
*PREPARING SHADERS NN %* screen: about 15-25 minutes on a phone, once. On Android every scaled resolution and every
picture shape share one prepared set; native 1080p without *Wide* is a second set. The launcher says when a choice still
needs this.

## Add-on content (DLC)

Smash's DLC fighters, stages, costumes, spirits and music are data of the game **update**; a DLC package only unlocks its
part. So the launcher only needs the add-on content ids, which it reads from the package names
(`… [01006A800016F041].nsp`) or the names inside a `.rar` / `.zip` of them, or from inside a `.nsp`: **Add-on content >
Import files / Import a folder**. Nothing is unpacked or decrypted for this. The list is `config/dlc.txt` in the game
folder (the runtime's `nn::aoc`, `runtime/nnmisc.c`). At the next start the game announces what was unlocked
(*Piranha Plant Joins the Battle!*, new stages, Mii costumes, spirit-board items), as on a console.

## Settings in the launcher

| Setting | What it does |
|---|---|
| Render resolution | `SSBU_VK_SCALE`: 540p to 2160p; lower = faster and less memory |
| Picture shape | 16:9 with bars; *Fill* (the 16:9 picture stretched to the screen); *Wide* (the game renders in the screen's shape, e.g. 20:9, and shows more at the sides) |
| On-screen controls | Auto (off while a controller is connected), on, off |
| Show the frame rate | frames per second and frame time in the top left corner |
| Extra settings | any `SSBU_*` variable, one per line |

The game asks the system for 60 Hz (`SSBU_FRAME_RATE`, default 60; 0 leaves the choice to the system). Phones with a
120 Hz screen otherwise switch to 120 Hz whenever the screen is touched, and a 60 fps game then looks uneven: frames stay
on screen for 8, 17 or 25 ms instead of always 17 ms, although the frame counter shows 60. The game log's
`nvn_vk_timing:` lines report how long each frame was really on screen (from the display, `VK_GOOGLE_display_timing`).

Controllers: Android gamepads through SDL3 (an Xbox 360-type pad was tested); a connected controller is player 1.

## Known limits

- The game libraries need a PC once (see above).
- Function hooks of mods (`fighter_sdk`) do not work on Android (the system forbids patching code in memory).
- Under memory pressure Android may close background apps while the game runs.
- If the screen locks while the game runs, the game waits for its window until it is unlocked.
