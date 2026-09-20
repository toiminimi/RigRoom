# RigRoom Release & Versioning Guidelines

This project follows [Semantic Versioning (SemVer 2.0.0)](https://semver.org/).

RigRoom is in active pre-release development. Development builds append `-dev` to
the version, so they display `0.11.1-dev`; release builds are configured with
`-DRIGROOM_DEV_BUILD=OFF` and display `0.11.1`.

---

## Versioning Structure: `MAJOR.MINOR.PATCH`

1. **PATCH (`x.x.1`)**: Bug fixes, crash recoveries, latency tweaks, or visual polish.
2. **MINOR (`x.1.0`)**: New features, new audio plugin format support, new settings, or UI overhauls (backward-compatible).
3. **MAJOR (`1.0.0`)**: Public initial release or breaking preset format/routing architecture overhauls.

Every published build needs its own version number. A backtrace only names module
offsets, so the version and build ID in a crash report are what tie it to a
specific binary; reusing a version number for two different builds makes a
report impossible to place.

---

## Single Source of Truth

The version is centrally defined in **`CMakeLists.txt`**:

```cmake
project(RigRoom VERSION 0.11.1 LANGUAGES CXX)
```

During build configuration, CMake generates `Version.h` from `src/Version.h.in`,
which feeds:
- the version label in the **About** dialog (`src/ui/AboutDialog.cpp`)
- the version line in crash reports (`src/main.cpp`)
- the AppImage file name and its `.sha256` (`packaging/build-appimage.sh`)

The preset format has its own counter (`formatVersion`) that is independent of
the application version and is bumped only when the preset format changes.

---

## Step-by-Step Release Process (Project Owner Execution)

1. **Update `CMakeLists.txt`**: change `project(RigRoom VERSION ...)`.
2. **Add a `CHANGELOG.md` section** for the new version, dated.
3. **Add a `<release>` entry** to `packaging/org.rigroom.RigRoom.appdata.xml`,
   newest first, with the same date as the changelog.
4. **Tag the release**:
   ```bash
   git tag -a v0.11.1 -m "RigRoom v0.11.1"
   git push origin v0.11.1
   ```
   Pushing the tag is what publishes: `.github/workflows/release.yml` builds the
   AppImage and creates the GitHub Release with both assets attached. Release
   notes are generated from the commits, so the changelog text is not copied
   there automatically.

A local `./packaging/build-appimage.sh` run is useful for testing the artifact
before tagging, but its output is not what gets published.
