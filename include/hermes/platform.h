#ifndef HERMES_PLATFORM_H
#define HERMES_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

typedef intptr_t hermes_fd;
typedef intptr_t hermes_socket_handle;

typedef struct hermes_platform_service_signals {
    volatile int stop_requested;
    volatile int reopen_requested;
} hermes_platform_service_signals;

typedef struct hermes_dir_entry {
    char name[512];
    int is_regular;
} hermes_dir_entry;

typedef struct hermes_dir_iter {
#ifdef _WIN32
    intptr_t handle;
    int first_ready;
    int active;
    char pattern[1024];
    void *native_data;
#else
    void *dir;
#endif
} hermes_dir_iter;

hermes_status hermes_platform_init(void);
void hermes_platform_shutdown(void);
hermes_status hermes_platform_install_service_signals(hermes_platform_service_signals *signals);

unsigned long hermes_platform_process_id(void);
int hermes_platform_process_alive(unsigned long pid);

int hermes_platform_path_exists(const char *path);
int hermes_platform_is_regular_file(const char *path);
int hermes_platform_mkdir(const char *path);
int hermes_platform_remove_file(const char *path);
int hermes_platform_rename(const char *src, const char *dst);

hermes_status hermes_platform_open_exclusive(const char *path, hermes_fd *fd);
int hermes_platform_close_fd(hermes_fd fd);
int hermes_platform_write_fd(hermes_fd fd, const void *data, size_t len);

hermes_status hermes_dir_open(const char *path, hermes_dir_iter *iter);
int hermes_dir_next(hermes_dir_iter *iter, hermes_dir_entry *entry);
void hermes_dir_close(hermes_dir_iter *iter);

hermes_status hermes_socket_open_listener(const char *host, const char *port, hermes_socket_handle *listen_fd);
hermes_status hermes_socket_connect(const char *host, const char *port, hermes_socket_handle *fd);
int hermes_socket_wait_readable(hermes_socket_handle fd, uint32_t timeout_ms);
hermes_status hermes_socket_accept(hermes_socket_handle listen_fd, hermes_socket_handle *client_fd);
intptr_t hermes_socket_send_bytes(hermes_socket_handle fd, const void *data, size_t len);
intptr_t hermes_socket_recv_bytes(hermes_socket_handle fd, void *data, size_t len);
void hermes_socket_close(hermes_socket_handle fd);
int hermes_socket_set_reuseaddr(hermes_socket_handle fd);

#endif
