# Install Moji Nook

Moji Nook is an early alpha (0.1.2). The [releases page](https://github.com/AsterithUlala/moji-nook/releases)
has an Arch package, an AppImage for other Linux distributions, and an
experimental Windows zip. You can also build from source. Build commands below start from the repository
directory after cloning.

## Install the Arch package

The package targets Arch Linux and derivatives such as CachyOS and EndeavourOS.
It includes both offline speech engines and uses the system's Qt and LayerShellQt.

```sh
sudo pacman -U moji-nook-*.pkg.tar.zst
moji-nook --demo
```

Moji Nook then appears in your application launcher. To remove it, quit the app
and run `sudo pacman -R moji-nook`; your practice profile stays in place (see
[storage](USAGE.md#storage-backups-and-privacy)). The package recipe is in
[`packaging/arch/PKGBUILD`](../packaging/arch/PKGBUILD) if you prefer to build it yourself.

### Build an Arch package

From a committed source checkout, run:

```sh
cd packaging/arch
./build-local.sh
```

This requires Arch Linux's `makepkg` and the build dependencies below. The script
packages the committed `HEAD`, not uncommitted edits, and leaves
`moji-nook-*.pkg.tar.zst` in `packaging/arch/`.

## Install the AppImage

For Linux distributions other than Arch. The AppImage bundles Qt 6.8,
LayerShellQt, and both offline speech engines, and needs glibc 2.35 or newer
(Ubuntu 22.04, Debian 12, Fedora 36, or later).

```sh
chmod +x moji-nook-x86_64.AppImage
./moji-nook-x86_64.AppImage --demo
```

AppImages need FUSE 2. If it reports a FUSE error, install `libfuse2`
(`libfuse2t64` on Ubuntu 24.04 and later), or run it with
`--appimage-extract-and-run`. To add it to your launcher, use a tool such as
Gear Lever or AppImageLauncher. To remove it, quit the app and delete the file;
your practice profile stays in place.

## Platform support

The tested target is **Linux x86_64, KDE Plasma on Wayland**, using CachyOS/Arch.
The overlay uses LayerShellQt to appear above ordinary and fullscreen windows
without taking keyboard focus on arrival. Other Wayland compositors and X11
sessions need their own native checks; offscreen tests cannot establish this.

Linux builds require LayerShellQt. Bundled Japanese speech is currently prepared
only for Linux x86_64. On other architectures, build without bundled speech and
treat the port as unvalidated.

Windows x64 builds compile and pass the test suite in CI, and a portable zip is
published with each release (see [Windows](#windows-experimental)). Desktop
behavior on Windows has not had a hands-on release pass. macOS has no validated
build or overlay behavior and is a development target only.

## Requirements

- Git, CMake **3.21+**, Ninja, and a C++17 compiler.
- Qt **6.5+** with Widgets, Network, Sql (including the SQLite driver), Test, and Multimedia.
- LayerShellQt on Linux.
- Python **3.12+** and network access for the initial speech-bundle preparation.
  Python is used at build time, not by the running app. The scripts require
  `tarfile`'s extraction-filter support; some maintained older Python versions
  also provide it, but 3.12+ is the straightforward choice.
- Recommended fonts: Noto Sans and Noto Sans CJK JP. Other fonts fall back to
  system defaults; a Japanese-capable font is needed for readable cards.

On Arch Linux or CachyOS:

```sh
sudo pacman -S --needed git cmake ninja gcc python qt6-base qt6-multimedia layer-shell-qt
sudo pacman -S --needed noto-fonts noto-fonts-cjk
```

The second command installs the recommended fonts. Package names on other
Linux distributions differ; install their development packages for the same
Qt components and LayerShellQt. Check that their Qt version meets the minimum.
Reference packages: [Qt Multimedia](https://archlinux.org/packages/extra/x86_64/qt6-multimedia/),
[LayerShellQt](https://archlinux.org/packages/extra/x86_64/layer-shell-qt/),
[Noto Sans](https://archlinux.org/packages/extra/any/noto-fonts/), and
[Noto CJK](https://archlinux.org/packages/extra/any/noto-fonts-cjk/).

## Clone and build

```sh
git clone https://github.com/AsterithUlala/moji-nook.git moji-nook
cd moji-nook
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/moji-nook --demo
```

The demo uses sample words in a separate profile and makes no source API
requests. Quit it from the dashboard or tray, then run normal Moji Nook:

```sh
./build/moji-nook
```

Continue with [connecting a source](USAGE.md#connect-your-practice-source).

CTest runs automated checks for practice, source integrations, UI, and speech.
Most widget checks run offscreen; check focus, fullscreen placement, and audio
on your desktop as well. Use `--demo` or a disposable `--data-dir` for testing.

### Speech options

Default Linux x86_64 builds prepare both **Fast** (Open JTalk) and **Quality**
(VOICEVOX) Japanese speech. Hash-pinned downloads come from Debian, GitHub, and
upstream voice repositories during CMake configuration. Fast occupies about
109 MiB and Quality adds about 78 MiB in the prepared bundle; download caches,
Qt, and build outputs need additional space. Runtime playback needs no speech
server, account, Python installation, or first-use downloads.

For a smaller Fast-only build, add this option to the configure command:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMOJI_NOOK_BUNDLE_QUALITY_SPEECH=OFF
```

To skip all bundled speech assets and their downloads:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMOJI_NOOK_BUNDLE_SPEECH=OFF -DMOJI_NOOK_BUNDLE_QUALITY_SPEECH=OFF
```

Then build normally. Practice still works; local Anki recordings can still play.
Qt Multimedia remains a required dependency. When changing bundle options,
use a new build directory and a new install prefix if you need to verify that
old installed assets are absent.

## Optional installation

Running from the build directory is enough to use Moji Nook. To install the
executable, speech helper, assets, and notices into your home directory:

```sh
cmake --install build --prefix "$HOME/.local"
"$HOME/.local/bin/moji-nook" --demo
```

The installation uses `bin/` and `share/moji-nook/speech/` under that prefix. Keep
the helper and speech directory with the executable; copying only `moji-nook`
does not create a portable bundle. Qt and LayerShellQt remain system dependencies.
If `~/.local/bin` is on your `PATH`, the installed app can be launched as `moji-nook`.

On Linux, installation includes a desktop launcher and scalable/raster app icons.
Keep `~/.local/bin` on your `PATH` so the launcher can find the executable.
Installation does not enable login startup.
To start Moji Nook at login on KDE Plasma, add the installed executable in
**System Settings → Autostart**, with `--tray` as its argument. Other desktops
have their own startup settings. Use the path to your installed executable;
the [Moji Nook app icon](../assets/brand/moji-nook-128.png) is available for a custom launcher.

## Update an existing checkout

Quit Moji Nook first and [back up your profile](USAGE.md#storage-backups-and-privacy).
From your existing checkout:

```sh
git switch main
git pull --ff-only origin main
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/moji-nook
```

Repeat `cmake --install build --prefix "$HOME/.local"` if you use the installed
copy. Keep any speech options you deliberately selected in the configure command.
If Git reports local changes or divergent history, preserve your changes before
updating; do not use a hard reset as an installation step.

## Troubleshooting

**Qt6 or LayerShellQt not found:** install the development packages, not only
runtime libraries. If Qt is installed outside the system prefix, pass its prefix
using `-DCMAKE_PREFIX_PATH=/path/to/Qt` when configuring.

**Speech downloads fail:** verify internet access to the manifest URLs in
`scripts/speech-assets.json` and `scripts/quality-assets.json`, then configure
again. Downloads are hash checked. For a build that needs no speech downloads,
use the bundle-off options above.

**“Already running”:** check the tray for an existing instance. Moji Nook allows
one process per data directory. Quit the existing process before relaunching.

**No cards available:** select a source and complete its sync. The source needs
eligible learned items, not only new lessons/cards. See the [source setup guide](USAGE.md#connect-your-practice-source).

**Cards are hidden or take focus unexpectedly:** check the selected display and
corner, and test on the supported Plasma Wayland environment. A successful
build or offscreen test does not validate a different compositor's fullscreen behavior.

**No Japanese text or sound:** install a Japanese-capable font; check Appearance
settings, the output device, and the system mixer. Voice and feedback-tone volume
are separate. For an installed build, verify that its speech helper and asset
directories were installed too. Missing audio does not prevent practice.

## Windows (experimental)

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds the zip
with MSVC and Qt 6.8 and runs the test suite on every push; a `v*` tag attaches
it, with the Linux packages, to a GitHub release.

The zip is portable: extract it anywhere and run `moji-nook.exe`. It bundles Qt,
the MSVC runtime, and the Quality speech engine, so nothing else needs to be
installed. Practice data lives
in `%LOCALAPPDATA%\MojiNook\Moji Nook`. To remove Moji Nook, quit it from the
tray and delete the extracted folder.

Known limits:

- **Quality voices only.** VOICEVOX neural voices are included; the Fast
  (Open JTalk) voices are Linux-only for now.
- **Overlay behavior is untested by hand.** Cards use a topmost window that
  should not take focus, but fullscreen games and exclusive-fullscreen apps
  may cover it.
- **Unsigned.** Windows SmartScreen may warn on first launch; choose
  **More info → Run anyway**.
- **Token storage.** The WaniKani token is a plaintext file in your profile
  directory, protected by your account's application-data permissions rather
  than Windows Credential Manager. See [storage](USAGE.md#storage-backups-and-privacy).

To build locally, install Qt 6.5+ (with Multimedia), CMake, Ninja, Python 3.12+,
and Visual Studio's C++ tools, then from a *Developer PowerShell*:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMOJI_NOOK_BUILD_TREE_SPEECH=OFF
cmake --build build
cmake --install build --prefix dist
windeployqt --release dist\moji-nook.exe
```
