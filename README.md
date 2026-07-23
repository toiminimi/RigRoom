# 🎛️ PedalBoard

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Build System](https://img.shields.io/badge/Build-CMake_3.20%2B-brightgreen.svg)](CMakeLists.txt)
[![Framework](https://img.shields.io/badge/Framework-Qt_6-green.svg)](https://qt.io)
[![Audio Engine](https://img.shields.io/badge/Audio-JACK_%7C_PipeWire-orange.svg)](https://jackaudio.org)
[![Plugin Formats](https://img.shields.io/badge/Formats-LV2_%7C_VST3_%7C_CLAP_%7C_NAM-purple.svg)](#features)

**PedalBoard** is an open-source guitar and bass multieffects host and real-time audio plugin router for Linux.

---

## ✨ Features

### 🔌 Plugin Hosting & External UIs
- **Plugin Standards**: Native support for **LV2**, **VST3**, and **CLAP** audio plugin formats.
- **Neural Amp Modeler (NAM) Integration**: Load `.nam` neural amp and pedal captures into hosted NAM plugins.
- **External Plugin UIs**: Native window embedding for plugin interfaces (GTK/X11 via Suil, VST3 IPlugView, CLAP extensions).
- **Parameter Inspector**: Parameter control panel with knobs, sliders, and plugin preset dropdowns.
- **Plugin Browser & Custom Paths**: Search plugins by format or category, and configure custom search directories for VST3, CLAP, and LV2.

### 🔀 Parallel Signal Routing Canvas
- **Multi-Lane Canvas**: Interactive node graph supporting up to 5 parallel audio lanes (`Main`, `Path B1`, `Path B2`, `Path C1`, `Path C2`) with configurable slot capacity (6 to 12 slots).
- **Drag-and-Drop Workflow**: Drag `DRAG SPLIT` to create parallel branches, drag pedal blocks between slots and lanes, and drag split/merge handles to adjust routing positions.
- **Split Modes**: Configurable branch routing modes (Copy, A/B Split, Equal Power blend).
- **Visual Signal Flow**: Animated glowing connection wires showing active signal paths, zoom controls (wheel zoom, 100% reset), and missing plugin alerts.

### ☁️ TONE3000 Neural Model Integration
- **In-App Search**: Search and filter thousands of open-source neural amp and pedal captures on TONE3000.
- **One-Click Loading**: View model details, tags, and sample rates, and download models directly into NAM plugin nodes.

### 🎚️ Audio Engine & Display Compatibility
- **Real-Time Audio Engine**: Low-latency JACK Audio Connection Kit and PipeWire-JACK backend.
- **Display Server Support**: Runs natively on **both X11 (Xorg) and Wayland** (via Qt 6 XCB / Wayland and XWayland for native X11 plugin GUIs).
- **Live Monitoring**: Real-time master input/output peak meters with clipping warnings, DSP load indicator, and XRun dropout counter.
- **Branch Gain Controls**: Individual row volume, main mix level, panning, and polarity invert per branch.

### 💾 Preset Management
- **Pedalboard Presets**: Save, load, rename, and delete pedalboard configurations.
- **Top Bar Quick Selector**: Active preset bar with prev/next navigation arrows.
- **Parameter Synchronization**: Automatic parameter and preset state sync upon loading presets.

---

## 🛠️ Building from Source

### Prerequisites

#### **Ubuntu 22.04 / 24.04 & Debian 12**
```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
    qt6-base-dev qt6-base-private-dev \
    libjack-jackd2-dev liblilv-dev libsuil-dev libx11-dev
```

#### **Arch Linux / Manjaro / EndeavourOS**
```bash
sudo pacman -S --needed base-devel cmake pkgconf \
    qt6-base lilv suil jack2 libx11
```

#### **Fedora 38 / 39 / 40 / 41**
```bash
sudo dnf install -y gcc-c++ cmake pkgconfig \
    qt6-qtbase-devel lilv-devel suil-devel jack-audio-connection-kit-devel libX11-devel
```

#### **openSUSE Tumbleweed / Leap**
```bash
sudo zypper install -y gcc-c++ cmake pkg-config \
    libqt6-qtbase-devel lilv-devel suil-devel libjack-devel libX11-devel
```

### Compiling PedalBoard

```bash
# 1. Clone repository
git clone https://github.com/placeholder/PedalBoard.git
cd PedalBoard

# 2. Configure build with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build executable
cmake --build build -j$(nproc)

# 4. Run PedalBoard
./build/PedalBoard
```

---

## ⌨️ Keyboard Shortcuts & Canvas Controls

| Shortcut | Action |
| :--- | :--- |
| **Ctrl + N** | Create new empty preset |
| **Ctrl + S** | Save current preset |
| **Ctrl + Shift + S** | Save preset as... |
| **Ctrl + =** / **Ctrl + +** | Add slot column |
| **Ctrl + -** | Remove trailing empty slot column |
| **Mouse Wheel** / **Pinch** | Zoom canvas in / out |
| **Drag `SPLIT` Tool** | Drop onto gap to create parallel branch |
| **Drag Routing Handle** | Move split or merge position |

---

## ⚖️ License & Open Source Credits

PedalBoard is open-source software released under the **GNU General Public License v3.0 (GPLv3)**. See [LICENSE](LICENSE) for details.

### Third-Party Acknowledgments
- **Qt 6 Framework** ([LGPLv3](https://www.qt.io/)) — GUI & event loop
- **JACK Audio Connection Kit** ([LGPL](https://jackaudio.org/)) — Real-time audio engine
- **Lilv & Suil Libraries** ([ISC](https://drobilla.net/software/lilv)) — LV2 hosting & UI embedding
- **CLAP C API** ([MIT](https://clap.technology/)) — CLever Audio Plugin specification
- **Steinberg VST3 SDK** ([MIT](https://www.steinberg.net/)) — VST3 interface headers

*VST is a registered trademark of Steinberg Media Technologies GmbH.*
