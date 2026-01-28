#include <wyoming/satellite.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "wyoming_user.h"

// region Microphone test impl

static pthread_t mic_thread;
static atomic_int mic_enabled = ATOMIC_VAR_INIT(0);
static atomic_int mic_play_audio = ATOMIC_VAR_INIT(0);

static void* mic_thread_fn(void* opaque)
{
  static uint8_t audio_buffer[2048];
  static FILE* audio_file = NULL;
  while (atomic_load(&mic_enabled)) {
    memset(audio_buffer, 0x0, sizeof(audio_buffer));
    if (atomic_load(&mic_play_audio)) {
      if (audio_file == NULL) {
#if 1
        audio_file = fopen("only-turn-on-light.raw", "rb");
#else
        audio_file = fopen("test-turn-on-the-light.raw", "rb");
#endif
      }
      size_t read_size = fread(audio_buffer, 1, 2048, audio_file);
      if (read_size <= 0) {
        atomic_store(&mic_play_audio, 0);
        fclose(audio_file);
        audio_file = NULL;
        printf("Test audio finished\n");
      }
    }
    wsat_mic_write_data(audio_buffer, sizeof(audio_buffer));
    usleep(64 * 1000);
  }
  return NULL;
}

static int32_t mic_start_stream()
{
  atomic_store(&mic_enabled, 1);
  pthread_create(&mic_thread, NULL, mic_thread_fn, NULL);
  return 0;
}

static int32_t mic_stop_stream()
{
  if (atomic_load(&mic_enabled)) {
    atomic_store(&mic_enabled, 0);
    pthread_join(mic_thread, NULL);
  }
  return 0;
}

struct wsat_microphone mic = {
  {
    WSAT_COMPONENT_TYPE_MICROPHONE,
    mic_start_stream,
    mic_stop_stream,
    NULL,
    false,
  },
  16000,
  2,
  1,
};

// endregion

// region Speaker test impl

static FILE* snd_out_file = NULL;

static int32_t snd_handle_sys_event(enum wsat_sys_event_type type, void* data)
{
  switch (type) {
  case WSAT_SYS_EVENT_SND_AUDIO_START: {
    struct wsat_sys_event_audio_start_params* info = data;
    static uint32_t i = 0;
    char snd_file_name[50];
    snprintf(snd_file_name, sizeof(snd_file_name), "snd_%d_%d_%d_%d.bin", i++,
      info->rate, info->width, info->channels);
    snd_out_file = fopen(snd_file_name, "wb");
    break;
  }
  case WSAT_SYS_EVENT_SND_AUDIO_DATA: {
    struct wsat_sys_event_buffer_params* buffer = data;
    fwrite(buffer->data, 1, buffer->size, snd_out_file);
    break;
  }
  case WSAT_SYS_EVENT_SND_AUDIO_END: {
    if (snd_out_file != NULL) {
      fclose(snd_out_file);
    }
    snd_out_file = NULL;
  }
  default: break;
  }
}

static struct wsat_sound snd = {
  {
    WSAT_COMPONENT_TYPE_SOUND,
    NULL,
    NULL,
    snd_handle_sys_event,
    true,
  }
};
// endregion

// region Wake test impl

static struct wsat_wake wake = {
  {
    WSAT_COMPONENT_TYPE_WAKE,
    NULL,
    NULL,
    NULL,
    true,
  },
  "test",
};

// endregion

void* terminal_thread_fn(void* opaque)
{
  while (1) {
    int ch = getchar();
    if (ch == 'l') {
      atomic_store(&mic_play_audio, 1);
    } else if (ch == 'q') {
      printf("Stopping the server\n");
      wsat_stop();
      break;
    } else if (ch == 'w') {
      wsat_wake_detection();
    }
  }
  return NULL;
}

int main(int argc, char** argv)
{
  // Temporary workaround for tests
#if 0
  extern void test_wsat_decoder();
  test_wsat_decoder();
  return 0;
#endif
  pthread_t terminal_thread;
  pthread_create(&terminal_thread, NULL, terminal_thread_fn, NULL);
  wsat_init();
  wsat_mic_set(&mic);
  wsat_snd_set(&snd);
  wsat_wake_set(&wake);
  wsat_run();
  pthread_join(terminal_thread, NULL);
  wsat_destroy();
}

void debug_print(char type, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  printf("[%c] ", type);
  vprintf(format, args);
  printf("\n");
  va_end(args);
}