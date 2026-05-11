// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include "satellite_priv.h"

int32_t wsat_mic_write_data(uint8_t* data, uint32_t length)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_sys_event_buffer_params arg = {
    data,
    length
  };
  if (!wsat_server_client_connected()) return -WSAT_ERROR_SAT_DISCONNECTED;
  inst->mode->comp.sys_event_handle_fn(WSAT_SYS_EVENT_MIC_DATA, &arg);
  return WSAT_OK;
}

int32_t wsat_wake_detection()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  if (!wsat_server_client_connected()) return -WSAT_ERROR_SAT_DISCONNECTED;
  // TODO: Send to every component?
  inst->mode->comp.sys_event_handle_fn(WSAT_SYS_EVENT_WAKE_DETECTION, NULL);
  return WSAT_OK;
  return WSAT_OK;
}