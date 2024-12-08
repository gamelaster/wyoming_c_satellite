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
  int32_t (* init_fn)();
  int32_t (* destroy_fn)();
  int32_t (* start_stream_fn)();
  int32_t (* stop_stream_fn)();
};


int32_t wsat_run();
void wsat_set_microphone(struct wsat_microphone* mic);

int32_t wsat_packet_send(struct wsat_packet pkt);
void wsat_packet_free(struct wsat_packet pkt, bool free_payload);

#endif