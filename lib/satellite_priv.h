// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#ifndef SATELLITE_PRIV_H_
#define SATELLITE_PRIV_H_

#include <wyoming_user.h>
#include <wyoming/satellite.h>
#include <stdint.h>

// To avoid dynamic allocation, every satellite mode instance is placed into union
#include "satellite_mode_always_stream.h"

#define DATA_BUFFER_SIZE (8096)

enum wsat_mode_type
{
  WSAT_MODE_ALWAYS_STREAM,
  WSAT_MODE_WAKE_STREAM
};

enum wsat_packet_type
{
  WSAT_EVENT_TYPE_NONE,
  WSAT_EVENT_TYPE_DESCRIBE,
  WSAT_EVENT_TYPE_RUN_SATELLITE,
  WSAT_EVENT_TYPE_PAUSE_SATELLITE,
};

struct wsat_mode_if
{
  enum wsat_mode_type type;
  int32_t (* init_fn)();
  int32_t (* destroy_fn)();
  int32_t (* packet_handle_fn)(enum wsat_packet_type packet_type, struct wsat_packet pkt);
};

struct wsat_inst_priv
{
  uint8_t data_buf[DATA_BUFFER_SIZE];
  uint32_t data_buf_avail_bytes;

  int sockfd;
  int connfd;

  PLAT_MUTEX_TYPE conn_mutex;

  struct wsat_mode_if* mode;
  union
  {
    struct wsat_mode_always_stream_inst always_stream;
  } mode_inst;

  struct wsat_microphone* mic;
  void* snd;
};

extern struct wsat_inst_priv wsat_priv;

void wsat_process_data();
void wsat_packet_handle(struct wsat_packet pkt);
int32_t wsat_packet_default_handle(enum wsat_packet_type packet_type, struct wsat_packet pkt);
int32_t wsat_send_run_pipeline(const char* pipeline_name);

extern struct wsat_mode_if wsat_mode_always_stream;

#endif