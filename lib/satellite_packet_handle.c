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

static int32_t handle_describe(struct wsat_packet pkt)
{
  cJSON* header = cJSON_CreateObject();
  cJSON_AddStringToObject(header, "type", "info");
  cJSON_AddStringToObject(header, "version", "1.5.2");
  cJSON* data = cJSON_CreateObject();
  cJSON* asr_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "asr", asr_array);
  cJSON* tts_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "tts", tts_array);
  cJSON* handle_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "handle", handle_array);
  cJSON* intent_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "intent", intent_array);
  cJSON* wake_array = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "wake", wake_array);

  cJSON* satellite_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(satellite_obj, "name", "Wyoming C Satellite");
  cJSON* attribution_obj = cJSON_CreateObject();
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
  wsat_packet_free(res_pkt, true);
  return 0;
}

struct packet_type_map_entry
{
  const char* type_str;
  enum wsat_packet_type type_enum;
} static packet_type_map[] = {
  { "describe",        WSAT_EVENT_TYPE_DESCRIBE },
  { "run-satellite",   WSAT_EVENT_TYPE_RUN_SATELLITE },
  { "pause-satellite", WSAT_EVENT_TYPE_PAUSE_SATELLITE },
};

struct packet_handler
{
  enum wsat_packet_type type;
  int32_t (* handler_fn)(struct wsat_packet);
} static packet_handlers[] = {
  { WSAT_EVENT_TYPE_DESCRIBE, handle_describe }
};


int32_t wsat_packet_default_handle(enum wsat_packet_type packet_type, struct wsat_packet pkt)
{
  uint8_t handlers_count = sizeof(packet_handlers) / sizeof(struct packet_handler);
  for (uint8_t i = 0; i < handlers_count; i++) {
    if (packet_type == packet_handlers[i].type) {
      packet_handlers[i].handler_fn(pkt);
      return 1;
    }
  }
  return 0;
}

void wsat_packet_handle(struct wsat_packet pkt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  uint8_t packet_type_entries = sizeof(packet_type_map) / sizeof(struct packet_type_map_entry);
  const char* packet_type_str = cJSON_GetStringValue(cJSON_GetObjectItem(pkt.header, "type"));
  int32_t res = 0;

  enum wsat_packet_type packet_type = WSAT_EVENT_TYPE_NONE;
  for (uint8_t i = 0; i < packet_type_entries; i++) {
    if (strcmp(packet_type_str, packet_type_map[i].type_str) == 0) {
      packet_type = packet_type_map[i].type_enum;
      break;
    }
  }

  if (inst->mode->packet_handle_fn != NULL) {
    res = inst->mode->packet_handle_fn(packet_type, pkt);
  } else {
    res = wsat_packet_default_handle(packet_type, pkt);
  }

  if (res == 0) {
    LOGI("Packet type \"%s\" was not handled", packet_type_str);
  }

  wsat_packet_free(pkt, true);
}