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

#include "led-matrix.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "gpio.h"
#include "thread.h"
#include "framebuffer-internal.h"

namespace rgb_matrix {
// Implementation details of RGBmatrix.
class RGBMatrix::Impl {
  class UpdateThread;
  friend class UpdateThread;

public:
  // The options have to be valid (see Options::Validate()).
  explicit Impl(const Options &options);

  ~Impl();

  // Claims the GPIO pins and starts the refresh thread.
  // Returns false if the output-enable pulser can't be set up.
  bool StartRefresh(GPIO *io);

  FrameCanvas *CreateFrameCanvas();
  FrameCanvas *SwapOnVSync(FrameCanvas *other, unsigned framerate_fraction);

  bool SetPWMBits(uint8_t value);
  uint8_t pwmbits();   // return the pwm-bits of the currently active buffer.

  void set_luminance_correct(bool on);
  bool luminance_correct() const;

  // Set brightness in percent for all created FrameCanvas. 1%..100%.
  // This will only affect newly set pixels.
  void SetBrightness(uint8_t brightness);
  uint8_t brightness();

private:
  friend class RGBMatrix;

  Options params_;
  bool do_luminance_correct_;

  FrameCanvas *active_;

  GPIO *io_;
  UpdateThread *updater_;
  std::vector<FrameCanvas*> created_frames_;
  internal::PixelDesignatorMap *shared_pixel_mapper_;
};

using namespace internal;

// Pump pixels to screen. Needs to be high priority real-time because jitter
class RGBMatrix::Impl::UpdateThread : public Thread {
public:
  UpdateThread(GPIO *io, FrameCanvas *initial_frame,
               int pwm_dither_bits, bool show_refresh,
               int limit_refresh_hz, bool allow_busy_waiting)
    : io_(io), show_refresh_(show_refresh),
      target_frame_usec_(limit_refresh_hz < 1 ? 0 : 1e6/limit_refresh_hz),
      allow_busy_waiting_(allow_busy_waiting),
      running_(true),
      current_frame_(initial_frame), next_frame_(NULL),
      requested_frame_multiple_(1) {
    pthread_cond_init(&frame_done_, NULL);
    switch (pwm_dither_bits) {
    case 0:
      start_bit_[0] = 0; start_bit_[1] = 0;
      start_bit_[2] = 0; start_bit_[3] = 0;
      break;
    case 1:
      start_bit_[0] = 0; start_bit_[1] = 1;
      start_bit_[2] = 0; start_bit_[3] = 1;
      break;
    case 2:
      start_bit_[0] = 0; start_bit_[1] = 1;
      start_bit_[2] = 2; start_bit_[3] = 2;
      break;
    }
  }

  void Stop() {
    MutexLock l(&running_mutex_);
    running_ = false;
  }

  virtual void Run() {
    unsigned frame_count = 0;
    unsigned low_bit_sequence = 0;
    uint32_t largest_time = 0;

    // Let's start measure max time only after a we were running for a few
    // seconds to not pick up start-up glitches.
    static const int kHoldffTimeUs = 2000 * 1000;
    uint32_t initial_holdoff_start = GetMicrosecondCounter();
    bool max_measure_enabled = false;

    while (running()) {
      const uint32_t start_time_us = GetMicrosecondCounter();

      current_frame_->framebuffer()
        ->DumpToMatrix(io_, start_bit_[low_bit_sequence % 4]);

      // SwapOnVSync() exchange.
      {
        MutexLock l(&frame_sync_);
        // Do fast equality test first (likely due to frame_count reset).
        if (frame_count == requested_frame_multiple_
            || frame_count % requested_frame_multiple_ == 0) {
          // We reset to avoid frame hick-up every couple of weeks
          // run-time iff requested_frame_multiple_ is not a factor of 2^32.
          frame_count = 0;
          if (next_frame_ != NULL) {
            current_frame_ = next_frame_;
            next_frame_ = NULL;
          }
          pthread_cond_signal(&frame_done_);
        }
      }

      ++frame_count;
      ++low_bit_sequence;

      if (target_frame_usec_) {
        if (allow_busy_waiting_) {
          while ((GetMicrosecondCounter() - start_time_us) < target_frame_usec_) {
            // busy wait. We have our dedicated core, so ok to burn cycles.
          }
        } else {
          long spent_us = GetMicrosecondCounter() - start_time_us;
          SleepMicroseconds(target_frame_usec_ - spent_us);
        }
      }

      const uint32_t end_time_us = GetMicrosecondCounter();
      if (show_refresh_) {
        uint32_t usec = end_time_us - start_time_us;
        printf("\b\b\b\b\b\b\b\b%6.1fHz", 1e6 / usec);
        if (usec > largest_time && max_measure_enabled) {
          largest_time = usec;
          const float lowest_hz = 1e6 / largest_time;
          printf(" (lowest: %.1fHz)"
                 "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", lowest_hz);
        } else {
          // Don't measure at startup, as times will be janky.
          max_measure_enabled = (end_time_us - initial_holdoff_start) > kHoldffTimeUs;
        }
      }
    }
  }

  FrameCanvas *SwapOnVSync(FrameCanvas *other, unsigned frame_fraction) {
    MutexLock l(&frame_sync_);
    FrameCanvas *previous = current_frame_;
    next_frame_ = other;
    requested_frame_multiple_ = frame_fraction;
    frame_sync_.WaitOn(&frame_done_);
    return previous;
  }

private:
  inline bool running() {
    MutexLock l(&running_mutex_);
    return running_;
  }

  GPIO *const io_;
  const bool show_refresh_;
  const uint32_t target_frame_usec_;
  const bool allow_busy_waiting_;
  uint32_t start_bit_[4];

  Mutex running_mutex_;
  bool running_;

  Mutex frame_sync_;
  pthread_cond_t frame_done_;
  FrameCanvas *current_frame_;
  FrameCanvas *next_frame_;
  unsigned requested_frame_multiple_;
};

RGBMatrix::Impl::Impl(const Options &options)
  : params_(options), do_luminance_correct_(true), active_(NULL), io_(NULL),
    updater_(NULL), shared_pixel_mapper_(NULL) {
  assert(params_.Validate(NULL));
  Framebuffer::InitHardwareMapping(params_.hardware_mapping);
  active_ = CreateFrameCanvas();
  active_->Clear();
}

RGBMatrix::Impl::~Impl() {
  if (updater_) {
    updater_->Stop();
    updater_->WaitStopped();
  }
  delete updater_;

  // Make sure LEDs are off.
  active_->Clear();
  if (io_) active_->framebuffer()->DumpToMatrix(io_, 0);

  for (size_t i = 0; i < created_frames_.size(); ++i) {
    delete created_frames_[i];
  }
  delete shared_pixel_mapper_;
}

RGBMatrix::~RGBMatrix() {
  delete impl_;
}

bool RGBMatrix::Impl::StartRefresh(GPIO *io) {
  if (!Framebuffer::InitGPIO(io, params_.rows,
                             !params_.disable_hardware_pulsing,
                             params_.pwm_lsb_nanoseconds,
                             params_.pwm_dither_bits)) {
    return false;
  }
  io_ = io;
  updater_ = new UpdateThread(io_, active_, params_.pwm_dither_bits,
                              params_.show_refresh_rate,
                              params_.limit_refresh_rate_hz,
                              !params_.disable_busy_waiting);
  // The kernel would otherwise move the thread between cores, creating some
  // global flicker, so tie it to the last of the four cores.
  updater_->Start(99, (1<<3));  // Prio: high. Also: put on last CPU.
  return true;
}

FrameCanvas *RGBMatrix::Impl::CreateFrameCanvas() {
  FrameCanvas *result =
    new FrameCanvas(new Framebuffer(params_.rows,
                                    params_.cols * params_.chain_length,
                                    params_.scan_mode,
                                    params_.led_rgb_sequence,
                                    params_.inverse_colors,
                                    &shared_pixel_mapper_));
  if (created_frames_.empty()) {
    // First time. Get defaults from initial Framebuffer.
    do_luminance_correct_ = result->framebuffer()->luminance_correct();
  }

  result->framebuffer()->SetPWMBits(params_.pwm_bits);
  result->framebuffer()->set_luminance_correct(do_luminance_correct_);
  result->framebuffer()->SetBrightness(params_.brightness);

  created_frames_.push_back(result);

  if (created_frames_.size() % 500 == 0) {
    fprintf(stderr, "CreateFrameCanvas() called %d times; these frames are "
            "only freed with the matrix. Create the off-screen canvas once "
            "and reuse it with SwapOnVSync().\n",
            (int)created_frames_.size());
  }

  return result;
}

FrameCanvas *RGBMatrix::Impl::SwapOnVSync(FrameCanvas *other,
                                          unsigned frame_fraction) {
  if (frame_fraction == 0) frame_fraction = 1; // correct user error.
  if (!updater_) return NULL;
  FrameCanvas *const previous = updater_->SwapOnVSync(other, frame_fraction);
  if (other) active_ = other;
  return previous;
}

bool RGBMatrix::Impl::SetPWMBits(uint8_t value) {
  const bool success = active_->framebuffer()->SetPWMBits(value);
  if (success) {
    params_.pwm_bits = value;
  }
  return success;
}
uint8_t RGBMatrix::Impl::pwmbits() { return params_.pwm_bits; }

// Map brightness of output linearly to input with CIE1931 profile.
void RGBMatrix::Impl::set_luminance_correct(bool on) {
  active_->framebuffer()->set_luminance_correct(on);
  do_luminance_correct_ = on;
}
bool RGBMatrix::Impl::luminance_correct() const {
  return do_luminance_correct_;
}

void RGBMatrix::Impl::SetBrightness(uint8_t brightness) {
  for (size_t i = 0; i < created_frames_.size(); ++i) {
    created_frames_[i]->framebuffer()->SetBrightness(brightness);
  }
  params_.brightness = brightness;
}

uint8_t RGBMatrix::Impl::brightness() {
  return params_.brightness;
}

// -- Public interface of RGBMatrix. Delegate everything to impl_

RGBMatrix *RGBMatrix::CreateFromOptions(const RGBMatrix::Options &options,
                                        const RuntimeOptions &runtime_options) {
  std::string error;
  if (!options.Validate(&error)) {
    fprintf(stderr, "%s", error.c_str());
    return NULL;
  }

  // For the Pi4, we might need 2, maybe up to 4. Let's open up to 5.
  // on supported architectures, -1 will emit memory barier (DSB ST) after GPIO write
  if (runtime_options.gpio_slowdown < (LED_MATRIX_ALLOW_BARRIER_DELAY ? -1 : 0)
      || runtime_options.gpio_slowdown > 10) {
    fprintf(stderr, "gpio_slowdown=%d is outside usable range\n",
            runtime_options.gpio_slowdown);
    return NULL;
  }

  static GPIO io;
  if (!io.Init(runtime_options.gpio_slowdown)) {
    fprintf(stderr, "Could not access the GPIO registers. This needs root "
            "(for /dev/mem) on a Raspberry Pi 2, 3 or 4.\n");
    return NULL;
  }

  RGBMatrix::Impl *result = new RGBMatrix::Impl(options);
  if (!result->StartRefresh(&io)) {
    delete result;
    return NULL;
  }
  return new RGBMatrix(result);
}

FrameCanvas *RGBMatrix::CreateFrameCanvas() {
  return impl_->CreateFrameCanvas();
}
FrameCanvas *RGBMatrix::SwapOnVSync(FrameCanvas *other,
                                    unsigned framerate_fraction) {
  return impl_->SwapOnVSync(other, framerate_fraction);
}
bool RGBMatrix::SetPWMBits(uint8_t value) { return impl_->SetPWMBits(value); }
uint8_t RGBMatrix::pwmbits() { return impl_->pwmbits(); }

void RGBMatrix::set_luminance_correct(bool on) {
  return impl_->set_luminance_correct(on);
}
bool RGBMatrix::luminance_correct() const { return impl_->luminance_correct(); }

void RGBMatrix::SetBrightness(uint8_t brightness) {
  impl_->SetBrightness(brightness);
}
uint8_t RGBMatrix::brightness() { return impl_->brightness(); }

// -- Implementation of RGBMatrix Canvas: delegation to ContentBuffer
int RGBMatrix::width() const {
  return impl_->active_->width();
}

int RGBMatrix::height() const {
  return impl_->active_->height();
}

void RGBMatrix::SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) {
  impl_->active_->SetPixel(x, y, red, green, blue);
}

void RGBMatrix::Clear() {
  impl_->active_->Clear();
}

void RGBMatrix::Fill(uint8_t red, uint8_t green, uint8_t blue) {
  impl_->active_->Fill(red, green, blue);
}

// FrameCanvas implementation of Canvas
FrameCanvas::~FrameCanvas() { delete frame_; }
int FrameCanvas::width() const { return frame_->width(); }
int FrameCanvas::height() const { return frame_->height(); }
void FrameCanvas::SetPixel(int x, int y,
                         uint8_t red, uint8_t green, uint8_t blue) {
  frame_->SetPixel(x, y, red, green, blue);
}
void FrameCanvas::Clear() { return frame_->Clear(); }
void FrameCanvas::Fill(uint8_t red, uint8_t green, uint8_t blue) {
  frame_->Fill(red, green, blue);
}
bool FrameCanvas::SetPWMBits(uint8_t value) { return frame_->SetPWMBits(value); }
uint8_t FrameCanvas::pwmbits() { return frame_->pwmbits(); }

// Map brightness of output linearly to input with CIE1931 profile.
void FrameCanvas::set_luminance_correct(bool on) { frame_->set_luminance_correct(on); }
bool FrameCanvas::luminance_correct() const { return frame_->luminance_correct(); }

void FrameCanvas::SetBrightness(uint8_t brightness) { frame_->SetBrightness(brightness); }
uint8_t FrameCanvas::brightness() { return frame_->brightness(); }
}  // end namespace rgb_matrix
