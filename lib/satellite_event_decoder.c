#include <string.h>

#include "satellite_priv.h"

void wsat_event_decoder_reset(struct wsat_event_decoder* dec)
{
  dec->state = WSAT_EVENT_DECODER_PROCESS_STATE_HEADER;
  dec->buffer_length = 0;
  memset(&dec->wip_evt, 0, sizeof(dec->wip_evt));
  dec->payload_received = 0;
}

uint32_t wsat_event_decoder_buffer_get(struct wsat_event_decoder* dec, uint8_t** buffer)
{
  *buffer = dec->buffer + dec->buffer_length;
  return EVENT_DECODER_BUFFER_SIZE - dec->buffer_length;
}

void wsat_event_decoder_buffer_advance(struct wsat_event_decoder* dec, uint32_t length)
{
  dec->buffer_length += length; // TODO: Add check if it's not more than allowed.
}

int32_t wsat_event_decoder_next(struct wsat_event_decoder* dec, struct wsat_decoded_event* out_event)
{
  uint8_t flags = 0;
  uint32_t processed_bytes = 0;
  bool ready_to_output_event = false;
  while (processed_bytes < dec->buffer_length) {
    uint8_t* buffer = dec->buffer + processed_bytes;
    uint32_t buffer_length = dec->buffer_length - processed_bytes;
    struct wsat_event_header* header = &dec->wip_evt.header;
    if (dec->state == WSAT_EVENT_DECODER_PROCESS_STATE_HEADER) {
      // Let's find start of the header `{"` anywhere in buffer
      if (buffer_length < 2) {
        return 0;
      }
      uint8_t* header_start_pos = (uint8_t*)memmem(buffer, buffer_length, "{\"", 2);
      if (header_start_pos == NULL) {
        if (buffer[buffer_length - 1] == '{') {
          processed_bytes += buffer_length - 1;
          break;
        }
        goto scratch_everything;
      }
      uint32_t header_start_offset = header_start_pos - buffer;
      // Now find end of the header `}\n`
      uint8_t* header_end_pos = (uint8_t*)memmem(header_start_pos,
                                                 buffer_length - header_start_offset,
                                                 "}\n", 2);
      if (header_end_pos == NULL) {
        // We didn't find header, let's check if rest of data are not too much long for the limit.
        const uint32_t current_header_size = buffer_length - header_start_offset;
        if (current_header_size + 2 > EVENT_DECODER_BUFFER_SIZE) {
          LOGD("Too big or invalid header, scratching the data.");
          goto scratch_everything;
        }
        // Remove unused data, if any
        if (header_start_offset != 0) {
          processed_bytes += header_start_offset;
          break;
        }
        return 0;
      }
      header_end_pos += 2; // So it marks real end of whole header
      const uint32_t header_size = header_end_pos - header_start_pos;
      const char* parse_end_ptr = NULL;
      cJSON* header_json = cJSON_ParseWithLengthOpts((const char*)header_start_pos, header_size, &parse_end_ptr, false);
      // If header wasn't found, or it's not real header we found, let's scrap where parser end
      if (header_json == NULL || ((uint8_t*)parse_end_ptr + 1) != header_end_pos) {
        LOGD("Failed to parse header");
        uint32_t parsed_bytes = (uint8_t*)parse_end_ptr - header_start_pos;
        if (parse_end_ptr == NULL) {
          parsed_bytes = header_size;
        }
        processed_bytes += header_start_offset + parsed_bytes;
        if (header_json != NULL) cJSON_Delete(header_json);
        continue;
      }
      memset(&dec->wip_evt, 0, sizeof(struct wsat_decoded_event));
      header->json = header_json;
      cJSON* type_field = cJSON_GetObjectItem(header_json, "type");
      if (type_field == NULL || !cJSON_IsString(type_field)) {
        LOGD("Type is missing in header");
        goto scratch_header;
      }
      header->type = cJSON_GetStringValue(type_field);
      cJSON* data_length_field = cJSON_GetObjectItem(header_json, "data_length");
      cJSON* payload_length_field = cJSON_GetObjectItem(header_json, "payload_length");
      uint32_t data_length = 0, payload_length = 0;
      if (data_length_field != NULL) {
        data_length = (uint32_t)cJSON_GetNumberValue(data_length_field);
        if (data_length > EVENT_DECODER_BUFFER_SIZE) {
          LOGE("Data length is too big: %d", data_length);
          goto scratch_header;
        }
      }
      if (payload_length_field != NULL) {
        double payload_length_dbl = cJSON_GetNumberValue(payload_length_field);
        if (payload_length_dbl < 0) goto scratch_header;
        payload_length = (uint32_t)payload_length_dbl;
        if (payload_length > 128 * 1024) {
          LOGE("Payload length is too big: %d", payload_length);
          goto scratch_header;
        }
      }
      header->data_length = data_length;
      header->payload_length = payload_length;

      cJSON* data_field = cJSON_GetObjectItem(header_json, "data");
      if (data_field != NULL && cJSON_IsObject(data_field)) {
        header->data = data_field;
      }
      processed_bytes += header_start_offset + header_size;
      flags |= WSAT_DECODED_EVENT_FLAG_BEGIN;

      if (data_length > 0) {
        dec->state = WSAT_EVENT_DECODER_PROCESS_STATE_DATA;
      } else if (payload_length > 0) {
        dec->state = WSAT_EVENT_DECODER_PROCESS_STATE_PAYLOAD;
      } else {
        flags |= WSAT_DECODED_EVENT_FLAG_END;
        ready_to_output_event = true;
        break;
      }
      continue;
scratch_header:
      processed_bytes += header_start_offset + header_size;
      cJSON_Delete(header_json);
      continue;
    } else if (dec->state == WSAT_EVENT_DECODER_PROCESS_STATE_DATA) {
      // Data must be JSON object, so scratch it if it doesn't start with `{`
      if (buffer[0] != '{') {
        LOGD("Data is not valid JSON Object");
        goto scratch_everything;
      }
      if (buffer_length < header->data_length) {
        // We do not have all data bytes, let's wait for them.
        if (processed_bytes != 0) {
          break;
        }
        return 0;
      }
      if (buffer[header->data_length - 1] != '}') {
        LOGD("Data is not valid JSON Object");
        goto scratch_everything;
      }
      const char* parse_end_ptr = NULL;
      cJSON* data_json = cJSON_ParseWithLengthOpts((const char*)buffer, header->data_length, &parse_end_ptr, false);
      // If data couldn't be parsed, or wasn't parsed til end, let's scrap this.
      if (data_json == NULL || ((uint8_t*)parse_end_ptr) != buffer + header->data_length) {
        LOGD("Failed to parse data");
        uint32_t parsed_bytes = (uint8_t*)parse_end_ptr - buffer;
        if (parse_end_ptr == NULL) {
          parsed_bytes = header->data_length;
        }
        processed_bytes += parsed_bytes;
        if (data_json != NULL) cJSON_Delete(data_json);
        dec->state = WSAT_EVENT_DECODER_PROCESS_STATE_HEADER;
        continue;
      }
      dec->wip_evt.data = data_json;
      processed_bytes += header->data_length;
      flags |= WSAT_DECODED_EVENT_FLAG_BEGIN;
      if (header->payload_length > 0) {
        dec->state = WSAT_EVENT_DECODER_PROCESS_STATE_PAYLOAD;
      } else {
        flags |= WSAT_DECODED_EVENT_FLAG_END;
        ready_to_output_event = true;
        break;
      }
    } else if (dec->state == WSAT_EVENT_DECODER_PROCESS_STATE_PAYLOAD) {
      struct wsat_event_payload_chunk* payload = &dec->wip_evt.payload;
      uint32_t payload_size_left = header->payload_length - dec->payload_received;
      uint32_t size = payload_size_left < buffer_length ? payload_size_left : buffer_length;
      // TODO: We can avoid having payload_buffer variable, if memcpy below will handle this.
      memcpy(dec->payload_buffer, buffer, size);
      payload->data = dec->payload_buffer;
      payload->size = size;
      payload->offset = dec->payload_received;
      if (dec->payload_received == 0) flags |= WSAT_DECODED_EVENT_FLAG_BEGIN;
      flags |= WSAT_DECODED_EVENT_FLAG_PAYLOAD;
      dec->payload_received += size;
      processed_bytes += size;
      ready_to_output_event = true;
      if (dec->payload_received == header->payload_length) {
        flags |= WSAT_DECODED_EVENT_FLAG_END;
      }
      break;
    }
  }

  if (processed_bytes < dec->buffer_length) {
    memcpy(dec->buffer, dec->buffer + processed_bytes, dec->buffer_length - processed_bytes);
  }
  dec->buffer_length = dec->buffer_length - processed_bytes;
  if (!ready_to_output_event) return 0;

  dec->wip_evt.flags = flags;
  *out_event = dec->wip_evt;
  if (flags & WSAT_DECODED_EVENT_FLAG_END) {
    dec->state = WSAT_EVENT_DECODER_PROCESS_STATE_HEADER;
    dec->payload_received = 0;
  }

  return 1;
scratch_everything:
  if (dec->state == WSAT_EVENT_DECODER_PROCESS_STATE_DATA) {
    cJSON_Delete(dec->wip_evt.header.json);
  }
  dec->buffer_length = 0;
  dec->state = WSAT_EVENT_DECODER_PROCESS_STATE_HEADER;
  return 0;
}

void wsat_decoded_event_free(struct wsat_decoded_event* evt)
{
  if (!(evt->flags & WSAT_DECODED_EVENT_FLAG_END)) {
    // Do nothing.
    return;
  }
  if (evt->header.data_length != 0) {
    cJSON_Delete(evt->data);
  }
  cJSON_Delete(evt->header.json);
  memset(evt, 0, sizeof(*evt));
}

// Temporary workaround for simple testing

#if 0

#include <assert.h>

void test_wsat_decoder()
{
  int32_t res = 0;
  struct wsat_decoded_event evt;
  struct wsat_event_decoder dec;
  uint8_t* buffer;
  uint32_t capacity;
#define MEMCPY_BUFFER(SRC, SIZE) { \
capacity = wsat_event_decoder_buffer_get(&dec, &buffer); \
if (capacity < SIZE) LOGD("TEST: cap %d < size %d", capacity, SIZE); \
memcpy(buffer, SRC, capacity < SIZE ? capacity : SIZE); \
wsat_event_decoder_buffer_advance(&dec, capacity < SIZE ? capacity : SIZE); }
  if (1){
    // Test initial partial header with junk on start
    wsat_event_decoder_reset(&dec);
    const char* p1 = "zxzzc{\"type\":\"test_wsat_decoder\"";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 27);
    assert(dec.buffer[0] == '{');
    // Now add rest + some junk on the end
    const char* p2 = ",\"something\": true}\nabcdefghi";
    MEMCPY_BUFFER(p2, strlen(p2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "test_wsat_decoder") == 0);
    assert(dec.buffer_length == 9);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 0);
    wsat_decoded_event_free(&evt);
  }
  if (1){
    // Test too big header at once
    wsat_event_decoder_reset(&dec);
    const char p1[EVENT_DECODER_BUFFER_SIZE] = "{\"type\":\"test_wsat_decoder\"";
    MEMCPY_BUFFER(p1, sizeof(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 0);
    // Too big header in two steps
    wsat_event_decoder_reset(&dec);
    MEMCPY_BUFFER(p1, 45);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 45);
    MEMCPY_BUFFER(p1, EVENT_DECODER_BUFFER_SIZE);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Test two JSONs between
    wsat_event_decoder_reset(&dec);
    const char* p1 = "zzzz{\"type\":\"fake\"}{\"type\":\"real\"}\n";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(dec.buffer_length == 0);
    assert(strcmp(evt.header.type, "real") == 0);
    wsat_decoded_event_free(&evt);
    // Test look-a-like JSON and real JSON
    wsat_event_decoder_reset(&dec);
    const char* p2 = "zzzzz{\"wannabejson\"}{\"type\":\"real\"}\n";
    MEMCPY_BUFFER(p2, strlen(p2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(dec.buffer_length == 0);
    assert(strcmp(evt.header.type, "real") == 0);
    wsat_decoded_event_free(&evt);
  }
  if (1){
    // Data test
    wsat_event_decoder_reset(&dec);
    dec.state = WSAT_EVENT_DECODER_PROCESS_STATE_DATA;
    dec.wip_evt.header.data_length = 10;
    const char* p1 = "{\"z\":";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 5);
    const char* p2 = "1337}";
    MEMCPY_BUFFER(p2, strlen(p2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(dec.buffer_length == 0);
    wsat_decoded_event_free(&evt);
  }
  if (1){
    // Full packet test with segmentation variation 1
    uint8_t payload[5056];
    for (int i = 0; i < sizeof(payload); i++) payload[i] = i % 256 + i / 256;
    wsat_event_decoder_reset(&dec);
    const char* p1 = "{\"type\":\"test\",";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    // assert(dec.buffer_length == strlen(p1));
    const char* p2 = "\"data_length\":18,\"payload_length\":5056}\n";
    MEMCPY_BUFFER(p2, strlen(p2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.state == WSAT_EVENT_DECODER_PROCESS_STATE_DATA);
    // assert(dec.buffer_length == strlen(p1));
    const char* p3 = "{\"somethi";
    MEMCPY_BUFFER(p3, strlen(p3));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.state == WSAT_EVENT_DECODER_PROCESS_STATE_DATA);
    const char* p4 = "ng\":true}";
    MEMCPY_BUFFER(p4, strlen(p4));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.state == WSAT_EVENT_DECODER_PROCESS_STATE_PAYLOAD);
    uint8_t payload_output[5056];
    MEMCPY_BUFFER(payload, EVENT_DECODER_BUFFER_SIZE);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(dec.state == WSAT_EVENT_DECODER_PROCESS_STATE_PAYLOAD);
    assert(dec.buffer_length == 0);
    assert(dec.payload_received == 4096);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_PAYLOAD));
    assert(evt.payload.offset == 0);
    assert(evt.payload.size == 4096);
    assert(evt.payload.data[2] == payload[2]);
    memcpy(payload_output + evt.payload.offset, evt.payload.data, evt.payload.size);
    MEMCPY_BUFFER(payload + EVENT_DECODER_BUFFER_SIZE, sizeof(payload) - EVENT_DECODER_BUFFER_SIZE);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_END | WSAT_DECODED_EVENT_FLAG_PAYLOAD));
    assert(evt.payload.offset == 4096);
    assert(evt.payload.size == 960);
    assert(dec.payload_received == 0);
    assert(dec.buffer_length == 0);
    assert(dec.state == WSAT_EVENT_DECODER_PROCESS_STATE_HEADER);
    memcpy(payload_output + evt.payload.offset, evt.payload.data, evt.payload.size);
    wsat_decoded_event_free(&evt);
    for (int i = 0; i < sizeof(payload); i++) assert(payload[i] == payload_output[i]);
  }
  if (1){
    // Simple header-only event in single chunk
    wsat_event_decoder_reset(&dec);
    const char* p1 = "{\"type\":\"simple\"}\n";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "simple") == 0);
    assert(dec.buffer_length == 0);
    wsat_decoded_event_free(&evt);
  }
  if (1){
    // Two headers back-to-back in one buffer
    wsat_event_decoder_reset(&dec);
    const char* p1 = "{\"type\":\"first\"}\n{\"type\":\"second\"}\n";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "first") == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length > 0);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "second") == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Junk + partial header, then completion
    wsat_event_decoder_reset(&dec);
    const char* p1 = "junk{\"type\":\"par";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == strlen("{\"type\":\"par"));
    assert(memcmp(dec.buffer, "{\"type\":\"par", dec.buffer_length) == 0);
    const char* p2 = "tial\"}\n";
    MEMCPY_BUFFER(p2, strlen(p2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(strcmp(evt.header.type, "partial") == 0);
    assert(dec.buffer_length == 0);
    wsat_decoded_event_free(&evt);
  }
  if (1){
    // Header + data in one buffer
    wsat_event_decoder_reset(&dec);
    const char* p1 = "{\"type\":\"data-only\",\"data_length\":10}\n{\"x\":1234}";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "data-only") == 0);
    cJSON* x = cJSON_GetObjectItem(evt.data, "x");
    assert(x != NULL && cJSON_IsNumber(x));
    assert((int)cJSON_GetNumberValue(x) == 1234);
    assert(dec.buffer_length == 0);
    wsat_decoded_event_free(&evt);
  }
  if (1){
    // Data split across chunks with next header preserved
    wsat_event_decoder_reset(&dec);
    const char* h1 = "{\"type\":\"data-chunk\",\"data_length\":12}\n";
    const char* d1 = "{\"foo\":";
    const char* d2 = "true}";
    const char* h2 = "{\"type\":\"next\"}\n";
    MEMCPY_BUFFER(h1, strlen(h1));
    MEMCPY_BUFFER(d1, strlen(d1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    MEMCPY_BUFFER(d2, strlen(d2));
    MEMCPY_BUFFER(h2, strlen(h2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "data-chunk") == 0);
    cJSON* foo = cJSON_GetObjectItem(evt.data, "foo");
    assert(foo != NULL && cJSON_IsBool(foo) && cJSON_IsTrue(foo));
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == strlen(h2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(strcmp(evt.header.type, "next") == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Header + payload in one buffer
    uint8_t payload[4] = {1, 2, 3, 4};
    wsat_event_decoder_reset(&dec);
    const char* h1 = "{\"type\":\"payload-one\",\"payload_length\":4}\n";
    MEMCPY_BUFFER(h1, strlen(h1));
    MEMCPY_BUFFER(payload, sizeof(payload));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_PAYLOAD |
                         WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "payload-one") == 0);
    assert(evt.payload.offset == 0);
    assert(evt.payload.size == sizeof(payload));
    assert(memcmp(evt.payload.data, payload, sizeof(payload)) == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Payload split into multiple chunks
    uint8_t payload[9] = {10, 11, 12, 13, 14, 15, 16, 17, 18};
    wsat_event_decoder_reset(&dec);
    const char* h1 = "{\"type\":\"payload-chunks\",\"payload_length\":9}\n";
    MEMCPY_BUFFER(h1, strlen(h1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    MEMCPY_BUFFER(payload, 2);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_PAYLOAD));
    assert(evt.payload.offset == 0);
    assert(evt.payload.size == 2);
    assert(memcmp(evt.payload.data, payload, 2) == 0);
    MEMCPY_BUFFER(payload + 2, 3);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == WSAT_DECODED_EVENT_FLAG_PAYLOAD);
    assert(evt.payload.offset == 2);
    assert(evt.payload.size == 3);
    assert(memcmp(evt.payload.data, payload + 2, 3) == 0);
    MEMCPY_BUFFER(payload + 5, 4);
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_PAYLOAD | WSAT_DECODED_EVENT_FLAG_END));
    assert(evt.payload.offset == 5);
    assert(evt.payload.size == 4);
    assert(memcmp(evt.payload.data, payload + 5, 4) == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.state == WSAT_EVENT_DECODER_PROCESS_STATE_HEADER);
  }
  if (1){
    // Skip invalid header and parse the next one
    wsat_event_decoder_reset(&dec);
    const char* p1 = "{\"type\":123}\n{\"type\":\"good\"}\n";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(strcmp(evt.header.type, "good") == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Data length too big should discard header
    wsat_event_decoder_reset(&dec);
    const char* p1 = "{\"type\":\"big-data\",\"data_length\":4097}\n";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Header + data + payload in one buffer
    uint8_t payload[4] = {21, 22, 23, 24};
    wsat_event_decoder_reset(&dec);
    const char* h1 = "{\"type\":\"data-payload\",\"data_length\":7,\"payload_length\":4}\n";
    const char* data = "{\"a\":1}";
    MEMCPY_BUFFER(h1, strlen(h1));
    MEMCPY_BUFFER(data, strlen(data));
    MEMCPY_BUFFER(payload, sizeof(payload));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_PAYLOAD |
                         WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "data-payload") == 0);
    cJSON* a = cJSON_GetObjectItem(evt.data, "a");
    assert(a != NULL && cJSON_IsNumber(a));
    assert((int)cJSON_GetNumberValue(a) == 1);
    assert(evt.payload.offset == 0);
    assert(evt.payload.size == sizeof(payload));
    assert(memcmp(evt.payload.data, payload, sizeof(payload)) == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Known bug: payload chunk data should remain valid when next header follows
    uint8_t payload[3] = {'a', 'b', 'c'};
    wsat_event_decoder_reset(&dec);
    const char* h1 = "{\"type\":\"payload-next\",\"payload_length\":3}\n";
    const char* h2 = "{\"type\":\"after\",\"very_long\":\"aaaaaaaaaaaaaaaaaaaaaaaa\"}\n";
    MEMCPY_BUFFER(h1, strlen(h1));
    MEMCPY_BUFFER(payload, sizeof(payload));
    MEMCPY_BUFFER(h2, strlen(h2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(evt.flags == (WSAT_DECODED_EVENT_FLAG_BEGIN | WSAT_DECODED_EVENT_FLAG_PAYLOAD |
                         WSAT_DECODED_EVENT_FLAG_END));
    assert(strcmp(evt.header.type, "payload-next") == 0);
    assert(memcmp(evt.payload.data, payload, sizeof(payload)) == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == strlen(h2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(strcmp(evt.header.type, "after") == 0);
    wsat_decoded_event_free(&evt);
    assert(dec.buffer_length == 0);
  }
  if (1){
    // Header start split across chunks should be handled
    wsat_event_decoder_reset(&dec);
    const char* p1 = "{";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    const char* p2 = "\"type\":\"split\"}\n";
    MEMCPY_BUFFER(p2, strlen(p2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(strcmp(evt.header.type, "split") == 0);
    wsat_decoded_event_free(&evt);
  }
  if (1) {
    // Header starts one byte before with junk on start
    wsat_event_decoder_reset(&dec);
    const char* p1 = "junk{";
    MEMCPY_BUFFER(p1, strlen(p1));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 0);
    assert(dec.buffer_length == 1);
    const char* p2 = "\"type\":\"test\"}\n";
    MEMCPY_BUFFER(p2, strlen(p2));
    res = wsat_event_decoder_next(&dec, &evt);
    assert(res == 1);
    assert(strcmp(evt.header.type, "test") == 0);
    wsat_decoded_event_free(&evt);
  }
}

#endif
