// Copyright 2026 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

/**
 * Event handling is made by defining list of packet types,
 * which will be handled. Right now, we compare packet types just with strcmp.
 * Although, at some point it might be beneficial to calculate hash of the types, and compare that.
 */

#include <stdlib.h>
#include <string.h>

#include "satellite_priv.h"

struct packet_type_map_entry
{
  const char* type_str;
  enum wsat_packet_type type_enum;
} static const packet_type_map[] = {
  { "describe",        WSAT_EVENT_TYPE_DESCRIBE },
  { "ping",            WSAT_EVENT_TYPE_PING },
  { "run-satellite",   WSAT_EVENT_TYPE_RUN_SATELLITE },
  { "pause-satellite", WSAT_EVENT_TYPE_PAUSE_SATELLITE },
  { "audio-start",     WSAT_EVENT_TYPE_AUDIO_START },
  { "audio-chunk",     WSAT_EVENT_TYPE_AUDIO_CHUNK },
  { "audio-stop",      WSAT_EVENT_TYPE_AUDIO_STOP },
  { "detection",       WSAT_EVENT_TYPE_DETECTION },
  { "voice-stopped",   WSAT_EVENT_TYPE_VOICE_STOPPED },
  { "error",           WSAT_EVENT_TYPE_ERROR },
  { "transcript",      WSAT_EVENT_TYPE_TRANSCRIPT},
};


static int32_t handle_describe(struct wsat_decoded_event* evt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
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

  if (inst->wake != NULL) {
    // TODO: Get more information from wake component
    cJSON* wake_entry = cJSON_CreateObject();

    cJSON_AddStringToObject(wake_entry, "name", "microwakeword-c");
    cJSON* w0_attr = cJSON_CreateObject();
    cJSON_AddItemToObject(wake_entry, "attribution", w0_attr);
    cJSON_AddStringToObject(w0_attr, "name", "gamelaster");
    cJSON_AddStringToObject(w0_attr, "url", "-");
    cJSON_AddBoolToObject(wake_entry, "installed", 1);
    cJSON_AddStringToObject(wake_entry, "description",
                            "C compatible implementation of MicroWakeWord");
    cJSON_AddStringToObject(wake_entry, "version", "1.0.0");

    cJSON* models = cJSON_CreateArray();
    cJSON_AddItemToObject(wake_entry, "models", models);

    cJSON* model = cJSON_CreateObject();
    cJSON_AddItemToArray(models, model);
    cJSON_AddStringToObject(model, "name", inst->wake->name);
    cJSON* m_attr = cJSON_CreateObject();
    cJSON_AddItemToObject(model, "attribution", m_attr);
    cJSON_AddStringToObject(m_attr, "name", "-");
    cJSON_AddStringToObject(m_attr, "url", "-");
    cJSON_AddBoolToObject(model, "installed", 1);
    cJSON_AddStringToObject(model, "description", "Wake word model");
    cJSON_AddStringToObject(model, "version", "1.0.0");
    cJSON* langs = cJSON_CreateArray();
    cJSON_AddItemToObject(model, "languages", langs);
    cJSON_AddStringToObject(model, "phrase", inst->wake->name);

    cJSON_AddItemToArray(wake_array, wake_entry);
  }
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

  struct wsat_event res_evt = {
    .header = header,
    .data = data
  };
  wsat_event_send(&res_evt);
  wsat_event_free(&res_evt, true);
  return 0;
}

int32_t handle_ping(struct wsat_decoded_event* evt)
{
  struct wsat_inst_priv* inst = &wsat_priv;

  cJSON* header = cJSON_CreateObject();
  cJSON_AddStringToObject(header, "type", "pong");
  cJSON_AddStringToObject(header, "version", "1.7.2");

  cJSON* data = NULL;
  if (evt->data != NULL) {
    data = cJSON_CreateObject();
    char* text = cJSON_GetStringValue(cJSON_GetObjectItem(evt->data, "text"));
    cJSON_AddStringToObject(data, "text", text);
  }

  struct wsat_event res_pkt = {
    .header = header,
    .data = data
  };
  wsat_event_send(&res_pkt);
  wsat_event_free(&res_pkt, false);
  return 0;
}

static int32_t handle_audio_start(struct wsat_decoded_event* evt)
{
  // TODO: Maybe tell to SND more information about the length of incoming data.
  struct wsat_inst_priv* inst = &wsat_priv;
  if (evt->data != NULL && inst->snd != NULL) {
    struct wsat_sys_event_audio_start_params params;
    params.rate = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(evt->data, "rate"));
    params.width = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(evt->data, "width"));
    params.channels = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(evt->data, "channels"));
    inst->snd->comp.sys_event_handle_fn(WSAT_SYS_EVENT_SND_AUDIO_START, &params);
  }
  return 0;
}

static int32_t handle_audio_chunk(struct wsat_decoded_event* evt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  if (inst->snd != NULL) {
    struct wsat_sys_event_buffer_params params;
    params.data = evt->payload.data;
    params.size = evt->payload.size;
    inst->snd->comp.sys_event_handle_fn(WSAT_SYS_EVENT_SND_AUDIO_DATA, &params);
  }
  return 0;
}

static int32_t handle_audio_stop(struct wsat_decoded_event* evt)
{
  struct wsat_inst_priv* inst = &wsat_priv;
  if (inst->snd != NULL) {
    inst->snd->comp.sys_event_handle_fn(WSAT_SYS_EVENT_SND_AUDIO_END, NULL);
  }
  return 0;
}

static int32_t handle_error(struct wsat_decoded_event* evt)
{
  char* error_str = NULL;
  char* error_code = NULL;
  if (evt->data != NULL) {
    error_str = cJSON_GetStringValue(cJSON_GetObjectItem(evt->data, "text"));
    error_code = cJSON_GetStringValue(cJSON_GetObjectItem(evt->data, "code"));
  }
  LOGE("Satellite returned error: \"%s\" (%s)", error_str != NULL ? error_str : "-",
    error_code != NULL ? error_code : "-");
  return 0;
}

struct packet_handler
{
  enum wsat_packet_type type;
  int32_t (* handler_fn)(struct wsat_decoded_event*);
} static packet_handlers[] = {
  { WSAT_EVENT_TYPE_DESCRIBE,      handle_describe },
  { WSAT_EVENT_TYPE_PING,          handle_ping },
  { WSAT_EVENT_TYPE_AUDIO_START,   handle_audio_start },
  { WSAT_EVENT_TYPE_AUDIO_CHUNK,   handle_audio_chunk },
  { WSAT_EVENT_TYPE_AUDIO_STOP,    handle_audio_stop },
  { WSAT_EVENT_TYPE_ERROR,         handle_error },
  // { WSAT_EVENT_TYPE_DETECTION,     handle_detection },
  // { WSAT_EVENT_TYPE_VOICE_STOPPED, handle_voice_stopped }
};

int32_t wsat_event_handle_default(enum wsat_packet_type packet_type, struct wsat_decoded_event* evt)
{
  uint8_t handlers_count = sizeof(packet_handlers) / sizeof(struct packet_handler);
  for (uint8_t i = 0; i < handlers_count; i++) {
    if (packet_type == packet_handlers[i].type) {
      packet_handlers[i].handler_fn(evt);
      return 1;
    }
  }
  return 0;
}

void wsat_event_handle(struct wsat_decoded_event* evt)
{
  int32_t res = 0;
  struct wsat_inst_priv* inst = &wsat_priv;
  const static uint8_t packet_type_entries = sizeof(packet_type_map) / sizeof(struct packet_type_map_entry);

  enum wsat_packet_type packet_type = WSAT_EVENT_TYPE_NONE;
  for (uint8_t i = 0; i < packet_type_entries; i++) {
    if (strcmp(evt->header.type, packet_type_map[i].type_str) == 0) {
      packet_type = packet_type_map[i].type_enum;
      break;
    }
  }

  if (inst->mode->event_handle_fn != NULL) {
    res = inst->mode->event_handle_fn(packet_type, evt);
  } else {
    res = wsat_event_handle_default(packet_type, evt);
  }

  if (res == 0) {
    LOGD("Packet type \"%s\" was not handled", evt->header.type);
#if 1
    char* header = cJSON_PrintUnformatted(evt->header.json);
    LOGD("Header: %s", header);
    free(header);
    if (evt->data != NULL) {
      char* data = cJSON_PrintUnformatted(evt->data);
      LOGD("Data: %s", data);
      free(data);
    }
#endif
  }

}