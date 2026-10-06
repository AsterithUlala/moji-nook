# Contributing to Moji Nook

Moji Nook is an early alpha under active development. Start with the
[build instructions](docs/INSTALL.md#clone-and-build). Contributions are accepted under the
project's [MIT License](LICENSE).

## Report a problem

Use the repository's [GitHub issues](https://github.com/AsterithUlala/moji-nook/issues).
Include the version or commit, OS, desktop/compositor, Qt version if known,
practice source, what you did, and the expected and actual behavior. For build
failures, include the configure/build error. For overlay issues, say whether
Wayland or X11 is in use and whether the affected window is fullscreen.

Remove API tokens, private cache or database content, account details, and
personal study material from logs and screenshots. Prefer reproducing the issue
with `--demo` or a disposable profile.

## Propose a change

Keep changes focused on a concrete user problem. Test changes with `--demo`
or a disposable `--data-dir`.

Run the CMake build and CTest suites from the installation guide. For UI, focus,
placement, or audio changes, also check the app on the affected desktop.
Describe what was tested and any platform limits in the pull request.

Update the user guides when behavior changes. Check navigation and setting
names against the actual UI, and verify defaults against the source.

Use synthetic fixtures and screenshots in the repository. Keep personal study
counts, device names, local paths, tokens, caches, and exported decks out of commits.
Preserve required upstream attribution when simplifying documentation.

## Publish a release

Every push builds the Arch package, AppImage, and Windows zip in the **Build**
workflow. To publish them, either push a `v*` tag, or open **Actions → Build →
Run workflow** on `main` and enter a new tag such as `v0.1.1-alpha`. Tags
containing `alpha` or `beta` are marked as pre-releases. Update `_tag` in
`packaging/arch/PKGBUILD` and the version in `CMakeLists.txt` and
`src/main.cpp` first.
