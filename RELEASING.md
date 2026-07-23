# PedalBoard Release & Versioning Guidelines

This project follows [Semantic Versioning (SemVer 2.0.0)](https://semver.org/).

## Current Development State: `v0.9.0-dev`

PedalBoard is currently in active pre-release development. All development builds display `v0.9.0-dev`.

---

## Versioning Structure: `MAJOR.MINOR.PATCH`

1. **PATCH (`x.x.1`)**: Bug fixes, crash recoveries, latency tweaks, or visual polish.
2. **MINOR (`x.1.0`)**: New features, new audio plugin format support, new settings, or UI overhauls (backward-compatible).
3. **MAJOR (`1.0.0`)**: Public initial release or breaking preset format/routing architecture overhauls.

---

## Single Source of Truth

The version is centrally defined in **`CMakeLists.txt`**:

```cmake
project(PedalBoard VERSION 0.9.0 LANGUAGES CXX)
```

During build configuration, CMake generates `Version.h` from `src/Version.h.in`, automatically updating:
- Application Settings **About** tab
- Main Window title bar
- Build metadata & saved preset version tags

---

## Automated Version Tracking & Suggestions

During pair programming sessions, the AI coding partner (Antigravity) tracks all code changes, bug fixes, and feature additions, and will proactively suggest version bumps:

- **After a batch of bug fixes**: Suggests bumping PATCH version.
- **After adding new user features**: Suggests bumping MINOR version.
- **Preparing for initial public launch**: Suggests bumping to `1.0.0`.

---

## Step-by-Step Release Process (Project Owner Execution)

When the project owner decides to issue a release:

1. **Update `CMakeLists.txt`**:
   Change version (e.g. `VERSION 1.0.0`).
2. **Remove `-dev` flag in `Version.h.in`** for tag build.
3. **Tag release in Git**:
   ```bash
   git tag -a v1.0.0 -m "PedalBoard v1.0.0 - Initial Public Release"
   git push origin v1.0.0
   ```
