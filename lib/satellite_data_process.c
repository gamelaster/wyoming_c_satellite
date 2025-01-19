// Copyright 2024 Marek Kraus (@gamelaster / @gamiee)
// SPDX-License-Identifier: Apache-2.0

#include "satellite_priv.h"
#include <memory.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum process_state
{
  PROCESS_STATE_HEADER,
  PROCESS_STATE_DATA,
  PROCESS_STATE_PAYLOAD
};

void wsat_process_data()
{
  struct wsat_inst_priv* inst = &wsat_priv;
  static enum process_state state = PROCESS_STATE_HEADER;
  uint32_t offset = 0;
  uint32_t data_size;

  static uint32_t payload_length = 0;
  static uint32_t add_data_length = 0;
  static struct wsat_packet pkt;

  while (inst->data_buf_avail_bytes != 0 && (data_size = inst->data_buf_avail_bytes - offset) > 0) {
    uint8_t* data = inst->data_buf + offset;
    if (state == PROCESS_STATE_HEADER) {
      uint32_t header_length = 0;
      cJSON* header;
      const char* parse_end_ptr = 0;

      // As first thing, we are waiting for header.
      // We need to check if we have at least 3 bytes, since {}\n can be "valid"
      if (data_size < 3) {
        // We wait for more data.
        break;
      }
      // Header always must start with {", if it does not, we should scrap the data.
      // Due to the nature of the protocol, there is no other "recovery" option, so we need to
      // rely on TCP (and stack) that it will give us data of header exactly on 0 position.
      if (data[0] != '{' && data[1] != '"') {
        inst->data_buf_avail_bytes = 0;
        offset = 0;
        break;
      }

      // We now have data, let's see if we received only header or also another data.
      // Header should end with }\n
      if (data[data_size - 2] == '}' &&
          data[data_size - 1] == '\n') {
        header_length = data_size;
        goto header_found;
      }
      // Simple check did not worked, so let's find '\n'
      uint8_t* nl_pos = (uint8_t*)memchr(data, '\n', data_size);
      if (nl_pos != NULL) {
        header_length = nl_pos - data + 1;
        if (data[header_length - 2] == '}') {
          goto header_found;
        }
      }
      // We did not find the new line. This should not happen if really whole wsat_packet was sent
      // (due nature of TCP), but anyway, let's handle it.
      // Also, if we already have more than 4096 bytes, and no header, let's scrap the data
      if (data_size > 4096) {
        inst->data_buf_avail_bytes = 0;
        offset = 0;
        break;
      } else {
        // Break and wait for more data
        break;
      }
header_found:
      header = cJSON_ParseWithLengthOpts((const char*)data, header_length, &parse_end_ptr, false);
      if (header == NULL) {
        // Parse of header fails, so we scrap the data.
        LOGE("Failed to parse header");
        offset += header_length;
        continue;
      }
      // There is chance, that we receive {UNEXPECTED_JSON}{RELATED_JSON}\n . So we need to verify where parser
      // stopped and check if it's \n, and if not, scrap the unexpected json.
      if (parse_end_ptr[0] != '\n') {
        offset += (uint8_t*)parse_end_ptr - data;
        cJSON_Delete(header);
        continue;
      }
      // Check if we have mandatory field "type_field" and it's string
      cJSON* type_field = cJSON_GetObjectItem(header, "type");
      if (type_field == NULL || !cJSON_IsString(type_field)) {
        offset += header_length;
        cJSON_Delete(header);
        continue;
      }

      add_data_length = 0;
      payload_length = 0;
      pkt.header = header;
      pkt.data = NULL;
      pkt.payload = NULL;
      pkt.payload_length = 0;

      LOGD("Found header: %.*s\n", header_length - 1, data);

      cJSON* add_data_length_field = cJSON_GetObjectItem(header, "data_length");
      cJSON* payload_length_field = cJSON_GetObjectItem(header, "payload_length");
      if (add_data_length_field != NULL) add_data_length = (uint32_t)cJSON_GetNumberValue(add_data_length_field);
      if (payload_length_field != NULL) payload_length = (uint32_t)cJSON_GetNumberValue(payload_length_field);

      if (add_data_length != 0) {
        state = PROCESS_STATE_DATA;
      } else if (payload_length != 0) {
        state = PROCESS_STATE_PAYLOAD;
      } else {
        wsat_packet_handle(pkt);
      }
      offset += header_length;
    } else if (state == PROCESS_STATE_DATA) {
      if (data_size < add_data_length) {
        // We want to get all the data.
        break;
      }
      if (data[0] != '{' || data[add_data_length - 1] != '}') {
        // This is not JSON, so let's scrap everything
        inst->data_buf_avail_bytes = 0;
        offset = 0;
        wsat_packet_free(pkt, true);
        state = PROCESS_STATE_HEADER;
        break;
      }
      const char* parse_end_ptr = 0;
      cJSON* add_data = cJSON_ParseWithLengthOpts((const char*)data, add_data_length, &parse_end_ptr, false);
      uint32_t parsed_json_length = (uint8_t*)parse_end_ptr - data;
      if (add_data == NULL || parsed_json_length != add_data_length) {
        // If only part of the JSON is parsed, that means something is definitely wrong.
        inst->data_buf_avail_bytes = 0;
        offset = 0;
        wsat_packet_free(pkt, true);
        state = PROCESS_STATE_HEADER;
        break;
      }
      pkt.data = add_data;
      if (payload_length != 0) {
        state = PROCESS_STATE_PAYLOAD;
      } else {
        wsat_packet_handle(pkt);
        state = PROCESS_STATE_HEADER;
      }
      offset += add_data_length;
    } else if (state == PROCESS_STATE_PAYLOAD) {
      if (data_size < payload_length) {
        // We want to get all the payload.
        break;
      }
      pkt.payload_length = payload_length;
      pkt.payload = malloc(payload_length);
      memcpy(pkt.payload, data, payload_length);
      wsat_packet_handle(pkt);
      state = PROCESS_STATE_HEADER;
      offset += payload_length;
    }
  }
  if (inst->data_buf_avail_bytes != 0 && offset != 0 && inst->data_buf_avail_bytes > offset) {
    // Copy the rest of unprocessed data to the beginning of the buffer
    memcpy(inst->data_buf, inst->data_buf + offset, inst->data_buf_avail_bytes - offset);
  }

  if (inst->data_buf_avail_bytes < offset) {
    LOGE("Offset is higher than received bytes, something went wrong!");
    inst->data_buf_avail_bytes = 0;
    return;
  }
  inst->data_buf_avail_bytes -= offset;
}