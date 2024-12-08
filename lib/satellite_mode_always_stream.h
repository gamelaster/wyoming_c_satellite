// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#ifndef SATELLITE_MODE_ALWAYS_STREAM_H_
#define SATELLITE_MODE_ALWAYS_STREAM_H_

#include <wyoming_user.h>
#include <wyoming/satellite.h>
#include <stdbool.h>

struct wsat_mode_always_stream_inst
{
  bool is_streaming;
  PLAT_MUTEX_TYPE is_streaming_mutex;
};

#endif