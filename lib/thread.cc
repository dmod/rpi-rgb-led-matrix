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

#include "thread.h"

#include <assert.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace rgb_matrix {
void *Thread::PthreadCallRun(void *tobject) {
  reinterpret_cast<Thread*>(tobject)->Run();
  return NULL;
}

Thread::Thread() : started_(false) {}
Thread::~Thread() {
  WaitStopped();
}

void Thread::WaitStopped() {
  if (!started_) return;
  int result = pthread_join(thread_, NULL);
  if (result != 0) {
    perror("Issue joining thread");
  }
  started_ = false;
}

void Thread::Start(int priority, uint32_t affinity_mask) {
  assert(!started_);  // Did you call WaitStopped() ?
  pthread_create(&thread_, NULL, &PthreadCallRun, this);
  int err;

  if (priority > 0) {
    struct sched_param p;
    p.sched_priority = priority;
    if ((err = pthread_setschedparam(thread_, SCHED_FIFO, &p))) {
      char buffer[PATH_MAX];
      const char *bin = realpath("/proc/self/exe", buffer);  // Linux specific.
      fprintf(stderr, "Can't set realtime thread priority=%d: %s.\n"
              "\tYou are probably not running as root ?\n"
              "\tThis will seriously mess with color stability and flicker\n"
              "\tof the matrix. Please run as `root` (e.g. by invoking this\n"
              "\tprogram with `sudo`), or setting the capability on this\n"
              "\tbinary by calling\n"
              "\tsudo setcap 'cap_sys_nice=eip' %s\n",
              p.sched_priority, strerror(err), bin ? bin : "<this binary>");
    }
  }

  if (affinity_mask != 0) {
    cpu_set_t cpu_mask;
    CPU_ZERO(&cpu_mask);
    for (int i = 0; i < 32; ++i) {
      if ((affinity_mask & (1<<i)) != 0) {
        CPU_SET(i, &cpu_mask);
      }
    }
    if ((err=pthread_setaffinity_np(thread_, sizeof(cpu_mask), &cpu_mask))) {
      fprintf(stderr, "Can't set thread CPU affinity: %s.\n", strerror(err));
    }
  }

  started_ = true;
}

void Mutex::WaitOn(pthread_cond_t *cond) {
  pthread_cond_wait(cond, &mutex_);
}
}  // namespace rgb_matrix
