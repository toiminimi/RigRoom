# Contributing To RigRoom

## Getting Started

Clone recursively so the pinned VST3 interfaces are available:

```bash
git clone --recurse-submodules https://github.com/toiminimi/RigRoom.git
```

Build with CMake as described in the README. Keep changes focused, preserve
real-time audio safety, and test plugin scanning and routing changes with
PipeWire-JACK or JACK.

## Pull Requests

Describe the user-visible behavior, include verification steps, and avoid
committing build artifacts, downloaded plugins, presets containing credentials,
or private model files.

By contributing, you agree that your contribution is licensed under GPL-3.0-or-later.
