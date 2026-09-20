# Changelog

## 0.11.1 - 2026-09-20

- Fixed: RigRoom crashed on startup if any installed LV2 bundle was missing its name; such a plugin is now listed by its URI instead. Plugins and ports with incomplete metadata no longer bring the whole application down.
- Crash reports record the version and build ID of the binary that produced them.

## 0.11.0 - 2026-09-20

- Secure Secret Service storage for user-provided TONE3000 API keys.
- Reproducible VST3 submodule checkout, safer plugin rescan and LV2 world lifetime.
- Ubuntu 22.04 AppImage build baseline.
- Preset library in numbered banks of four (01A–32D) with named banks, a grid of all banks (drag to move or swap), and a preset panel: bank selector, A–D footswitch tiles, scenes and Save.
- Scenes: up to 8 per preset switching block on/off, all parameter values (right-click a knob to keep it the same in all scenes) and a scene level without reloading plugins (no audio gap). Blocks changed by scenes get an "S" badge.
- MIDI-controlled parameters no longer fight with scenes: learning a CC keeps that parameter the same in all scenes, and each assignment chooses whether the controller waits until it reaches the stored value (default) or takes over at once, as a pedal should. Knob labels show "=" and "CC n" marks.
- Previous/next preset can be limited to the shown bank (Settings > MIDI), for controllers that have bank up/down; applies to the buttons and keyboard too.
- MIDI input: Program Change for presets or scenes, global CC commands (scene select by value, Scene 1–8, previous/next, bank, A–D) with Learn, and per-preset MIDI Learn for block on/off and expression pedals.
- Click-free block on/off, canvas auto-fit with animated zoom, and a canvas label showing the loaded preset and scene.
- Insert between blocks: markers between blocks, before splits, after merges and at path ends make room by sliding blocks within the lane, else later blocks right into a free column (or earlier blocks left when the right side is full) in every lane; splits and merges get a marker on each side, and the drag preview slides splits and merges along with the blocks, without adding columns or changing the signal path; parallel paths widen when a block is added inside them. Live preview while dragging.
- Plugin pictures (experimental, off by default): opens each plugin's own GUI on a hidden display (Xvfb or kwin_wayland), photographs it and shows it in the browser's info panel; click for a full-size view. X11, Gtk and CLAP GUIs are all covered. Run from Settings > Plugins & Formats: one button plus a drop-down choosing what it covers (never tried / also the failed ones / everything again), with the count on the button, progress while it runs and a Cancel button that stops after the current plugin. Background, lowest priority, one process per plugin so a crash costs only that picture; a separate box turns on doing new plugins after a scan. Plugins without a window, or whose GUI will not open, are recorded and skipped by later runs.
- Settings pages rebuilt to one pattern: long explanations moved behind an "i" button on each section heading, headings above the frame instead of across it, one hint colour, left-aligned labels in a shared column, and a hand-drawn icon set for the tabs.
- New plugin browser: instant search with ranking, sidebar (All / Favorites / Recently used / categories), format chips, info panel with picture and full metadata (the list itself shows category cards); drag plugins onto the board; "＋ Plugins" (Ctrl+B) keeps it open next to the board. Better category detection and a CLAP scan cache.
- TONE3000: creator pages (click a name) and creator search; impulse responses browsable and previewable directly in IR loader blocks.
- Fixed: TONE3000 previews could overwrite a loaded file; capture variants of different tones could overwrite each other in the cache.
- Fixed: toggling a block did not mark the preset as edited; built-in Bypass blocks reloaded as missing plugins; MIDI ports could appear in the audio device lists.
