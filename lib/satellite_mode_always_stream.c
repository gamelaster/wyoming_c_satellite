// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include "satellite_priv.h"

static int32_t wsat_mode_packet_handle(enum wsat_packet_type packet_type, struct wsat_packet pkt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;

  int32_t res = wsat_packet_default_handle(packet_type, pkt);
  switch (packet_type) {
    case WSAT_EVENT_TYPE_RUN_SATELLITE: {
      PLAT_MUTEX_LOCK(&mode_inst->is_streaming_mutex);
      mode_inst->is_streaming = true;
      PLAT_MUTEX_UNLOCK(&mode_inst->is_streaming_mutex);
      wsat_send_run_pipeline(NULL);
      if (inst->mic->start_stream_fn != NULL) {
        inst->mic->start_stream_fn();
      }
      break;
    }
    case WSAT_EVENT_TYPE_PAUSE_SATELLITE: {
      PLAT_MUTEX_LOCK(&mode_inst->is_streaming_mutex);
      mode_inst->is_streaming = false;
      PLAT_MUTEX_UNLOCK(&mode_inst->is_streaming_mutex);
    }
    default:
      break;
  }
  return res;
}

static int32_t wsat_mode_init()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;
  if (PLAT_MUTEX_CREATE(&mode_inst->is_streaming_mutex) != 0) {
    LOGD("Failed to initialize mutex");
    return -1;
  }
  return 0;
}

static int32_t wsat_mode_destroy()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;
  PLAT_MUTEX_DESTROY(&mode_inst->is_streaming_mutex);
  return 0;
}

struct wsat_mode_if wsat_mode_always_stream = {
  WSAT_MODE_ALWAYS_STREAM,
  wsat_mode_init,
  wsat_mode_destroy,
  wsat_mode_packet_handle,
};