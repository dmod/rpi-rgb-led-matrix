/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 * Copyright (C) 2013, 2016 Henner Zeller <h.zeller@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http: *gnu.org/licenses/gpl-2.0.txt>
 */

/*
 * We do this in plain C so that we can use designated initializers.
 */
#include "hardware-mapping.h"

#define GPIO_BIT(b) ((uint32_t)1<<(b))

struct HardwareMapping matrix_hardware_mappings[] = {
  /*
   * The regular hardware mapping of upstream's wiring documentation and
   * adapter PCBs.
   */
  {
    .name          = "regular",

    .output_enable = GPIO_BIT(18),
    .clock         = GPIO_BIT(17),
    .strobe        = GPIO_BIT(4),

    /* Address lines */
    .a             = GPIO_BIT(22),
    .b             = GPIO_BIT(23),
    .c             = GPIO_BIT(24),
    .d             = GPIO_BIT(25),
    .e             = GPIO_BIT(15),  /* RxD kept free unless 1:64 */

    /* RGB for both sub-panels */
    .p0_r1         = GPIO_BIT(11),  /* masks: SPI0_SCKL  */
    .p0_g1         = GPIO_BIT(27),
    .p0_b1         = GPIO_BIT(7),   /* masks: SPI0_CE1   */
    .p0_r2         = GPIO_BIT(8),   /* masks: SPI0_CE0   */
    .p0_g2         = GPIO_BIT(9),   /* masks: SPI0_MISO  */
    .p0_b2         = GPIO_BIT(10),  /* masks: SPI0_MOSI  */
  },

  /*
   * This is used if you have an Adafruit HAT in the default configuration
   */
  {
    .name          = "adafruit-hat",

    .output_enable = GPIO_BIT(4),
    .clock         = GPIO_BIT(17),
    .strobe        = GPIO_BIT(21),

    .a             = GPIO_BIT(22),
    .b             = GPIO_BIT(26),
    .c             = GPIO_BIT(27),
    .d             = GPIO_BIT(20),
    .e             = GPIO_BIT(24),  /* Needs a solder bridge for 1:32 panels */

    .p0_r1         = GPIO_BIT(5),
    .p0_g1         = GPIO_BIT(13),
    .p0_b1         = GPIO_BIT(6),
    .p0_r2         = GPIO_BIT(12),
    .p0_g2         = GPIO_BIT(16),
    .p0_b2         = GPIO_BIT(23),
  },

  /*
   * An Adafruit HAT with the PWM modification
   */
  {
    .name          = "adafruit-hat-pwm",

    .output_enable = GPIO_BIT(18),  /* The only change compared to above */
    .clock         = GPIO_BIT(17),
    .strobe        = GPIO_BIT(21),

    .a             = GPIO_BIT(22),
    .b             = GPIO_BIT(26),
    .c             = GPIO_BIT(27),
    .d             = GPIO_BIT(20),
    .e             = GPIO_BIT(24),

    .p0_r1         = GPIO_BIT(5),
    .p0_g1         = GPIO_BIT(13),
    .p0_b1         = GPIO_BIT(6),
    .p0_r2         = GPIO_BIT(12),
    .p0_g2         = GPIO_BIT(16),
    .p0_b2         = GPIO_BIT(23),
  },

  {0}
};
