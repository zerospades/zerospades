# ZeroSpades ![Build status](https://github.com/siecvi/zerospades/actions/workflows/ci.yml/badge.svg) [![All releases downloads](https://img.shields.io/github/downloads/siecvi/zerospades/total.svg)](https://github.com/siecvi/zerospades/releases) [![Latest release](https://img.shields.io/github/release/siecvi/zerospades.svg)](https://github.com/siecvi/zerospades/releases)


![unknown](https://user-images.githubusercontent.com/25997662/166125363-3cdf237d-2154-4371-a44b-baea8a7abe5f.png)

[Download](https://github.com/siecvi/zerospades/releases/latest) — Community: [BuildAndShoot](https://buildandshoot.com) - [aloha.pk](https://aloha.pk)

## What is OpenSpades?
[OpenSpades](https://github.com/yvt/openspades) is a compatible client of [Ace Of Spades](https://en.wikipedia.org/wiki/Ace_of_Spades_(video_game)) 0.75.

* Can connect to a vanilla/[pyspades](https://code.google.com/archive/p/pyspades)/[PySnip](https://github.com/NateShoffner/PySnip)/[piqueserver](https://github.com/piqueserver/piqueserver)/[SpadeX](https://github.com/SpadesX/SpadesX) server.
* Uses OpenGL/AL for better experience.
* Open source, and cross platform.

## Ok but what the heck is ZeroSpades?
ZeroSpades is a fork of OpenSpades with extra features and many improvements/fixes.

Some of the most important changes are:

* Demo recording/playback (credits to Fran6nd).
* Pie menu (credits to Fran6nd).
* In-game Hit detection debugger (credits to BR).
* Improved firstperson weapon animations (credits to PTrooper).
* More thirdperson animations.
* Weapon charms/keychains support.
* Configurable left-handed viewmodel.
* Viewmodel position presets (Default, Balanced, Minimal).
* Firstperson playermodels (torso & legs).
* Classic firstperson viewmodel.
* Dynamic playermodels based on weapon class.
* Dead player corpse and falling blocks physics.
* Configurable ragdoll removal.
* Physically-based lighting for models.
* Smooth interpolation for player orientations.
* Killstreak sounds (needs [Killsounds.pak](https://github.com/zerospades/zerospades-paks/raw/refs/heads/main/(SFX)%20Killsounds.pak))
* Extended block color palette.
* Configurable alive player counter.
* HUD hotbar displaying selectable tools.
* HUD health bar with damage animation.
* Configurable crosshair and scope.
* Configurable HUD color and safe zone.
* Player names while on spectator mode.
* Teammate names added to the minimap.
* Damage dealt to players shown as floating damage numbers.
* Client-side hit analyzer (showing hit player name, distance, and body part).
* Player statistics such as: hit accuracy, kill/death ratio, kill streak, number of melee/grenade kills, and blocks placed.
* Killfeed icons with kill indicators (non-scoped, in-air) and domination tracking.
* Multiple sensitivity scaling presets (Quake, Source, Valorant, etc.)
* Same fog tint/color as the classic client (openspades applies a [bluish tint](https://github.com/yvt/openspades/blob/v0.1.3/Resources/Shaders/Fog.vs#L27) to the fog).
* Demo recording compatible with [aos_replay](https://github.com/BR-/aos_replay) format.

-- And a lot of other things that I forgot to mention here.

## Demo Recording & Playback

ZeroSpades supports recording and playing back gameplay demos using a format compatible with [aos_replay](https://github.com/BR-/aos_replay).

### Recording

Record gameplay from the command line:

```bash
# Record demos to mydemo_1.dem, mydemo_2.dem, etc. (one per game/map)
openspades aos://server:port --record mydemo
```

Demos are saved with a numeric suffix for each game joined during the session.

### Playback

Play back demos directly in ZeroSpades:

```bash
# Play a demo file
openspades /path/to/demo.dem

# Or with the explicit flag
openspades --replay /path/to/demo.dem
```

Demos can also be played back using the external [aos_replay](https://github.com/BR-/aos_replay) tool.

## How to Build/Install?
See the [OpenSpades Building&Installing Guide](https://github.com/yvt/openspades#how-to-buildinstall)

Just a quick note when building on [Windows](https://github.com/yvt/openspades#on-windows-with-visual-studio) (with vcpkg):

On CMake, before you configure the project, you will need to add a new string entry:

* With the Name field: `VCPKG_TARGET_TRIPLET`
* And the Value field: `x86-windows-static`

Then you're ready to configure & generate.

## Tested Platforms
* x86-64, EndeavourOS, Ryzen 7 7700X, RX9070XT, 32G RAM
* ARM64, macOS 26.4.1, Apple Silicon M4, Integrated GPU, 24 GB RAM
* riscv64, Ubuntu 24.04 LTS, SIFIVE P550, RX6700XT, 8G RAM
* riscv64, Ubuntu 25.10 LTS, QEMU RVA23, No GPU, 12G RAM

## Troubleshooting
For troubleshooting and common problems see [TROUBLESHOOTING](TROUBLESHOOTING.md).

## Licensing
Please see the file named LICENSE.

Note that other assets including sounds and models are not open source.
