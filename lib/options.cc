// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013, 2016 Henner Zeller <h.zeller@acm.org>
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

#include "led-matrix.h"

#include <stdio.h>
#include <string.h>

#include "framebuffer-internal.h"
#include "gpio.h"

namespace rgb_matrix {
RGBMatrix::Options::Options() :
  hardware_mapping("regular"),
  rows(32), cols(32), chain_length(1),
  pwm_bits(internal::Framebuffer::kDefaultBitPlanes),
  pwm_lsb_nanoseconds(130),
  pwm_dither_bits(0),
  brightness(100),
  scan_mode(0),
  disable_hardware_pulsing(false),
  show_refresh_rate(false),
  inverse_colors(false),
  led_rgb_sequence("RGB"),
  limit_refresh_rate_hz(0),
  disable_busy_waiting(false)
{
}

RuntimeOptions::RuntimeOptions() :
  gpio_slowdown(GPIO::IsPi4() ? 2 : 1)
{
}

bool RGBMatrix::Options::Validate(std::string *err_in) const {
  std::string scratch;
  std::string *err = err_in ? err_in : &scratch;
  bool success = true;
  if (internal::Framebuffer::FindHardwareMapping(hardware_mapping) == NULL) {
    err->append("Unknown hardware mapping '")
      .append(hardware_mapping)
      .append("'. Available: regular, adafruit-hat, adafruit-hat-pwm.\n");
    success = false;
  }

  if (rows < 8 || rows > 64 || rows % 2 != 0) {
    err->append("Invalid number of rows per panel. "
                "Should be in range of [8..64] and divisible by 2.\n");
    success = false;
  }

  if (cols < 16) {
    err->append("Invalid number of columns for panel. "
                "Typically that is something like 32 or 64\n");
    success = false;
  }

  if (chain_length < 1) {
    err->append("Chain-length outside usable range.\n");
    success = false;
  }

  if (brightness < 1 || brightness > 100) {
    err->append("Brightness outside usable range (Percent 1..100 allowed).\n");
    success = false;
  }

  if (pwm_bits <= 0 || pwm_bits > internal::Framebuffer::kBitPlanes) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "Invalid range of pwm-bits (1..%d allowed).\n",
             internal::Framebuffer::kBitPlanes);
    err->append(buffer);
    success = false;
  }

  if (scan_mode < 0 || scan_mode > 1) {
    err->append("Invalid scan mode (0 or 1 allowed).\n");
    success = false;
  }

  if (pwm_lsb_nanoseconds < 50 || pwm_lsb_nanoseconds > 3000) {
    err->append("Invalid range of pwm-lsb-nanoseconds (50..3000 allowed).\n");
    success = false;
  }

  if (pwm_dither_bits < 0 || pwm_dither_bits > 2) {
    err->append("Invalid range of pwm-dither-bits (0..2 allowed).\n");
    success = false;
  }

  if (led_rgb_sequence == NULL || strlen(led_rgb_sequence) != 3) {
    err->append("led-sequence needs to be three characters long.\n");
    success = false;
  } else {
    if ((!strchr(led_rgb_sequence, 'R') && !strchr(led_rgb_sequence, 'r'))
        || (!strchr(led_rgb_sequence, 'G') && !strchr(led_rgb_sequence, 'g'))
        || (!strchr(led_rgb_sequence, 'B') && !strchr(led_rgb_sequence, 'b'))) {
      err->append("led-sequence needs to contain all of letters 'R', 'G' "
                  "and 'B'\n");
      success = false;
    }
  }

  if (!success && !err_in) {
    // If we didn't get a string to write to, we write things to stderr.
    fprintf(stderr, "%s", err->c_str());
  }

  return success;
}

}  // namespace rgb_matrix
