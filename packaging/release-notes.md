Moji Nook is an early alpha. Please [report problems](https://github.com/AsterithUlala/moji-nook/issues).

## What's new

- **WaniKani review mode (optional, off by default).** Turn on
  **Settings → WaniKani → Do WaniKani reviews here** and your due WaniKani
  reviews come first on the cards. You type every answer, Moji Nook grades it by
  WaniKani's rules, and finished reviews are submitted to WaniKani. It needs a
  token with `reviews:create` and never starts lessons. See
  [review mode](https://github.com/AsterithUlala/moji-nook/blob/main/docs/USAGE.md#do-wanikani-reviews-in-moji-nook-optional).
- Fixes for reminders that stopped, a double-click answering the next card,
  snooze resets, speech that got stuck on "Preparing audio…", CSV export, and
  several layout issues.
- The Arch package now depends on `qt6-svg`.

Full list: [CHANGELOG.md](https://github.com/AsterithUlala/moji-nook/blob/main/CHANGELOG.md).

## Downloads

| File | For |
| --- | --- |
| `moji-nook-*-x86_64.pkg.tar.zst` | Arch Linux, CachyOS, EndeavourOS, Manjaro. Uses your system Qt and LayerShellQt. |
| `moji-nook-x86_64.AppImage` | Any other 64-bit Linux distribution. Self-contained. |
| `moji-nook-windows-x64.zip` | Windows 10 and 11, 64-bit. Portable, experimental. |

**Arch and derivatives**

```sh
sudo pacman -U moji-nook-*.pkg.tar.zst
moji-nook --demo   # try sample words, no account needed
```

**Other Linux**

```sh
chmod +x moji-nook-x86_64.AppImage
./moji-nook-x86_64.AppImage --demo
```

If it reports a FUSE error, install `libfuse2` (Debian/Ubuntu: `sudo apt install libfuse2t64`
or `libfuse2`) or run it with `--appimage-extract-and-run`.

**Windows (experimental)**

Extract the zip anywhere and run `moji-nook.exe`. Windows SmartScreen may warn
because the app is unsigned; choose **More info → Run anyway**. Windows includes
the Quality (neural) voices but not the Fast voices.

Check a download against `SHA256SUMS` with `sha256sum -c SHA256SUMS --ignore-missing`.

Full setup, speech details, and troubleshooting are in the
[installation guide](https://github.com/AsterithUlala/moji-nook/blob/main/docs/INSTALL.md).
