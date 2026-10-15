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

// The framebuffer is the workhorse: it represents the frame in some internal
// format that is friendly to be dumped to the matrix quickly. Provides methods
// to manipulate the content.

#include "framebuffer-internal.h"

#include <algorithm>
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "gpio.h"
#include "../include/graphics.h"

namespace rgb_matrix {
namespace internal {
// We need one global instance of a timing correct pulser. There are different
// implementations depending on the context.
static PinPulser *sOutputEnablePulser = NULL;

// Each panel row address drives two rows at once, one in each half.
static constexpr int kSubPanels = 2;

PixelDesignator *PixelDesignatorMap::get(int x, int y) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_)
    return NULL;
  return &buffer_[(y*width_) + x];
}

PixelDesignatorMap::PixelDesignatorMap(int width, int height,
                                       const PixelDesignator &fill_bits)
  : width_(width),
    height_(height),
    fill_bits_(fill_bits),
    buffer_(width * height) {
}

// Sets the row address in parallel output lines ABCDE with A the LSB and E
// the MSB.
class RowAddressSetter {
public:
  RowAddressSetter(int double_rows, const HardwareMapping &h)
    : row_mask_(0), last_row_(-1) {
    assert(double_rows <= 32);  // need to resize row_lookup_
    if (double_rows > 16) row_mask_ |= h.e;
    if (double_rows > 8)  row_mask_ |= h.d;
    if (double_rows > 4)  row_mask_ |= h.c;
    if (double_rows > 2)  row_mask_ |= h.b;
    row_mask_ |= h.a;
    for (int i = 0; i < double_rows; ++i) {
      // To avoid the bit-fiddle in the critical path, utilize
      // a lookup-table for all possible rows.
      gpio_bits_t row_address = (i & 0x01) ? h.a : 0;
      row_address |= (i & 0x02) ? h.b : 0;
      row_address |= (i & 0x04) ? h.c : 0;
      row_address |= (i & 0x08) ? h.d : 0;
      row_address |= (i & 0x10) ? h.e : 0;
      row_lookup_[i] = row_address;
    }
  }

  gpio_bits_t need_bits() const { return row_mask_; }

  void SetRowAddress(GPIO *io, int row) {
    if (row == last_row_) return;
    io->WriteMaskedBits(row_lookup_[row], row_mask_);
    last_row_ = row;
  }

private:
  gpio_bits_t row_mask_;
  gpio_bits_t row_lookup_[32];
  int last_row_;
};

const struct HardwareMapping *Framebuffer::hardware_mapping_ = NULL;
RowAddressSetter *Framebuffer::row_setter_ = NULL;

Framebuffer::Framebuffer(int rows, int columns, int scan_mode,
                         const char *led_sequence, bool inverse_color,
                         PixelDesignatorMap **mapper)
  : rows_(rows),
    columns_(columns),
    scan_mode_(scan_mode),
    inverse_color_(inverse_color),
    pwm_bits_(kBitPlanes), do_luminance_correct_(true), brightness_(100),
    double_rows_(rows / kSubPanels),
    shared_mapper_(mapper) {
  assert(hardware_mapping_ != NULL);   // Called InitHardwareMapping() ?
  assert(shared_mapper_ != NULL);  // Storage should be provided by RGBMatrix.
  assert(rows_ >=4 && rows_ <= 64 && rows_ % 2 == 0);

  bitplane_buffer_ = new gpio_bits_t[double_rows_ * columns_ * kBitPlanes];

  // The first Framebuffer builds the map from pixel (x,y) to framebuffer
  // position and color bits, which all later frames share.
  if (*shared_mapper_ == NULL) {
    // Gather all the bits for given color for fast Fill()s and use the right
    // bits according to the led sequence
    const struct HardwareMapping &h = *hardware_mapping_;
    gpio_bits_t r = h.p0_r1 | h.p0_r2;
    gpio_bits_t g = h.p0_g1 | h.p0_g2;
    gpio_bits_t b = h.p0_b1 | h.p0_b2;
    PixelDesignator fill_bits;
    fill_bits.r_bit = GetGpioFromLedSequence('R', led_sequence, r, g, b);
    fill_bits.g_bit = GetGpioFromLedSequence('G', led_sequence, r, g, b);
    fill_bits.b_bit = GetGpioFromLedSequence('B', led_sequence, r, g, b);

    *shared_mapper_ = new PixelDesignatorMap(columns_, rows_, fill_bits);
    for (int y = 0; y < rows_; ++y) {
      for (int x = 0; x < columns_; ++x) {
        InitDefaultDesignator(x, y, led_sequence, (*shared_mapper_)->get(x, y));
      }
    }
  }

  Clear();
}

Framebuffer::~Framebuffer() {
  delete [] bitplane_buffer_;
}

/* static */ const struct HardwareMapping *
Framebuffer::FindHardwareMapping(const char *name) {
  if (name == NULL || *name == '\0') name = "regular";
  for (HardwareMapping *it = matrix_hardware_mappings; it->name; ++it) {
    if (strcasecmp(it->name, name) == 0) return it;
  }
  return NULL;
}

/* static */ bool Framebuffer::InitHardwareMapping(const char *named_hardware) {
  hardware_mapping_ = FindHardwareMapping(named_hardware);
  return hardware_mapping_ != NULL;
}

/* static */ bool Framebuffer::InitGPIO(GPIO *io, int rows,
                                        bool allow_hardware_pulsing,
                                        int pwm_lsb_nanoseconds,
                                        int dither_bits) {
  if (sOutputEnablePulser != NULL)
    return true;  // already initialized.

  const struct HardwareMapping &h = *hardware_mapping_;
  // Tell GPIO about all bits we intend to use.
  gpio_bits_t all_used_bits = 0;

  all_used_bits |= h.output_enable | h.clock | h.strobe;
  all_used_bits |= h.p0_r1 | h.p0_g1 | h.p0_b1 | h.p0_r2 | h.p0_g2 | h.p0_b2;

  if (row_setter_ == NULL) {
    row_setter_ = new RowAddressSetter(rows / kSubPanels, h);
  }
  all_used_bits |= row_setter_->need_bits();

  // Adafruit HAT identified by the same prefix.
  const bool is_some_adafruit_hat = (0 == strncmp(h.name, "adafruit-hat",
                                                  strlen("adafruit-hat")));
  // Initialize outputs, make sure that all of these are supported bits.
  const gpio_bits_t result = io->InitOutputs(all_used_bits,
                                             is_some_adafruit_hat);
  if (result != all_used_bits) {
    fprintf(stderr, "GPIO pins %#x could not be claimed for output.\n",
            all_used_bits & ~result);
  }

  std::vector<int> bitplane_timings;
  uint32_t timing_ns = pwm_lsb_nanoseconds;
  for (int b = 0; b < kBitPlanes; ++b) {
    bitplane_timings.push_back(timing_ns);
    if (b >= dither_bits) timing_ns *= 2;
  }
  sOutputEnablePulser = PinPulser::Create(io, h.output_enable,
                                          allow_hardware_pulsing,
                                          bitplane_timings);
  return sOutputEnablePulser != NULL;
}

bool Framebuffer::SetPWMBits(uint8_t value) {
  if (value < 1 || value > kBitPlanes)
    return false;
  pwm_bits_ = value;
  return true;
}

inline gpio_bits_t *Framebuffer::ValueAt(int double_row, int column, int bit) {
  return &bitplane_buffer_[ double_row * (columns_ * kBitPlanes)
                            + bit * columns_
                            + column ];
}

void Framebuffer::Clear() {
  if (inverse_color_) {
    Fill(0, 0, 0);
  } else  {
    // Cheaper.
    memset(bitplane_buffer_, 0,
           sizeof(*bitplane_buffer_) * double_rows_ * columns_ * kBitPlanes);
  }
}

struct ColorLookup {
  uint16_t color[256];
};

class ColorLookupTable {
  public:
    static const ColorLookup &GetLookup(uint8_t brightness) {
      static ColorLookupTable instance;
      return instance.lookups_[brightness - 1];
    }


  private:
    // Do CIE1931 luminance correction and scale to output bitplanes
    static uint16_t luminance_cie1931(uint8_t c, uint8_t brightness) {
      float out_factor = ((1 << internal::Framebuffer::kBitPlanes) - 1);
      float v = (float) c * brightness / 255.0;
      return roundf(out_factor * ((v <= 8) ? v / 902.3 : pow((v + 16) / 116.0, 3)));
    }

    ColorLookupTable() {
      for (int c = 0; c < 256; ++c)
        for (int b = 0; b < 100; ++b)
          lookups_[b].color[c] = luminance_cie1931(c, b + 1);
    }

    ColorLookup lookups_[100]{};
  };

  static inline uint16_t CIEMapColor(uint8_t brightness, uint8_t c) {
    return ColorLookupTable::GetLookup(brightness).color[c];
  }

// Non luminance correction. TODO: consider getting rid of this.
static inline uint16_t DirectMapColor(uint8_t brightness, uint8_t c) {
  // simple scale down the color value
  c = c * brightness / 100;

  // shift to be left aligned with top-most bits.
  constexpr int shift = internal::Framebuffer::kBitPlanes - 8;
  return (shift > 0) ? (c << shift) : (c >> -shift);
}

inline void Framebuffer::MapColors(
  uint8_t r, uint8_t g, uint8_t b,
  uint16_t *red, uint16_t *green, uint16_t *blue) {

  if (do_luminance_correct_) {
    *red   = CIEMapColor(brightness_, r);
    *green = CIEMapColor(brightness_, g);
    *blue  = CIEMapColor(brightness_, b);
  } else {
    *red   = DirectMapColor(brightness_, r);
    *green = DirectMapColor(brightness_, g);
    *blue  = DirectMapColor(brightness_, b);
  }

  if (inverse_color_) {
    *red = ~(*red);
    *green = ~(*green);
    *blue = ~(*blue);
  }
}

void Framebuffer::Fill(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t red, green, blue;
  MapColors(r, g, b, &red, &green, &blue);
  const PixelDesignator &fill = (*shared_mapper_)->GetFillColorBits();

  for (int bits = kBitPlanes - pwm_bits_; bits < kBitPlanes; ++bits) {
    uint16_t mask = 1 << bits;
    gpio_bits_t plane_bits = 0;
    plane_bits |= ((red & mask) == mask)   ? fill.r_bit : 0;
    plane_bits |= ((green & mask) == mask) ? fill.g_bit : 0;
    plane_bits |= ((blue & mask) == mask)  ? fill.b_bit : 0;

    for (int row = 0; row < double_rows_; ++row) {
      gpio_bits_t *row_data = ValueAt(row, 0, bits);
      for (int col = 0; col < columns_; ++col) {
        *row_data++ = plane_bits;
      }
    }
  }
}

int Framebuffer::width() const { return (*shared_mapper_)->width(); }
int Framebuffer::height() const { return (*shared_mapper_)->height(); }

void Framebuffer::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  const PixelDesignator *designator = (*shared_mapper_)->get(x, y);
  if (designator == NULL) return;
  const long pos = designator->gpio_word;
  if (pos < 0) return;  // non-used pixel marker.

  uint16_t red, green, blue;
  MapColors(r, g, b, &red, &green, &blue);

  gpio_bits_t *bits = bitplane_buffer_ + pos;
  const int min_bit_plane = kBitPlanes - pwm_bits_;
  bits += (columns_ * min_bit_plane);
  const gpio_bits_t r_bits = designator->r_bit;
  const gpio_bits_t g_bits = designator->g_bit;
  const gpio_bits_t b_bits = designator->b_bit;
  const gpio_bits_t designator_mask = designator->mask;
  for (uint16_t mask = 1<<min_bit_plane; mask != 1<<kBitPlanes; mask <<=1 ) {
    gpio_bits_t color_bits = 0;
    if (red & mask)   color_bits |= r_bits;
    if (green & mask) color_bits |= g_bits;
    if (blue & mask)  color_bits |= b_bits;
    *bits = (*bits & designator_mask) | color_bits;
    bits += columns_;
  }
}

// Strange LED-mappings such as RBG or so are handled here.
gpio_bits_t Framebuffer::GetGpioFromLedSequence(char col,
                                                const char *led_sequence,
                                                gpio_bits_t default_r,
                                                gpio_bits_t default_g,
                                                gpio_bits_t default_b) {
  const char *pos = strchr(led_sequence, col);
  if (pos == NULL) pos = strchr(led_sequence, tolower(col));
  if (pos == NULL) {
    fprintf(stderr, "LED sequence '%s' does not contain any '%c'.\n",
            led_sequence, col);
    abort();
  }
  switch (pos - led_sequence) {
  case 0: return default_r;
  case 1: return default_g;
  case 2: return default_b;
  }
  return default_r;  // String too long, should've been caught earlier.
}

void Framebuffer::InitDefaultDesignator(int x, int y, const char *seq,
                                        PixelDesignator *d) {
  const struct HardwareMapping &h = *hardware_mapping_;
  gpio_bits_t *bits = ValueAt(y % double_rows_, x, 0);
  d->gpio_word = bits - bitplane_buffer_;
  if (y < double_rows_) {
    d->r_bit = GetGpioFromLedSequence('R', seq, h.p0_r1, h.p0_g1, h.p0_b1);
    d->g_bit = GetGpioFromLedSequence('G', seq, h.p0_r1, h.p0_g1, h.p0_b1);
    d->b_bit = GetGpioFromLedSequence('B', seq, h.p0_r1, h.p0_g1, h.p0_b1);
  } else {
    d->r_bit = GetGpioFromLedSequence('R', seq, h.p0_r2, h.p0_g2, h.p0_b2);
    d->g_bit = GetGpioFromLedSequence('G', seq, h.p0_r2, h.p0_g2, h.p0_b2);
    d->b_bit = GetGpioFromLedSequence('B', seq, h.p0_r2, h.p0_g2, h.p0_b2);
  }

  d->mask = ~(d->r_bit | d->g_bit | d->b_bit);
}

void Framebuffer::DumpToMatrix(GPIO *io, int pwm_low_bit) {
  const struct HardwareMapping &h = *hardware_mapping_;
  gpio_bits_t color_clk_mask = 0;  // Mask of bits while clocking in.
  color_clk_mask |= h.p0_r1 | h.p0_g1 | h.p0_b1 | h.p0_r2 | h.p0_g2 | h.p0_b2;
  color_clk_mask |= h.clock;

  // Depending if we do dithering, we might not always show the lowest bits.
  const int start_bit = std::max(pwm_low_bit, kBitPlanes - pwm_bits_);

  const uint8_t half_double = double_rows_/2;
  for (uint8_t row_loop = 0; row_loop < double_rows_; ++row_loop) {
    uint8_t d_row;
    switch (scan_mode_) {
    case 0:  // progressive
    default:
      d_row = row_loop;
      break;

    case 1:  // interlaced
      d_row = ((row_loop < half_double)
               ? (row_loop << 1)
               : ((row_loop - half_double) << 1) + 1);
    }

    // Rows can't be switched very quickly without ghosting, so we do the
    // full PWM of one row before switching rows.
    for (int b = start_bit; b < kBitPlanes; ++b) {
      gpio_bits_t *row_data = ValueAt(d_row, 0, b);
      // While the output enable is still on, we can already clock in the next
      // data.
      for (int col = 0; col < columns_; ++col) {
        const gpio_bits_t &out = *row_data++;
        io->WriteMaskedBits(out, color_clk_mask);  // col + reset clock
        io->SetBits(h.clock);               // Rising edge: clock color in.
      }
      io->ClearBits(color_clk_mask);    // clock back to normal.

      // OE of the previous row-data must be finished before strobe.
      sOutputEnablePulser->WaitPulseFinished();

      // Setting address and strobing needs to happen in dark time.
      row_setter_->SetRowAddress(io, d_row);

      io->SetBits(h.strobe);   // Strobe in the previously clocked in row.
      io->ClearBits(h.strobe);

      // Now switch on for the sleep time necessary for that bit-plane.
      sOutputEnablePulser->SendPulse(b);
    }
  }
}
}  // namespace internal
}  // namespace rgb_matrix
