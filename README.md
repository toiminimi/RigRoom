# 🎛️ RigRoom

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Build System](https://img.shields.io/badge/Build-CMake_3.20%2B-brightgreen.svg)](CMakeLists.txt)
[![Framework](https://img.shields.io/badge/Framework-Qt_6-green.svg)](https://qt.io)
[![Audio Engine](https://img.shields.io/badge/Audio-JACK_%7C_PipeWire-orange.svg)](https://jackaudio.org)
[![Plugin Formats](https://img.shields.io/badge/Formats-LV2_%7C_VST3_%7C_CLAP_%7C_NAM-purple.svg)](#features)

**RigRoom** is an open-source guitar and bass multieffects host and real-time audio plugin router for Linux.

---

## ✨ Features

### 🔌 Plugin Hosting & External UIs
- **Plugin Standards**: Native support for **LV2**, **VST3**, and **CLAP** audio plugin formats.
- **Neural Amp Modeler (NAM) Integration**: Load `.nam` neural amp and pedal captures into hosted NAM plugins.
- **External Plugin UIs**: Native window embedding for plugin interfaces (GTK/X11 via Suil, VST3 IPlugView, CLAP extensions).
- **Parameter Inspector**: Parameter control panel with knobs, sliders, and plugin preset dropdowns.
- **Plugin Browser**: Instant search across all installed plugins (every word must match name, maker, type or tag; best matches first), a sidebar with *All*, *Favorites*, *Recently used* and categories, format chips (LV2 / CLAP / VST3), and an info panel with image, maker, version, license, description, audio I/O and tags. Plugins without an image get generated artwork.
  - **＋ Plugins** in the top bar (Ctrl+B) opens it as a panel you can keep open: drag plugins onto the signal chain (onto a free slot or between blocks), or double-click to add at the end of the main chain. The **+** buttons on the board still add one plugin at a time.
  - CLAP scan results are cached (`~/.cache/RigRoom/plugin-scan.json`), so unchanged plugins aren't reloaded at startup. Custom search directories for VST3, CLAP and LV2 are set in Settings.

### 🔀 Parallel Signal Routing Canvas
- **Multi-Lane Canvas**: Interactive node graph supporting up to 5 parallel audio lanes (`Main`, `Path B1`, `Path B2`, `Path C1`, `Path C2`) with a configurable number of columns (4 to 16).
- **Drag-and-Drop Workflow**: Drag `DRAG SPLIT` to create parallel branches, drag pedal blocks between columns and lanes, and drag split/merge handles to adjust routing positions.
- **Insert Between Blocks**: Thin insert markers sit between blocks, before a split, after a merge and at the end of a path; hover shows a **+**. Adding (or dropping a dragged block) there makes room without adding columns: blocks slide into a free slot of that lane first (only within the stretch between its splits and merges, so nothing changes path), then right into the nearest column that is empty in every lane, or, if there is none on the right, earlier blocks slide left into one. Where a parallel path splits off or merges back there are two markers, one on each side of the junction, so a block can go before or after the split (or merge) on that lane. The signal path stays exactly the same. If no column is free, the status bar says so (add a column with Columns + or remove a block). Adding inside a parallel path widens that path (and any path containing it). While dragging over a marker the blocks preview where they will move.
- **Split Modes**: **Copy** (full duplicate of the source) or **A/B** (power split between Path A and the branch).
- **Visual Signal Flow**: Animated glowing connection wires showing active signal paths, zoom controls (wheel zoom, 100% reset), and missing plugin alerts.

### ☁️ TONE3000 Neural Model Integration
- **In-App Search**: Search and filter thousands of open-source neural amp and pedal captures on TONE3000.
- **Creators**: Click a creator's name to see all their uploads with a profile header (avatar, verified badge, uploads/downloads/favorites, link to their web profile). **← Back to search** returns to exactly where you were (same search, results, scroll and selection). The **Creators** toggle next to the search box searches creators by name.
- **Impulse Responses**: IR loader blocks (e.g. TooB Cab IR, x42 convolver, or any plugin with an IR file slot) show **Browse TONE3000 IRs** next to *Load File…*: search cabinet IRs, preview them live in that slot, and load them. IRs keep their real file type and are cached in `~/.cache/RigRoom/tone3000/ir/`.
- **One-Click Loading**: View model details, tags, sample rates, and profile images, and download models directly into NAM plugin nodes.
- **Requirements**: A **Neural Amp Modeler LV2 plugin** must be installed on the system. Users must provide their own **TONE3000 secret key** (available at tone3000.com/settings) in the application settings. Secure key storage requires a running Secret Service provider — **gnome-keyring** on GNOME or **KDE Wallet** (with the Secrets DBus interface enabled) on KDE Plasma.

### 🎚️ Audio Engine & Display Compatibility
- **Real-Time Audio Engine**: Low-latency JACK Audio Connection Kit and PipeWire-JACK backend.
- **Display Server Support**: Runs on X11 and Wayland sessions with XWayland. Native plugin UIs currently require X11/XWayland.
- **Live Monitoring**: Real-time master input/output peak meters with clipping warnings, DSP load indicator, and XRun dropout counter.
- **Branch Gain Controls**: Individual row volume, main mix level, panning, and polarity invert per branch.

### 💾 Preset Management
- **Pedalboard Presets**: Save, load, rename, duplicate, and delete board configurations, including each block's on/off state.
- **Preset Panel**: Under the top bar, one panel reads left to right like a floor unit: **Bank → Presets (A–D) → Scenes**, with the preset actions at the right edge. Preset slots and scenes use the same footswitch-style tiles.
  - **Bank**: ◀ ▶ only change which bank is shown (like bank up/down on hardware); nothing loads until you click a preset tile. The bank label turns orange while you are looking at a bank other than the loaded preset's. Banks keep their number and can also have a name (`04 Floyd`): double-click the bank label (unnamed banks show ✎). "▦ All banks" (Ctrl+P) opens the grid.
  - **Presets**: every bank holds four presets, `A`–`D`, and a preset stays in its bank and letter until you move it. The number of banks is set in Settings (it can't go below the highest bank in use). Click to load, double-click to rename, right-click for load/save here/rename/duplicate/delete. Click an empty tile to save the current board there. The loaded preset is filled; orange with a dot means unsaved changes.
  - **Preset actions** (right edge): **Save** (Ctrl+S) and **⋯** (New, Save As, Rename, Duplicate, Delete, All banks).
  - **Canvas label**: the top-left corner of the canvas shows what is playing, e.g. `04A · 2 Lead` with the preset name large. Click it to jump back to the preset's bank, double-click to rename the preset.
  - **Grid of all banks**: drag to move or swap, right-click to rename/duplicate/delete, double-click a bank to name it, filter by preset, scene or bank name. Each preset lists its numbered scenes (`1 Clean · 2 Lead`).
  - Slot order and bank names are stored in `~/.config/RigRoom/library.json`; existing presets are placed alphabetically on first run. Flat slot numbers (01A = 0, 01B = 1, …) are what MIDI Program Change will map to.
- **Scenes**: Up to 8 scenes per preset, shown on the right of the performance bar. A scene stores every block's on/off state, every parameter value, and a scene level trim. To keep a knob the same in all scenes (a master volume, say), right-click it in the Inspector → *Keep same in all scenes* (marked =). Plugin files such as NAM models and IRs are shared by all scenes. Blocks that change between scenes carry an amber **S** badge. Switching scenes never reloads plugins, so there is no dropout and delay/reverb tails ring on. Edits made while a scene is active are remembered when you switch away and saved with the preset. Scene tiles show the scene number and name. Double-click a scene to rename it; right-click to pick a color (named swatches), duplicate, store the current board into it, or delete it.
- **Preset Level**: Per-preset loudness (−24 to +12 dB), applied before the global Master Out. Click the System Output node to adjust it and the active scene's level trim; both are stored with the preset.
- **Click-free Block Switching**: Turning a block on or off crossfades over about 5 ms instead of cutting.
- **Parameter Synchronization**: Automatic parameter and preset state sync upon loading presets.

---

### 🎹 MIDI Control
RigRoom registers a JACK MIDI input (`RigRoom:midi_in`). Pick your controller in **Settings ▸ MIDI** (USB devices appear through PipeWire or a2jmidid); the **● MIDI** indicator in the status bar flashes on incoming messages.

- **Global (Settings ▸ MIDI)**: device, channel (Omni or 1–16), and commands that work in every preset:
  - **Program Change selects** *Presets* (PC 0 = 01A, PC 1 = 01B, …), *Scenes* of the loaded preset (PC 0 = scene 1 … PC 7 = scene 8, for footswitches that can only send PC) or *Nothing*. **Bank Select** (CC 0) reaches preset slots beyond 128; tick *counts programs from 1* if your controller's PC 1 should mean the first preset or scene.
  - **Performance commands** (each has a Learn button and can be set to Off; switches fire when pressed, value ≥ 64):

    | Command | Default CC |
    | :--- | :--- |
    | Previous / next preset | 102 / 103 |
    | Bank down / up (changes the shown bank only) | 104 / 105 |
    | Preset A–D in the shown bank | 106–109 |
    | Scene select (value 0–7 = scene 1–8) | 69 |
    | Scene 1 … Scene 8 (one CC per switch) | Off |
    | Previous / next scene | 114 / 115 |

    Defaults use CC 69 (as Helix snapshots) and the MIDI spec's undefined range, so mod wheel (1), volume (7), expression (11) and sustain (64) stay free.
- **Switching scenes from a controller** (no audio gap: plugins are not reloaded, delay and reverb tails ring on). Use whichever your controller can send:

  | Your controller sends… | Set up |
  | :--- | :--- |
  | One CC with a chosen value per switch (most programmable controllers) | **Scene select**: e.g. switch 1 = CC 69 value 0, switch 2 = CC 69 value 1 |
  | A fixed CC per switch, 127 on press | **Scene 1–8**: give each scene its own CC (Learn: press the switch) |
  | Only Program Change | **Program Change selects: Scenes** |
  | Up / down only | **Previous / Next scene** |

  Loading a different preset (PC in Presets mode, Preset A–D, previous/next) rebuilds the signal chain and has a short gap; use scenes for changes within a song.
- **Per preset (MIDI Learn)**: right-click a block → *MIDI Learn On/Off…*, or right-click a knob in the Inspector → *MIDI Learn…*, then press a switch or move a pedal. Blocks with assignments get an **M** badge; assigned knobs are marked **M**. Edit or remove them in **⋯ ▸ MIDI Assignments…**:
  - Block on/off: **Toggle on press** (momentary footswitches) or **Follow value** (switches that send 127 for on, 0 for off).
  - Parameters: the range the pedal sweeps (from/to %) and invert. Pedal moves don't mark the preset as edited.
- **Switching from MIDI** never stops at a "save changes?" prompt: unsaved edits are discarded and the status bar says so. Changing presets with the mouse still asks.

## 🛠️ System Requirements & Building

### 🔊 Audio Engine Requirements

RigRoom uses **PipeWire** or **JACK** for real-time guitar audio processing.

- **Ubuntu 22.04 / 24.04 & Debian 12 (PipeWire)**:
  Modern Ubuntu distros run PipeWire by default. Install PipeWire's JACK compatibility library:
  ```bash
  sudo apt install -y pipewire-jack
  ```
- **Fedora / Arch Linux / Manjaro**:
  PipeWire-JACK is included out-of-the-box. Ensure the service is active (`systemctl --user status pipewire`).

---

### Prerequisites for Building from Source

#### **Ubuntu 22.04 / 24.04 & Debian 12**
```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
    qt6-base-dev qt6-base-private-dev \
    libjack-jackd2-dev liblilv-dev libsuil-dev libsecret-1-dev libx11-dev
```

#### **Arch Linux / Manjaro / EndeavourOS**
```bash
sudo pacman -S --needed base-devel cmake pkgconf \
    qt6-base lilv suil jack2 libx11 libsecret
```

#### **Fedora 43+**
```bash
sudo dnf install -y gcc-c++ cmake pkgconfig \
    qt6-qtbase-devel lilv-devel suil-devel jack-audio-connection-kit-devel libX11-devel libsecret-devel
```

#### **openSUSE Tumbleweed / Leap**
```bash
sudo zypper install -y gcc-c++ cmake pkg-config \
    libqt6-qtbase-devel lilv-devel suil-devel libjack-devel libX11-devel libsecret-devel
```

### AppImage

Release AppImages are built against an Ubuntu 22.04 glibc baseline and use the
host PipeWire-JACK or JACK service for real-time audio. They do not bundle
glibc or an audio server. To build one locally, run:

```bash
./packaging/build-appimage.sh
```

Docker or Podman is used automatically when available. The output is written
to the repository root. Install PipeWire-JACK or JACK on the target system
before using audio processing.

Release downloads include a `.sha256` file. Verify it with:

```bash
sha256sum --check RigRoom-<version>-x86_64.AppImage.sha256
```

### Compiling RigRoom

```bash
# 1. Clone repository
git clone --recurse-submodules https://github.com/toiminimi/RigRoom.git
cd RigRoom

# 2. Configure build with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build executable
cmake --build build -j$(nproc)

# 4. Run RigRoom
./build/RigRoom
```

---

## ⌨️ Keyboard Shortcuts & Canvas Controls

| Shortcut | Action |
| :--- | :--- |
| **Ctrl + N** | Create new empty preset |
| **Ctrl + S** | Save current preset |
| **Ctrl + Shift + S** | Save preset as... |
| **Ctrl + P** | Open the grid of all banks |
| **Ctrl + PageUp** / **Ctrl + PageDown** | Load previous / next preset |
| **Ctrl + Shift + PageUp** / **Ctrl + Shift + PageDown** | Show previous / next bank (does not load) |
| **Alt + 1 … 8** | Select scene 1–8 |
| **Alt + Left** / **Alt + Right** | Previous / next scene |
| **Ctrl + =** / **Ctrl + +** | Add canvas column (also in the canvas zoom box) |
| **Ctrl + -** | Remove trailing empty canvas column |
| **Mouse Wheel** / **Pinch** | Zoom canvas in / out |
| **Auto** (canvas zoom box) | Keep the whole signal path in view on resize and edits; zooming by hand turns it off |
| **Drag `SPLIT` Tool** | Drop onto gap to create parallel branch |
| **Drag Routing Handle** | Move split or merge position |

---

## ⚖️ License & Open Source Credits

RigRoom is open-source software released under the **GNU General Public License v3.0 (GPLv3)**. See [LICENSE](LICENSE) for details.

See [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), and
[PRIVACY.md](PRIVACY.md) for project policies.

### Third-Party Acknowledgments
- **Qt 6 Framework** ([LGPLv3](https://www.qt.io/)) — GUI & event loop
- **JACK Audio Connection Kit** ([LGPL](https://jackaudio.org/)) — Real-time audio engine
- **Lilv & Suil Libraries** ([ISC](https://drobilla.net/software/lilv)) — LV2 hosting & UI embedding
- **CLAP C API** ([MIT](https://clap.technology/)) — CLever Audio Plugin specification
- **Steinberg VST3 SDK** ([MIT](https://www.steinberg.net/)) — VST3 interface headers

*VST is a registered trademark of Steinberg Media Technologies GmbH.*
