# Live Captions powered by Parakeet

A small native GNOME Shell extension and GTK4 application for on-device live
captions using NVIDIA Nemotron 3.5 ASR Streaming 0.6B through parakeet.cpp.

The project provides one Quick Settings pill, a compact floating caption
window, language and audio input selectors, and no desktop launcher or top bar
indicator. Captions run locally and the audio is not sent to a server.

## Status

This repository is published for reuse and experimentation. The original
author does not provide ongoing maintenance or support. Contributions and
forks are welcome.

## Requirements

- GNOME Shell 46
- GTK4 and libadwaita
- PulseAudio compatibility (`libpulse-simple`)
- Meson, Ninja, CMake and a C/C++ toolchain
- The Parakeet streaming model, downloaded separately

## Build and install

Clone with submodules, then build with a user prefix:

```sh
git clone --recursive <repository-url>
cd LiveCaptions
meson setup builddir --buildtype=release --prefix="$HOME/.local"
meson compile -C builddir
meson install -C builddir
```

Download `nemotron-3.5-asr-streaming-0.6b-q4_k.gguf` from the parakeet.cpp
GGUF model collection and place it at:

```text
~/.local/share/net.sapples.LiveCaptions/models/nemotron-3.5-asr-streaming-0.6b-q4_k.gguf
```

The model path can be changed with `PARAKEET_MODEL_PATH`.

Enable `livecaptions@local` in GNOME Extensions. The application starts on
demand when the Quick Settings pill is activated.

## Features

- Parakeet/Nemotron streaming transcription only
- Automatic language detection and 40+ locale prompts
- Desktop audio monitor or microphone input
- Rolling two-line captions with automatic line movement
- Light and dark GTK themes
- Draggable compact floating window
- Settings page for the default caption language

## License

The application follows the GPL license included in `COPYING`. The bundled
parakeet.cpp subproject and the model have their own licenses; review their
license files and model terms before redistributing binaries or model files.
