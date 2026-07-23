# PedalBoard TODO & Roadmap

## 🔴 Priority 0: Critical Bugs
- [x] **If plugin is missing app crashes**: If plugin which was earlier available is missing, make it recover and load the pedalboards but highlight that plugin block so that user know it's missing. make sure that info about plugin is saved so that once plugin comes back available is possible to load again
- [x] **Plugin GUI Preset Switch Crash**: App crashes when switching pedalboard presets while external plugin GUIs are open.

## 🟠 Priority 1: Core Audio & DSP Engine
- [x] **Plugin Preset State Sync**: When loading a pedalboard preset, automatically reflect the loaded plugin preset in the Inspector dropdown.
- [x] **In/Out Peak & Clipping Indicators**: Add visual audio peak meters and clipping warnings for master input and output.
- [x] **XRun Detection**: Monitor and display audio buffer xruns/dropouts in the status bar/meter.

## 🟡 Priority 2: Canvas & UI/UX Improvements
- [x] **Disable Split Drag when Lanes Full**: Prevent dragging split nodes when maximum lane capacity is reached.
- [x] **Pedalboard Preset Handling & Canvas Display**: Better handling and UI for pedalboard presets (e.g. showing active pedalboard preset selector directly on the canvas/top header).
- [x] **Nicer Plugin Blocks**: Upgrade visual design, shadows, icons, and signal flow aesthetics of plugin blocks on canvas.
- [x] **Canvas Zoom**: Add zoom controls / wheel zoom to navigate complex pedalboard signal trees.
- [x] **Plugin Browser Format Filtering & Rich Details**: Separate LV2 / CLAP / VST3 format filter tabs from categories, and display an enhanced plugin metadata detail panel (format, vendor, channel layout, URI, parameters).

## 🔵 Priority 3: Settings & TONE3000 Integration
- [x] **Settings API Key Management**: Add "Clear API Key" button, confirmation indication on save, and helpful guide/links on acquiring a TONE3000 API key.
- [x] **TONE3000 Prompt Dismissal & Feature Gating**: Allow dismissing API key suggestions and hide API-dependent features when no key is set.
- [x] **Custom Plugin Paths & Type Toggles**: Allow users to specify custom VST3/CLAP/LV2 search directories and toggle specific plugin formats on/off.

## 🟢 Priority 4: Release, Branding & Documentation
- [ ] **App Naming**: Decide on official application name.
- [ ] **GitHub README.md**: Create comprehensive README with feature highlights, build instructions, and screenshots.
- [ ] **About Dialog & OSS Licenses**: Add About dialog disclosing open-source components and licenses used (JUCE, Qt, etc.).
- [ ] **Packaging & Code Cleanup**: Prepare Flatpak / AppImage packaging scripts and clean up code for initial GitHub public release.


