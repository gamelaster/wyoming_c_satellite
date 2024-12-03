#ifndef WYOMING_USER_H_
#define WYOMING_USER_H_

// System libraries
#include <pthread.h>

// Include required libraries
#include <cJSON.h>
#include <sys/socket.h> // POSIX Sockets
#include <netinet/in.h> // POSIX Sockets
#include <unistd.h> // For Sockets

// Logging implementation

void debug_print(char type, const char *format, ...);

#define LOGD(...) debug_print('D', __VA_ARGS__)
#define LOGE(...) debug_print('E', __VA_ARGS__)

// Platform related macros

#define PLAT_THREAD_TYPE pthread_t
#define PLAT_THREAD_CREATE(thread, start_routine, name, stack_size, priority) pthread_create(thread, NULL, start_routine, NULL)
#define PLAT_THREAD_JOIN(thread) pthread_join(*thread, NULL)

#define PLAT_MUTEX_TYPE pthread_mutex_t
#define PLAT_MUTEX_CREATE(mutex) pthread_mutex_init(mutex, NULL)
#define PLAT_MUTEX_DESTROY(mutex) pthread_mutex_destroy(mutex)
#define PLAT_MUTEX_LOCK(mutex) pthread_mutex_lock(mutex)
#define PLAT_MUTEX_UNLOCK(mutex) pthread_mutex_unlock(mutex)


#endif