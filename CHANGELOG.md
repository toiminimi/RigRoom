# Changelog

## Unreleased

- Added secure Secret Service storage for user-provided TONE3000 API keys.
- Restored reproducible VST3 submodule checkout.
- Fixed plugin rescan registration and LV2 world lifetime safety.
- Added Ubuntu 22.04 AppImage build baseline support.

## 0.11.0

- Preset library in numbered banks of four (01A–32D) with named banks, a grid of all banks (drag to move or swap), and a preset panel: bank selector, A–D footswitch tiles, scenes and Save.
- Scenes: up to 8 per preset switching block on/off, chosen parameters and a scene level without reloading plugins (no audio gap). Blocks changed by scenes get an "S" badge.
- MIDI input: Program Change for presets or scenes, global CC commands (scene select by value, Scene 1–8, previous/next, bank, A–D) with Learn, and per-preset MIDI Learn for block on/off and expression pedals.
- Click-free block on/off, canvas auto-fit with animated zoom, and a canvas label showing the loaded preset and scene.
- Fixed: toggling a block did not mark the preset as edited; built-in Bypass blocks reloaded as missing plugins; MIDI ports could appear in the audio device lists.
