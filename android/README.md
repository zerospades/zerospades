# Android work in progress

The Android project is a **launcher and touch HUD preview**, not a playable port
of the game. Start currently explains that the engine is unavailable. The
launcher preferences are stored locally using existing setting names, but do
not yet affect the engine.

The separate **Preview touch HUD** button shows an illustrative landscape
layout inspired by the supplied reference. The background and HUD data are
examples, not a game or an actual map. The preview timer counts time since the
preview opened. The overlay accepts multiple simultaneous touches; the X closes
the preview. The top-right tool panel cycles example tool counts, and chat opens
a large, 80%-opaque panel. Messages entered there are labelled as unsent and
remain local to the preview. Other actions are not connected to game mechanics.

## Build the preview

Install JDK 17, Android SDK platform 35 and Gradle 8.9, then run:

```sh
cd android
gradle testDebugUnitTest assembleDebug
```

The debug-signed APK is at `app/build/outputs/apk/debug/app-debug.apk`.
The optional `workflow-templates/android.yml` can be added under
`.github/workflows/` by a repository maintainer to build and test the preview;
it is not active as a template. If enabled, a tag named `android-preview-*`
publishes a clearly labelled GitHub **prerelease**. Debug signing is not
suitable for production or reliable upgrades between CI builds.
The native launcher defaults to lower-cost water; its water reflections option
adds a mirrored-scene pass. Both need device performance testing.

If the experimental game exits during startup, reopen the app and select
**View crash report**. You can inspect, copy, or share the report manually;
no data is sent automatically. It may contain device details, file paths,
server addresses, and Android crash traces, so review it before sharing. The
engine log and crash trace might be unavailable if the process dies before
logging starts or the Android version does not expose a trace.

## HUD integration contract

`hud/TouchHudView` draws a transparent overlay, taking `HudState` snapshots on
the UI thread. The experimental native bridge supplies scores and the shared
mode maximum, health, selected tool and weapon, ammunition, frame rate and up
to five kill messages. The engine renders its real circular minimap underneath
the overlay in the reference position. The tool icons come from
`Resources/Gfx/Hotbar`. The activity tracks `joinedAtMillis` using
`SystemClock.elapsedRealtime()`, but its behavior across disconnects and server
changes still requires validation.
The displayed clock counts up in minutes and seconds. Both score bars use the
same game-mode maximum as their denominator (not the leading team's current
score); an unknown or non-positive maximum produces empty bars. A bitmap passed
to the view must remain valid until it is replaced.

`TouchController.Sink` emits normalized movement, relative look deltas and
pressed/released button actions. `NativeTouchSink` and the Android-only native
bridge now provide a **partial integration path**: the SDL game loop drains
queued input on its own thread, and the router uses the player's current ZS
key bindings for movement, fire, aim, reload, sprint, crouch, global chat and
pause. Movement is digital (four directions), and aiming follows ZS's existing
hold/toggle setting. The tool panel sends an Android-only next-tool action that
cycles selectable tools, independent of mouse-wheel settings. The router emits
releases on cancellation, queue overflow, menu entry or engine shutdown; release
handling across menu transitions still needs device testing. The grenade button
uses a frame-spanning select/fire/release/restore sequence, which also requires
integration testing. Double-tap the movement joystick to jump (drag on the
second tap to move); this adds no extra HUD button. The jump pulse is held
through one game update. Tap the tool panel to cycle selectable tools; hold it
to open the game's loadout/team menu, where weapon changes use the existing
server rules. With blocks selected, tap or drag across the game's palette to
select a block color; hold it to grab the color of the aimed-at block. These
controls must be tested on a device.

This bridge is **not active in the default preview APK**. The opt-in,
unverified native build attaches the overlay and polls actual gameplay state.
It forwards chat and killfeed messages, renders the native minimap beneath the
overlay, and suppresses corresponding duplicate desktop HUD elements during
active play. Gameplay input is suppressed when the game needs an absolute
cursor. SDL handles touch-to-mouse conversion in menus, but menu usability,
keyboard focus, input sensitivity, audio focus, safe areas, lifecycle, minimap
markers and rendering, and reconnect behavior all require Android testing. The
launcher exports Android system CA certificates for native curl; the result has
not been verified against Android's complete network trust policy.

To run the platform-independent native router tests on a host with g++:

```sh
g++ -std=c++17 -Wall -Wextra -Werror android/native/TouchInputRouter.cpp \
  android/native/tests/TouchInputRouterTest.cpp -o /tmp/touch-input-tests
/tmp/touch-input-tests
```

## Native AngelScript ABI checks

The Android ABI selection now chooses AngelScript's ARM64 call bridge for
`arm64-v8a`, its ARM bridge for `armeabi-v7a`, and the existing x86 sources for
`x86` and `x86_64`. Both AngelScript static libraries are position-independent
on Android for later linking into a shared game library. An unsupported ABI is
rejected rather than silently using the wrong calling convention.

The isolated smoke test builds the bundled engine and addons, registers native
scalar, value-type and C++ method calls, compiles a script, and checks its
result. On a host with CMake and a C++ compiler:

```sh
cmake -P android/native/tests/angelscript/TestAbiSelection.cmake
cmake -S android/native/tests/angelscript -B /tmp/angelscript-host -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/angelscript-host --parallel
ctest --test-dir /tmp/angelscript-host --output-on-failure
```

The optional preview workflow template includes cross-compilation and linking
of this smoke executable and a shared-library link check for arm64-v8a and
x86_64 using NDK r28. This is an isolated ABI check, **not** a build of the
game. Cross-compilation does not execute the script on Android; the executable
still needs to be run on matching devices/emulators to validate the actual
native calling convention. No such runtime test has been performed here.

## Resource preparation for the native port

`tools/package_resources.py` builds the five ZeroSpades paks from the existing
`Resources/` tree, with the same file membership as `Resources/mkpak.sh`. It
can also copy the essential external paks and their license texts from a local
OpenSpades development package. For example, in a fresh output directory:

```sh
python3 android/tools/package_resources.py --output /tmp/zs-paks \
  --devpak /path/to/OpenSpadesDevPackage-r33.zip
python3 -m unittest discover -s android/tests -v
```

Without `--devpak` the generated paks are incomplete; the game requires the
external assets listed in `Resources/PakLocation.txt`. Do not redistribute the
nonfree pak separately from a compiled release. No external assets are committed
or included in the preview APK.

The experimental native build packages these paks and notices under the APK
asset path `game-paks/`, verifies each external file against pinned upstream r33
content, and invokes `GameAssetsInstaller.install()` before starting SDL. It
copies paks to private `files/game-paks` without overwriting the separate
`files/user-resources` config/mod directory. The preview does not invoke it.
Packaging and installation have not been tested on Android. The nonfree pak's
license has conditions that must be reviewed before distributing a game build.

## Experimental native integration build

This path is incomplete and **not playtest-ready**. It has not been built or
run on Android. For an arm64 integration build, install JDK 17, Gradle 8.9,
Android SDK platform 35, NDK 28.2.13676358 and Android SDK CMake 3.31.5. Initialize the `openal-soft`
submodule, then build the ports in `vcpkg/dependencies.txt` from vcpkg commit
`4334d8b4c8916018600212ab4dd4bbdc343065d1` with the triplet in
`vcpkg/triplets/arm64-android-api24.cmake`. Set `ZEROSPADES_ANDROID_DEPS` to
the installed triplet prefix and `ZEROSPADES_DEVPAK` to the upstream r33
`OpenSpadesDevPackage-r33.zip`, then run:

```sh
cd android
gradle --no-daemon -PnativeGame=true assembleDebug
```

The `workflow-templates/android-native-experimental.yml` template attempts
this build if a maintainer adds it under `.github/workflows/`. It is currently
inactive and untested. Its debug artifact, if produced, is not a release and
is not yet suitable for gameplay testing. CI builds cannot establish rendering,
networking, touch, asset licensing or device runtime correctness.

## KyroSpades reference and remaining work

KyroSpades (checked at `b380ae85474d37e5a4eee0071894e5398ddb6253`) is a
separate BetterSpades-derived C engine, not the OpenSpades-derived ZeroSpades
C++ engine. Its SDL activity, NDK dependency builds, GLES renderer, and Android
asset packaging are useful references, but cannot be substituted for ZS without
changing the game. No KyroSpades engine or assets have been copied here.

To deliver a playable ZeroSpades APK, the following is still required:

* Successfully cross-compile and link the C++ game and its native dependencies;
  verify the SDL activity and native library work on Android.
* Finish and validate GLES operations and shaders on actual drivers, as well as
  lifecycle and audio support.
* Validate resource installation and ensure packaging and distribution comply
  with all asset and dependency license terms.
* Validate the launcher settings, HUD state and touch bridge in the running
  engine, including quick grenade, chat send/receive, menus and server browser;
  fix the problems found.
* Test networking, permissions, lifecycle, display cutouts and input on actual
  Android devices, and use a persistent production signing key for release.

The preview workflow template deliberately builds and publishes only a
preview, not a playable game release. Android-specific code lives in
`android/app/src/main` and `android/native`; existing game sources remain in
`Sources/`. The existing desktop workflow is retained unchanged and has not
been validated against this Android-focused branch.
