# Changelog

## Unreleased

- Added secure Secret Service storage for user-provided TONE3000 API keys.
- Restored reproducible VST3 submodule checkout.
- Fixed plugin rescan registration and LV2 world lifetime safety.
- Added Ubuntu 22.04 AppImage build baseline support.

## 0.11.0

- Preset library in numbered banks of four (01A–32D) with named banks, a grid of all banks (drag to move or swap), and a preset panel: bank selector, A–D footswitch tiles, scenes and Save.
- Scenes: up to 8 per preset switching block on/off, all parameter values (right-click a knob to keep it the same in all scenes) and a scene level without reloading plugins (no audio gap). Blocks changed by scenes get an "S" badge.
- MIDI input: Program Change for presets or scenes, global CC commands (scene select by value, Scene 1–8, previous/next, bank, A–D) with Learn, and per-preset MIDI Learn for block on/off and expression pedals.
- Click-free block on/off, canvas auto-fit with animated zoom, and a canvas label showing the loaded preset and scene.
- Insert between blocks: markers between blocks, before splits, after merges and at path ends make room by sliding blocks within the lane, else later blocks right into a free column (or earlier blocks left when the right side is full) in every lane; splits and merges get a marker on each side, and the drag preview slides splits and merges along with the blocks, without adding columns or changing the signal path; parallel paths widen when a block is added inside them. Live preview while dragging.
- New plugin browser: instant search with ranking, sidebar (All / Favorites / Recently used / categories), format chips, info panel with image or generated artwork and full metadata; drag plugins onto the board; "＋ Plugins" (Ctrl+B) keeps it open next to the board. Better category detection and a CLAP scan cache.
- TONE3000: creator pages (click a name) and creator search; impulse responses browsable and previewable directly in IR loader blocks.
- Fixed: TONE3000 previews could overwrite a loaded file; capture variants of different tones could overwrite each other in the cache.
- Fixed: toggling a block did not mark the preset as edited; built-in Bypass blocks reloaded as missing plugins; MIDI ports could appear in the audio device lists.
