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

#ifndef RPI_GPIO_INTERNAL_H
#define RPI_GPIO_INTERNAL_H

#include "gpio-bits.h"

#include <vector>

// "dsb st" is the store barrier that a gpio_slowdown of -1 emits after each
// GPIO write. Other architectures only build the library for the emulator.
#if defined(__aarch64__)
#define LED_MATRIX_ALLOW_BARRIER_DELAY 1
#else
#define LED_MATRIX_ALLOW_BARRIER_DELAY 0
#endif

// Putting this in our namespace to not collide with other things called like
// this.
namespace rgb_matrix {
// For now, everything is initialized as output.
class GPIO {
public:
  GPIO();

  // Initialize before use. Returns 'true' if successful, 'false' otherwise
  // (e.g. due to a permission problem or an unsupported board).
  bool Init(int slowdown);

  // Initialize outputs.
  // Returns the bits that were available and could be set for output.
  // (never use the optional adafruit_hack_needed parameter, it is used
  // internally to this library).
  gpio_bits_t InitOutputs(gpio_bits_t outputs,
                          bool adafruit_hack_needed = false);

  // Set the bits that are '1' in the output. Leave the rest untouched.
  inline void SetBits(gpio_bits_t value) {
    if (!value) return;
    *gpio_set_bits_ = value;
    delay();
  }

  // Clear the bits that are '1' in the output. Leave the rest untouched.
  inline void ClearBits(gpio_bits_t value) {
    if (!value) return;
    *gpio_clr_bits_ = value;
    delay();
  }

  // Write all the bits of "value" mentioned in "mask". Leave the rest untouched.
  inline void WriteMaskedBits(gpio_bits_t value, gpio_bits_t mask) {
    // Writing a word is two operations. The IO is actually pretty slow, so
    // this should probably  be unnoticable.
    *gpio_clr_bits_ = ~value & mask;
    *gpio_set_bits_ = value & mask;
    delay();
  }

  // Return if this appears to be a Pi 4-class board.
  static bool IsPi4();

private:
  inline void delay() const {
#if LED_MATRIX_ALLOW_BARRIER_DELAY
    if (slowdown_ == -1) {
        asm volatile("dsb\tst");
        return;
    }
#endif
    for (int n = 0; n < slowdown_; n++) {
      *gpio_clr_bits_ = 0;
    }
  }

  gpio_bits_t output_bits_;
  gpio_bits_t reserved_bits_;
  int slowdown_;

  volatile uint32_t *gpio_set_bits_;
  volatile uint32_t *gpio_clr_bits_;
};

// A PinPulser is a utility class that pulses a GPIO pin. There can be various
// implementations.
class PinPulser {
public:
  // Factory for a PinPulser. Chooses the right implementation depending
  // on the context (CPU and which pins are affected).
  // "gpio_mask" is the mask that should be output (since we only
  //   need negative pulses, this is what it does)
  // "nano_wait_spec" contains a list of time periods we'd like
  //   invoke later. This can be used to pre-process timings if needed.
  static PinPulser *Create(GPIO *io, gpio_bits_t gpio_mask,
                           bool allow_hardware_pulsing,
                           const std::vector<int> &nano_wait_spec);

  virtual ~PinPulser() {}

  // Send a pulse with a given length (index into nano_wait_spec array).
  virtual void SendPulse(int time_spec_number) = 0;

  // If SendPulse() is asynchronously implemented, wait for pulse to finish.
  virtual void WaitPulseFinished() {}
};

// Get rolling over microsecond counter. We get this from a hardware register
// if possible and a terrible slow fallback otherwise.
uint32_t GetMicrosecondCounter();

void SleepMicroseconds(long);

}  // end namespace rgb_matrix

#endif  // RPI_GPIO_INGERNALH
