# rgbmatrix (PlaneSign fork)

A trimmed fork of [hzeller/rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix)
that keeps only the C++ panel driver and its Python bindings, as used by
[PlaneSign](https://github.com/dmod/PlaneSign).

Differences from upstream:

- Raspberry Pi 2 (v1.2), 3, 4 and Zero 2 only: the boards that run a 64-bit
  OS. A Pi 1, Zero or Pi 5 reports that it is unsupported instead of starting.
- One chain of panels with direct ABCDE row addressing. Parallel chains,
  pixel mappers, multiplexed (outdoor) panels, shift-register row addressing
  and FM6126/FM6127 panel initialization were removed.
- Hardware mappings: `regular`, `adafruit-hat` and `adafruit-hat-pwm`.
- `SetImage()` reads pixels with Pillow's public `Image.tobytes()`, so building
  needs no Pillow headers and works with any Pillow version.
- A failed initialization raises `RuntimeError` instead of crashing Python,
  and `SwapOnVSync()`, `Clear()`, `Fill()` and `SetImage()` release the GIL.
- Removed: the C API, content streamer, frame sequences, examples, utilities,
  fonts, documentation, command-line flag parsing, GPIO input/output, daemon
  mode, privilege dropping and the non-Python bindings.

Install with `pip install .` (needs a C++ compiler, CMake and the Python
development headers). Licensed under GPL-2.0-or-later; see `COPYING`.
