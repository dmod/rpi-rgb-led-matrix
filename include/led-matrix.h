// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

// Controlling a chain of RGB LED matrix panels via GPIO.

#ifndef RPI_RGBMATRIX_H
#define RPI_RGBMATRIX_H

#include <stdint.h>
#include <stddef.h>

#include <string>

#include "canvas.h"

namespace rgb_matrix {
class RGBMatrix;
class FrameCanvas;   // Canvas for Double- and Multibuffering
struct RuntimeOptions;

// The RGB matrix provides the framebuffer and the facilities to constantly
// update the LED matrix.
//
// This implement the Canvas interface that represents the display with
// (cols * chain_length) x rows pixels.
//
// If can do multi-buffering using the CreateFrameCanvas() and SwapOnVSync()
// methods. This is useful for animations and to prevent tearing.
class RGBMatrix : public Canvas {
public:
  // Options to initialize the RGBMatrix.
  struct Options {
    Options();   // Creates a default option set.

    // Validate the options and possibly output a message to string. If
    // "err" is NULL, outputs validation problems to stderr.
    // Returns 'true' if all options look good.
    bool Validate(std::string *err) const;

    // Name of the hardware mapping: "regular", "adafruit-hat" or
    // "adafruit-hat-pwm". NULL or "" means "regular".
    const char *hardware_mapping;

    // The "rows" are the number
    // of rows supported by the display, so 32 or 16. Default: 32.
    int rows;

    // The "cols" are the number of columns per panel. Typically something
    // like 32, but also 64 is possible. Sometimes even 40.
    // cols * chain_length is the total length of the display, so you can
    // represent a 64 wide display as cols=32, chain=2 or cols=64, chain=1;
    // same thing, but more convenient to think of.
    int cols;

    // The chain_length is the number of displays daisy-chained together
    // (output of one connected to input of next). Default: 1
    int chain_length;

    // Set PWM bits used for output. Default is 11, but if you only deal with
    // limited comic-colors, 1 might be sufficient. Lower require less CPU and
    // increases refresh-rate.
    int pwm_bits;

    // Change the base time-unit for the on-time in the lowest
    // significant bit in nanoseconds.
    // Higher numbers provide better quality (more accurate color, less
    // ghosting), but have a negative impact on the frame rate.
    int pwm_lsb_nanoseconds;

    // The lower bits can be time-dithered for higher refresh rate.
    int pwm_dither_bits;

    // The initial brightness of the panel in percent. Valid range is 1..100
    // Default: 100
    int brightness;

    // Scan mode: 0=progressive, 1=interlaced.
    int scan_mode;

    // Disable the PWM hardware subsystem to create pulses.
    // Typically, you don't want to disable hardware pulsing, this is mostly
    // for debugging and figuring out if there is interference with the
    // sound system.
    // This won't do anything if output enable is not connected to GPIO 18 or
    // 12, as on the default adafruit-hat mapping.
    bool disable_hardware_pulsing;

    // Show refresh rate on the terminal for debugging and tweaking purposes.
    bool show_refresh_rate;

    // Some panels have inversed colors.
    bool inverse_colors;

    // In case the internal sequence of mapping is not "RGB", this contains the
    // real mapping. Some panels mix up these colors. String of length three
    // which has to contain all characters R, G and B.
    const char *led_rgb_sequence;

    // Limit refresh rate of LED panel. This will help on a loaded system
    // to keep a constant refresh rate. <= 0 for no limit.
    int limit_refresh_rate_hz;

    // Sleep instead of busy wait to free CPU cycles but get slightly less
    // accurate frame timing.
    bool disable_busy_waiting;
  };

  // Factory to create a matrix and start its refresh thread.
  // Returns NULL, if there was a problem (a message then is written to stderr).
  static RGBMatrix *CreateFromOptions(const Options &options,
                                      const RuntimeOptions &runtime_options);

  // Stop matrix, delete all resources.
  virtual ~RGBMatrix();

  // -- Canvas interface. These write to the active FrameCanvas
  // (see documentation in canvas.h)
  //
  // Since this is updating the canvas that is currently displayed, this
  // might result in tearing.
  // Prefer using a FrameCanvas and do double-buffering, see section below.
  virtual int width() const;
  virtual int height() const;
  virtual void SetPixel(int x, int y,
                        uint8_t red, uint8_t green, uint8_t blue);
  virtual void Clear();
  virtual void Fill(uint8_t red, uint8_t green, uint8_t blue);

  // -- Double- and Multibuffering.

  // Create a new buffer to be used for multi-buffering. The returned new
  // Buffer implements a Canvas with the same size as this RGBMatrix.
  // You can use it to draw off-screen on it, then swap it with the active
  // buffer using SwapOnVSync(). That would be classic double-buffering.
  //
  // The ownership of the created Canvases remains with the RGBMatrix, so you
  // don't have to worry about deleting them (but you also don't want to create
  // more than needed as this will fill up your memory as they are only deleted
  // when the RGBMatrix is deleted).
  FrameCanvas *CreateFrameCanvas();

  // This method waits to the next VSync and swaps the active buffer with the
  // supplied buffer. The formerly active buffer is returned.
  //
  // If you pass in NULL, the active buffer is returned, but it won't be
  // replaced with NULL. You can use the NULL-behavior to just wait on
  // VSync or to retrieve the initial buffer when preparing a multi-buffer
  // animation.
  //
  // The optional "framerate_fraction" parameter allows to choose which
  // multiple of the global frame-count to use. So it slows down your animation
  // to an exact integer fraction of the refresh rate.
  // Default is 1, so immediately next available frame.
  // (Say you have 140Hz refresh rate, then a value of 5 would give you an
  // 28Hz animation, nicely locked to the refresh-rate).
  // If you combine this with Options::limit_refresh_rate_hz you can create
  // time-correct animations.
  FrameCanvas *SwapOnVSync(FrameCanvas *other, unsigned framerate_fraction = 1);

  // -- Setting shape and behavior of matrix.

  // Set PWM bits used for output. Default is 11, but if you only deal with
  // limited comic-colors, 1 might be sufficient. Lower require less CPU and
  // increases refresh-rate.
  //
  // Returns boolean to signify if value was within range.
  //
  // This sets the PWM bits for the current active FrameCanvas and future
  // ones that are created with CreateFrameCanvas().
  bool SetPWMBits(uint8_t value);
  uint8_t pwmbits();   // return the pwm-bits of the currently active buffer.

  // Map brightness of output linearly to input with CIE1931 profile.
  void set_luminance_correct(bool on);
  bool luminance_correct() const;

  // Set brightness in percent for all created FrameCanvas. 1%..100%.
  // This will only affect newly set pixels.
  void SetBrightness(uint8_t brightness);
  uint8_t brightness();

private:
  class Impl;

  RGBMatrix(Impl *impl) : impl_(impl) {}
  Impl *const impl_;
};

namespace internal {
class Framebuffer;
}

class FrameCanvas : public Canvas {
public:
  // Set PWM bits used for this Frame.
  // Simple comic-colors, 1 might be sufficient (111 RGB, i.e. 8 colors).
  // Lower require less CPU.
  // Returns boolean to signify if value was within range.
  bool SetPWMBits(uint8_t value);
  uint8_t pwmbits();

  // Map brightness of output linearly to input with CIE1931 profile.
  void set_luminance_correct(bool on);
  bool luminance_correct() const;

  void SetBrightness(uint8_t brightness);
  uint8_t brightness();

  // -- Canvas interface.
  virtual int width() const;
  virtual int height() const;
  virtual void SetPixel(int x, int y,
                        uint8_t red, uint8_t green, uint8_t blue);
  virtual void Clear();
  virtual void Fill(uint8_t red, uint8_t green, uint8_t blue);

private:
  friend class RGBMatrix;

  FrameCanvas(internal::Framebuffer *frame) : frame_(frame){}
  virtual ~FrameCanvas();   // Any FrameCanvas is owned by RGBMatrix.
  internal::Framebuffer *framebuffer() { return frame_; }

  internal::Framebuffer *const frame_;
};

// Options that affect how the GPIO is driven rather than the panel itself.
struct RuntimeOptions {
  RuntimeOptions();

  // Extra writes after each GPIO write, for panels that can't keep up with
  // a fast Pi. 0 = none; -1 emits a store barrier instead (arm64 only).
  int gpio_slowdown;
};

// Same as RGBMatrix::CreateFromOptions(), as a free function for bindings.
inline RGBMatrix *CreateMatrixFromOptions(
  const RGBMatrix::Options &options,
  const RuntimeOptions &runtime_options) {
  return RGBMatrix::CreateFromOptions(options, runtime_options);
}

}  // end namespace rgb_matrix
#endif  // RPI_RGBMATRIX_H
