#include <wyoming/satellite.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

// region Microphone test impl

static pthread_t mic_thread;
static atomic_int mic_enabled = ATOMIC_VAR_INIT(0);

static void* mic_thread_fn(void* opaque)
{
  static uint8_t audio_buffer[2048];
  memset(audio_buffer, 0x00, sizeof(audio_buffer));
  while (atomic_load(&mic_enabled)) {
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
  atomic_store(&mic_enabled, 0);
  pthread_join(mic_thread, NULL);
  return 0;
}

struct wsat_microphone mic = {
  16000,
  2,
  1,
  NULL,
  NULL,
  mic_start_stream,
  mic_stop_stream
};

// endregion

int main(int argc, char** argv)
{
  wsat_mic_set(&mic);
  wsat_run();
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