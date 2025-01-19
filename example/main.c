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
static atomic_int mic_play_audio = ATOMIC_VAR_INIT(0);

static void* mic_thread_fn(void* opaque)
{
  static uint8_t audio_buffer[2048];
  static FILE* audio_file = NULL;
  memset(audio_buffer, 0x00, sizeof(audio_buffer));
  while (atomic_load(&mic_enabled)) {
    memset(audio_buffer, 0x0, sizeof(audio_buffer));
    if (atomic_load(&mic_play_audio)) {
      if (audio_file == NULL) {
        audio_file = fopen("test-turn-on-the-light.raw", "rb");
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
    pthread_join(mic_thread, NULL);
  }
  atomic_store(&mic_enabled, 0);
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

// region Speaker test impl

static FILE* snd_out_file = NULL;

static int32_t snd_start_stream(uint32_t rate, uint8_t width, uint8_t channels)
{
  static uint32_t i = 0;
  char snd_file_name[50];
  snprintf(snd_file_name, sizeof(snd_file_name), "snd_%d_%d_%d_%d.bin", i++, rate, width, channels);
  snd_out_file = fopen(snd_file_name, "wb");
  return 0;
}

static int32_t snd_stop_stream()
{
  if (snd_out_file != NULL) {
    fclose(snd_out_file);
  }
  snd_out_file = NULL;
  return 0;
}

static int32_t snd_on_data(uint8_t* data, uint32_t length)
{
  fwrite(data, 1, length, snd_out_file);
  return 0;
}

static struct wsat_sound snd = {
  NULL,
  NULL,
  snd_start_stream,
  snd_on_data,
  snd_stop_stream,
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
    }
  }
  return NULL;
}

int main(int argc, char** argv)
{
  pthread_t terminal_thread;
  pthread_create(&terminal_thread, NULL, terminal_thread_fn, NULL);
  wsat_mic_set(&mic);
  wsat_snd_set(&snd);
  wsat_run();
  pthread_join(terminal_thread, NULL);
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