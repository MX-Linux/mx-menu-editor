mx-menu-editor
===================

MX tool for editing Xfce menu

Makeing this readonly since we moved to menulibre in Xfce and native editor in Plasma

## AppImage

Download the x86-64 AppImage from the **Assets** section of a GitHub release,
make it executable, and run it as your normal desktop user. It includes Qt,
translations, and offline help, and edits the Xfce menu on the host.
If FUSE is unavailable, run it with `--appimage-extract-and-run`.

To build locally on Debian 12 x86-64, install the build dependencies and run:

```sh
sudo apt install build-essential cmake ninja-build dpkg-dev curl file qt6-base-dev \
    qt6-tools-dev qt6-tools-dev-tools libqt6svg6 qt6-translations-l10n qt6-wayland
./make-appimage.sh
```

The builder verifies and caches the packaging tools, runs the tests, and writes
`build-appimage/mx-menu-editor-<version>-x86_64.AppImage` using the version in
`debian/changelog`. GitHub Actions builds and attaches it for non-Arch release tags.
