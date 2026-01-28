// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#ifndef SATELLITE_PRIV_H_
#define SATELLITE_PRIV_H_

#include <wyoming_user.h>
#include <wyoming/satellite.h>
#include <stdint.h>

// To avoid dynamic allocation, every satellite mode instance is placed into union

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

enum wsat_mode_type
{
  WSAT_MODE_ALWAYS_STREAM,
  WSAT_MODE_WAKE_STREAM
};

enum wsat_packet_type
{
  WSAT_EVENT_TYPE_NONE,
  WSAT_EVENT_TYPE_DESCRIBE,
  WSAT_EVENT_TYPE_PING,
  WSAT_EVENT_TYPE_RUN_SATELLITE,
  WSAT_EVENT_TYPE_PAUSE_SATELLITE,
  WSAT_EVENT_TYPE_AUDIO_START,
  WSAT_EVENT_TYPE_AUDIO_CHUNK,
  WSAT_EVENT_TYPE_AUDIO_STOP,
  WSAT_EVENT_TYPE_DETECTION,
  WSAT_EVENT_TYPE_VOICE_STOPPED,
  WSAT_EVENT_TYPE_ERROR,
  WSAT_EVENT_TYPE_TRANSCRIPT,
};

struct wsat_mode
{
  struct wsat_component component;
  enum wsat_mode_type type;
  int32_t (* event_handle_fn)(enum wsat_packet_type event_type, struct wsat_decoded_event* evt);
};

struct wsat_mode_always_stream_inst
{
  bool is_streaming;
  PLAT_MUTEX_TYPE is_streaming_mutex;
};

struct wsat_mode_wake_stream_inst
{
  bool is_streaming;
  bool is_paused;
  PLAT_MUTEX_TYPE state_mutex;
};

extern struct wsat_mode wsat_mode_wake_stream;
extern struct wsat_mode wsat_mode_always_stream;

enum wsat_event_decoder_process_state
{
  WSAT_EVENT_DECODER_PROCESS_STATE_HEADER,
  WSAT_EVENT_DECODER_PROCESS_STATE_DATA,
  WSAT_EVENT_DECODER_PROCESS_STATE_PAYLOAD
};

struct wsat_event_decoder
{
  enum wsat_event_decoder_process_state state;
  struct wsat_decoded_event wip_evt;
  uint8_t buffer[EVENT_DECODER_BUFFER_SIZE];
  uint32_t buffer_length;
  uint8_t payload_buffer[EVENT_DECODER_BUFFER_SIZE];
  uint32_t payload_received;
};

struct wsat_server
{
  int sockfd;
  int connfd;

  PLAT_MUTEX_TYPE state_mutex;
  PLAT_MUTEX_TYPE send_mutex;
  bool stop_requested;

  struct wsat_event_decoder decoder;
};

struct wsat_inst_priv
{
  struct wsat_server server;

  struct wsat_mode* mode;
  union
  {
    struct wsat_mode_always_stream_inst always_stream;
    struct wsat_mode_wake_stream_inst wake_stream;
  } mode_inst;

  struct wsat_component* components[4];

  struct wsat_microphone* mic;
  struct wsat_sound* snd;
  struct wsat_wake* wake;
};

extern struct wsat_inst_priv wsat_priv;

int32_t wsat_server_run();
int32_t wsat_run_pipeline_send(const char* pipeline_name);
int32_t wsat_audio_chunk_send(uint8_t* data, uint32_t length);

void wsat_event_handle(struct wsat_decoded_event* evt);
int32_t wsat_event_handle_default(enum wsat_packet_type packet_type, struct wsat_decoded_event* evt);

void wsat_event_decoder_reset(struct wsat_event_decoder* dec);
uint32_t wsat_event_decoder_buffer_get(struct wsat_event_decoder* dec, uint8_t** buffer);
void wsat_event_decoder_buffer_advance(struct wsat_event_decoder* dec, uint32_t length);
int32_t wsat_event_decoder_next(struct wsat_event_decoder* dec, struct wsat_decoded_event* out_event);
void wsat_decoded_event_free(struct wsat_decoded_event* evt);

#if 0
void wsat_process_data();
void wsat_packet_handle(struct wsat_event pkt);
int32_t wsat_packet_default_handle(enum wsat_packet_type packet_type, struct wsat_event pkt);
int32_t wsat_send_run_pipeline(const char* pipeline_name);

extern struct wsat_mode wsat_mode_always_stream;
extern struct wsat_mode wsat_mode_wake_stream;
#endif

#endif
