// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include "satellite_priv.h"

void wsat_mic_write_data(uint8_t* data, uint32_t length)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  struct wsat_sys_event_buffer_params arg = {
    data,
    length
  };
  inst->mode->component.sys_event_handle_fn(WSAT_SYS_EVENT_MIC_DATA, &arg);
}

void wsat_wake_detection()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  // TODO: Send to every component?
  inst->mode->component.sys_event_handle_fn(WSAT_SYS_EVENT_WAKE_DETECTION, NULL);
}