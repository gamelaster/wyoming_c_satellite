#ifndef WYOMING_SATELLITE_H_
#define WYOMING_SATELLITE_H_

#include <stdint.h>
#include <cJSON.h>
#include <stdbool.h>

struct wsat_packet
{
  cJSON* header;
  cJSON* data;
  uint8_t* payload;
  uint16_t payload_length;
};

struct wsat_microphone
{
  uint32_t rate;
  uint8_t width;
  uint8_t channels;
  int32_t (* init_fn)();
  int32_t (* destroy_fn)();
  int32_t (* start_stream_fn)();
  int32_t (* stop_stream_fn)();
};

struct wsat_sound
{
  int32_t (* init_fn)();
  int32_t (* destroy_fn)();
  int32_t (* start_stream_fn)(uint32_t rate, uint8_t width, uint8_t channels);
  int32_t (* on_data_fn)(uint8_t* data, uint32_t length);
  int32_t (* stop_stream_fn)();
};

int32_t wsat_run();
void wsat_stop();
void wsat_mic_set(struct wsat_microphone* mic);
void wsat_snd_set(struct wsat_sound* snd);
void wsat_mic_write_data(uint8_t* data, uint32_t length);

int32_t wsat_packet_send(struct wsat_packet pkt);
void wsat_packet_free(struct wsat_packet pkt, bool free_payload);

#endif