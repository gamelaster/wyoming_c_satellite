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
      PLAT_MUTEX_LOCK(&inst->is_streaming_mutex);
      inst->is_streaming = true;
      PLAT_MUTEX_UNLOCK(&inst->is_streaming_mutex);
      wsat_send_run_pipeline(NULL);
      if (inst->mic && inst->mic->start_stream_fn) {
        inst->mic->start_stream_fn();
      }
      res = 1;
      break;
    }
    case WSAT_EVENT_TYPE_AUDIO_START: {
      // This is a difference between Python Wyoming. We stop mic stream after STT ends,
      // so we don't stream TTS answer playback back to server for STT.
      if (inst->mic && inst->mic->stop_stream_fn) {
        inst->mic->stop_stream_fn();
      }
      res = 1;
      break;
    }
    case WSAT_EVENT_TYPE_AUDIO_STOP: {
      // This is a difference between Python Wyoming. After TTS we start stream again.
      if (inst->mic && inst->mic->start_stream_fn) {
        inst->mic->start_stream_fn();
      }
      res = 1;
      break;
    }
    case WSAT_EVENT_TYPE_PAUSE_SATELLITE: {
      PLAT_MUTEX_LOCK(&inst->is_streaming_mutex);
      inst->is_streaming = false;
      PLAT_MUTEX_UNLOCK(&inst->is_streaming_mutex);
      res = 1;
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
  return 0;
}

static int32_t wsat_mode_destroy()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_mode_always_stream_inst* mode_inst = &inst->mode_inst.always_stream;
  return 0;
}

struct wsat_mode_if wsat_mode_always_stream = {
  WSAT_MODE_ALWAYS_STREAM,
  wsat_mode_init,
  wsat_mode_destroy,
  wsat_mode_packet_handle,
};