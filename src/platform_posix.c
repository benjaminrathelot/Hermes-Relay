#include "hermes/platform.h"

#ifndef _WIN32

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static hermes_platform_service_signals *hermes_posix_signals = NULL;

static void hermes_posix_signal_handler(int signo) {
    if (!hermes_posix_signals) {
        return;
    }
    if (signo == SIGTERM || signo == SIGINT) {
        hermes_posix_signals->stop_requested = 1;
    } else if (signo == SIGHUP) {
        hermes_posix_signals->reopen_requested = 1;
    }
}

hermes_status hermes_platform_init(void) {
    return HERMES_OK;
}

void hermes_platform_shutdown(void) {
}

hermes_status hermes_platform_install_service_signals(hermes_platform_service_signals *signals) {
    if (!signals) {
        return HERMES_ERR_ARGUMENT;
    }
    hermes_posix_signals = signals;
    signal(SIGTERM, hermes_posix_signal_handler);
    signal(SIGINT, hermes_posix_signal_handler);
#ifdef SIGHUP
    signal(SIGHUP, hermes_posix_signal_handler);
#endif
    return HERMES_OK;
}

unsigned long hermes_platform_process_id(void) {
    return (unsigned long) getpid();
}

int hermes_platform_process_alive(unsigned long pid) {
    if (pid == 0ul) {
        return 0;
    }
    return kill((pid_t) pid, 0) == 0 || errno == EPERM;
}

int hermes_platform_path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

int hermes_platform_is_regular_file(const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) != 0;
}

int hermes_platform_mkdir(const char *path) {
    return mkdir(path, 0700);
}

int hermes_platform_remove_file(const char *path) {
    return unlink(path);
}

int hermes_platform_rename(const char *src, const char *dst) {
    return rename(src, dst);
}

hermes_status hermes_platform_open_exclusive(const char *path, hermes_fd *fd) {
    if (!path || !fd) {
        return HERMES_ERR_ARGUMENT;
    }
    *fd = (hermes_fd) open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (*fd < 0) {
        return errno == EEXIST ? HERMES_ERR_DUPLICATE : HERMES_ERR_IO;
    }
    return HERMES_OK;
}

int hermes_platform_close_fd(hermes_fd fd) {
    return close((int) fd);
}

int hermes_platform_write_fd(hermes_fd fd, const void *data, size_t len) {
    return (int) write((int) fd, data, len);
}

hermes_status hermes_dir_open(const char *path, hermes_dir_iter *iter) {
    if (!path || !iter) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(iter, 0, sizeof(*iter));
    iter->dir = opendir(path);
    return iter->dir ? HERMES_OK : HERMES_ERR_IO;
}

int hermes_dir_next(hermes_dir_iter *iter, hermes_dir_entry *entry) {
    for (;;) {
        struct dirent *dir_entry;
        if (!iter || !entry || !iter->dir) {
            return -1;
        }
        dir_entry = readdir((DIR *) iter->dir);
        if (!dir_entry) {
            return 0;
        }
        if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0) {
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        if (strlen(dir_entry->d_name) >= sizeof(entry->name)) {
            return -1;
        }
        memcpy(entry->name, dir_entry->d_name, strlen(dir_entry->d_name) + 1u);
        entry->is_regular = 1;
        return 1;
    }
}

void hermes_dir_close(hermes_dir_iter *iter) {
    if (iter && iter->dir) {
        closedir((DIR *) iter->dir);
        iter->dir = NULL;
    }
}

static hermes_status hermes_posix_socket_parse(const char *host,
                                               const char *port,
                                               int passive,
                                               hermes_socket_handle *fd,
                                               int set_reuseaddr) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it;
    hermes_socket_handle socket_fd = (hermes_socket_handle) -1;
    if (!host || !port || !fd) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }
    if (getaddrinfo((*host == '\0' || strcmp(host, "*") == 0) ? NULL : host, port, &hints, &res) != 0) {
        return HERMES_ERR_IO;
    }
    for (it = res; it != NULL; it = it->ai_next) {
        int yes = 1;
        socket_fd = (hermes_socket_handle) socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }
        if (set_reuseaddr) {
            (void) setsockopt((int) socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        }
        if (passive) {
            if (bind((int) socket_fd, it->ai_addr, (socklen_t) it->ai_addrlen) == 0 &&
                listen((int) socket_fd, 16) == 0) {
                break;
            }
        } else if (connect((int) socket_fd, it->ai_addr, (socklen_t) it->ai_addrlen) == 0) {
            break;
        }
        close((int) socket_fd);
        socket_fd = (hermes_socket_handle) -1;
    }
    freeaddrinfo(res);
    if (socket_fd < 0) {
        return HERMES_ERR_IO;
    }
    *fd = socket_fd;
    return HERMES_OK;
}

hermes_status hermes_socket_open_listener(const char *host, const char *port, hermes_socket_handle *listen_fd) {
    return hermes_posix_socket_parse(host, port, 1, listen_fd, 1);
}

hermes_status hermes_socket_connect(const char *host, const char *port, hermes_socket_handle *fd) {
    return hermes_posix_socket_parse(host, port, 0, fd, 0);
}

int hermes_socket_wait_readable(hermes_socket_handle fd, uint32_t timeout_ms) {
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET((int) fd, &rfds);
    tv.tv_sec = (long) (timeout_ms / 1000u);
    tv.tv_usec = (long) ((timeout_ms % 1000u) * 1000u);
    return select((int) fd + 1, &rfds, NULL, NULL, &tv) > 0 && FD_ISSET((int) fd, &rfds);
}

hermes_status hermes_socket_accept(hermes_socket_handle listen_fd, hermes_socket_handle *client_fd) {
    hermes_socket_handle fd;
    if (!client_fd) {
        return HERMES_ERR_ARGUMENT;
    }
    fd = (hermes_socket_handle) accept((int) listen_fd, NULL, NULL);
    if (fd < 0) {
        return HERMES_ERR_IO;
    }
    *client_fd = fd;
    return HERMES_OK;
}

intptr_t hermes_socket_send_bytes(hermes_socket_handle fd, const void *data, size_t len) {
    return (intptr_t) send((int) fd, data, len, 0);
}

intptr_t hermes_socket_recv_bytes(hermes_socket_handle fd, void *data, size_t len) {
    return (intptr_t) recv((int) fd, data, len, 0);
}

void hermes_socket_close(hermes_socket_handle fd) {
    if (fd >= 0) {
        close((int) fd);
    }
}

int hermes_socket_set_reuseaddr(hermes_socket_handle fd) {
    int yes = 1;
    return setsockopt((int) fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
}

#endif
