#ifndef WYOMING_SATELLITE_H_
#define WYOMING_SATELLITE_H_

#include <stdint.h>
#include <cJSON.h>
#include <stdbool.h>

enum wsat_error
{
  WSAT_OK,
  WSAT_ERROR_SOCKET,
  WSAT_ERROR_SAT_DISCONNECTED
};

enum wsat_decoded_event_flags
{
  WSAT_DECODED_EVENT_FLAG_BEGIN = 1 << 0,
  WSAT_DECODED_EVENT_FLAG_PAYLOAD = 1 << 1,
  WSAT_DECODED_EVENT_FLAG_END = 1 << 2,
};

enum wsat_component_type
{
  WSAT_COMPONENT_TYPE_MODE,
  WSAT_COMPONENT_TYPE_MICROPHONE,
  WSAT_COMPONENT_TYPE_SOUND,
  WSAT_COMPONENT_TYPE_WAKE,
};

struct wsat_decoded_event
{
  uint8_t flags; // enum wsat_decoded_event_flags
  struct wsat_event_header
  {
    cJSON* json;
    const char* type;
    cJSON* data;
    uint32_t data_length;
    uint32_t payload_length;
  } header;
  cJSON* data;
  struct wsat_event_payload_chunk
  {
    uint8_t* data;
    uint32_t offset;
    uint32_t size;
  } payload;
};

struct wsat_event
{
  cJSON* header;
  cJSON* data;
  uint8_t* payload;
  uint16_t payload_length;
};

enum wsat_sys_event_type
{
  WSAT_SYS_EVENT_SAT_CONNECT,
  WSAT_SYS_EVENT_SAT_DISCONNECT,
  WSAT_SYS_EVENT_MIC_DATA,
  WSAT_SYS_EVENT_SND_AUDIO_START,
  WSAT_SYS_EVENT_SND_AUDIO_DATA,
  WSAT_SYS_EVENT_SND_AUDIO_END,
  WSAT_SYS_EVENT_WAKE_DETECTION,
};

struct wsat_sys_event_buffer_params
{
  void* data;
  uint32_t size;
};

struct wsat_sys_event_audio_start_params
{
  uint32_t rate;
  uint8_t width;
  uint8_t channels;
};

// Component is service in Python implementation
struct wsat_component
{
  enum wsat_component_type type;
  int32_t (* init_fn)();
  int32_t (* destroy_fn)();
  int32_t (* sys_event_handle_fn)(enum wsat_sys_event_type type, void* data);
  bool is_init;
};

struct wsat_microphone
{
  struct wsat_component comp;
  uint32_t rate;
  uint8_t width;
  uint8_t channels;
};

struct wsat_sound
{
  struct wsat_component comp;
};

struct wsat_wake
{
  struct wsat_component comp;
  const char* name;
};

int32_t wsat_init();
void wsat_destroy();
int32_t wsat_run();
void wsat_stop();
void wsat_mic_set(struct wsat_microphone* mic);
void wsat_snd_set(struct wsat_sound* snd);
void wsat_wake_set(struct wsat_wake* wake);
void wsat_mic_write_data(uint8_t* data, uint32_t length);
bool wsat_server_is_connected();
void wsat_wake_detection();

int32_t wsat_event_send(struct wsat_event* evt);
void wsat_event_free(struct wsat_event* evt, bool free_payload);

#endif