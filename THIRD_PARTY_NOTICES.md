# Third-Party Notices

RigRoom includes the following third-party source dependencies.

## CLAP headers

Location: `src/audio/clap/`

Version: 1.2.10

Copyright (c) 2014-2022 Alexandre Bique

License: MIT. The complete license notice is retained in
`src/audio/clap/clap.h`.

## Steinberg VST3 pluginterfaces

Location: `src/audio/pluginterfaces/`

Source: https://github.com/steinbergmedia/vst3_pluginterfaces.git

Pinned revision: `31d6eeba6daaa3e2a8bfbe3e7a90ca0b7fbfbc1c`

License: MIT. The complete license text is retained in
`src/audio/pluginterfaces/LICENSE.txt`.

## Runtime dependencies

RigRoom dynamically links to Qt 6, JACK/PipeWire-JACK, Lilv, Suil, and X11.
Their licenses are provided by the operating system or runtime that supplies
those libraries.
