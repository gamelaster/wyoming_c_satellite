// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

/**
 * Packet handling is made by defining list of packet types,
 * which will be handled. Right now, we compare packet types just with strcmp.
 * Although, at some point it might be benefit to calculate hash of the types, and compare that.
 */

#include <string.h>
#include <stdbool.h>
#include "satellite_priv.h"

static uint32_t handle_describe(struct wsat_packet pkt)
{
  cJSON *header = cJSON_CreateObject();
  cJSON_AddStringToObject(header, "type", "info");
  cJSON_AddStringToObject(header, "version", "1.5.2");
  cJSON *data = cJSON_CreateObject();
  cJSON *asr_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "asr", asr_array);
  cJSON *tts_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "tts", tts_array);
  cJSON *handle_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "handle", handle_array);
  cJSON *intent_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "intent", intent_array);
  cJSON *wake_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "wake", wake_array);

  cJSON *satellite_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(satellite_obj, "name", "Wyoming C Satellite");
  cJSON *attribution_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(attribution_obj, "name", "");
  cJSON_AddStringToObject(attribution_obj, "url", "");
  cJSON_AddItemToObject(satellite_obj, "attribution", attribution_obj);
  cJSON_AddTrueToObject(satellite_obj, "installed");
  cJSON_AddStringToObject(satellite_obj, "description", "my satellite");
  cJSON_AddStringToObject(satellite_obj, "version", "1.0.0");
  cJSON_AddNullToObject(satellite_obj, "area");
  cJSON_AddNullToObject(satellite_obj, "snd_format");
  cJSON_AddItemToObject(data, "satellite", satellite_obj);

  struct wsat_packet res_pkt = {
    .header = header,
    .data = data
  };
  wsat_packet_send(res_pkt);
  return 0;
}

struct packet_handler
{
  const char* type;
  uint32_t (*handler_fn)(struct wsat_packet);
} static packet_handlers[] = {
  { "describe", handle_describe }
};

void wsat_handle_packet(struct wsat_packet pkt)
{
  uint8_t handlers_count = sizeof(packet_handlers) / sizeof(struct packet_handler);
  const char* packet_type = cJSON_GetStringValue(cJSON_GetObjectItem(pkt.header, "type"));
  bool packet_handled = false;
  uint32_t res = 0;

  for (uint8_t i = 0; i < handlers_count; i++) {
    if (strcmp(packet_type, packet_handlers[i].type) == 0) {
      res = packet_handlers[i].handler_fn(pkt);
      packet_handled = true;
    }
  }
  if (!packet_handled) {
    LOGI("Packet type \"%s\" was not handled", packet_type);
  }
  if (res != 1) {
    wsat_packet_free(pkt, true);
  }
}