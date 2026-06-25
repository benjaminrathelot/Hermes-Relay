#include "hermes/platform.h"

#ifdef _WIN32

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

typedef struct hermes_win_dir_state {
    struct _finddata_t data;
} hermes_win_dir_state;

static hermes_platform_service_signals *hermes_win_signals = NULL;
static LONG hermes_win_wsa_ready = 0;

static BOOL WINAPI hermes_win_ctrl_handler(DWORD ctrl_type) {
    if (!hermes_win_signals) {
        return FALSE;
    }
    if (ctrl_type == CTRL_C_EVENT ||
        ctrl_type == CTRL_BREAK_EVENT ||
        ctrl_type == CTRL_CLOSE_EVENT ||
        ctrl_type == CTRL_SHUTDOWN_EVENT) {
        hermes_win_signals->stop_requested = 1;
        return TRUE;
    }
    return FALSE;
}

hermes_status hermes_platform_init(void) {
    if (InterlockedCompareExchange(&hermes_win_wsa_ready, 1, 0) == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            InterlockedExchange(&hermes_win_wsa_ready, 0);
            return HERMES_ERR_IO;
        }
    }
    return HERMES_OK;
}

void hermes_platform_shutdown(void) {
    if (InterlockedCompareExchange(&hermes_win_wsa_ready, 0, 1) == 1) {
        WSACleanup();
    }
}

hermes_status hermes_platform_install_service_signals(hermes_platform_service_signals *signals) {
    if (!signals) {
        return HERMES_ERR_ARGUMENT;
    }
    hermes_win_signals = signals;
    return SetConsoleCtrlHandler(hermes_win_ctrl_handler, TRUE) ? HERMES_OK : HERMES_ERR_IO;
}

unsigned long hermes_platform_process_id(void) {
    return (unsigned long) _getpid();
}

int hermes_platform_process_alive(unsigned long pid) {
    HANDLE handle;
    DWORD wait_result;
    if (pid == 0ul) {
        return 0;
    }
    handle = OpenProcess(SYNCHRONIZE, FALSE, (DWORD) pid);
    if (!handle) {
        return 0;
    }
    wait_result = WaitForSingleObject(handle, 0);
    CloseHandle(handle);
    return wait_result == WAIT_TIMEOUT;
}

int hermes_platform_path_exists(const char *path) {
    struct _stat st;
    return path && _stat(path, &st) == 0;
}

int hermes_platform_is_regular_file(const char *path) {
    struct _stat st;
    if (!path || _stat(path, &st) != 0) {
        return 0;
    }
    return (st.st_mode & _S_IFREG) != 0;
}

int hermes_platform_mkdir(const char *path) {
    return _mkdir(path);
}

int hermes_platform_remove_file(const char *path) {
    return _unlink(path);
}

int hermes_platform_rename(const char *src, const char *dst) {
    return rename(src, dst);
}

hermes_status hermes_platform_open_exclusive(const char *path, hermes_fd *fd) {
    if (!path || !fd) {
        return HERMES_ERR_ARGUMENT;
    }
    *fd = (hermes_fd) _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (*fd < 0) {
        return errno == EEXIST ? HERMES_ERR_DUPLICATE : HERMES_ERR_IO;
    }
    return HERMES_OK;
}

int hermes_platform_close_fd(hermes_fd fd) {
    return _close((int) fd);
}

int hermes_platform_write_fd(hermes_fd fd, const void *data, size_t len) {
    return _write((int) fd, data, (unsigned int) len);
}

hermes_status hermes_dir_open(const char *path, hermes_dir_iter *iter) {
    hermes_win_dir_state *state = NULL;
    int written;
    if (!path || !iter) {
        return HERMES_ERR_ARGUMENT;
    }
    memset(iter, 0, sizeof(*iter));
    written = snprintf(iter->pattern, sizeof(iter->pattern), "%s/*", path);
    if (written < 0 || (size_t) written >= sizeof(iter->pattern)) {
        return HERMES_ERR_RANGE;
    }
    state = (hermes_win_dir_state *) calloc(1u, sizeof(*state));
    if (!state) {
        return HERMES_ERR_MEMORY;
    }
    iter->handle = _findfirst(iter->pattern, &state->data);
    if (iter->handle < 0) {
        free(state);
        return HERMES_ERR_IO;
    }
    iter->native_data = state;
    iter->first_ready = 1;
    iter->active = 1;
    return HERMES_OK;
}

int hermes_dir_next(hermes_dir_iter *iter, hermes_dir_entry *entry) {
    if (!iter || !entry || !iter->active || !iter->native_data) {
        return -1;
    }
    for (;;) {
        hermes_win_dir_state *state = (hermes_win_dir_state *) iter->native_data;
        const struct _finddata_t *data = &state->data;
        if (!iter->first_ready) {
            if (_findnext(iter->handle, &state->data) != 0) {
                return 0;
            }
            data = &state->data;
        } else {
            iter->first_ready = 0;
        }
        if (strcmp(data->name, ".") == 0 || strcmp(data->name, "..") == 0) {
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        if (strlen(data->name) >= sizeof(entry->name)) {
            return -1;
        }
        memcpy(entry->name, data->name, strlen(data->name) + 1u);
        entry->is_regular = ((data->attrib & _A_SUBDIR) == 0);
        return 1;
    }
}

void hermes_dir_close(hermes_dir_iter *iter) {
    if (!iter) {
        return;
    }
    if (iter->active) {
        _findclose(iter->handle);
        iter->active = 0;
    }
    free(iter->native_data);
    iter->native_data = NULL;
}

static hermes_status hermes_win_socket_parse(const char *host,
                                             const char *port,
                                             int passive,
                                             hermes_socket_handle *fd,
                                             int set_reuseaddr) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it;
    SOCKET socket_fd = INVALID_SOCKET;
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
        BOOL yes = 1;
        socket_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_fd == INVALID_SOCKET) {
            continue;
        }
        if (set_reuseaddr) {
            (void) setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof(yes));
        }
        if (passive) {
            if (bind(socket_fd, it->ai_addr, (int) it->ai_addrlen) == 0 &&
                listen(socket_fd, 16) == 0) {
                break;
            }
        } else if (connect(socket_fd, it->ai_addr, (int) it->ai_addrlen) == 0) {
            break;
        }
        closesocket(socket_fd);
        socket_fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (socket_fd == INVALID_SOCKET) {
        return HERMES_ERR_IO;
    }
    *fd = (hermes_socket_handle) socket_fd;
    return HERMES_OK;
}

hermes_status hermes_socket_open_listener(const char *host, const char *port, hermes_socket_handle *listen_fd) {
    return hermes_win_socket_parse(host, port, 1, listen_fd, 1);
}

hermes_status hermes_socket_connect(const char *host, const char *port, hermes_socket_handle *fd) {
    return hermes_win_socket_parse(host, port, 0, fd, 0);
}

int hermes_socket_wait_readable(hermes_socket_handle fd, uint32_t timeout_ms) {
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET((SOCKET) fd, &rfds);
    tv.tv_sec = (long) (timeout_ms / 1000u);
    tv.tv_usec = (long) ((timeout_ms % 1000u) * 1000u);
    return select(0, &rfds, NULL, NULL, &tv) > 0 && FD_ISSET((SOCKET) fd, &rfds);
}

hermes_status hermes_socket_accept(hermes_socket_handle listen_fd, hermes_socket_handle *client_fd) {
    SOCKET fd;
    if (!client_fd) {
        return HERMES_ERR_ARGUMENT;
    }
    fd = accept((SOCKET) listen_fd, NULL, NULL);
    if (fd == INVALID_SOCKET) {
        return HERMES_ERR_IO;
    }
    *client_fd = (hermes_socket_handle) fd;
    return HERMES_OK;
}

intptr_t hermes_socket_send_bytes(hermes_socket_handle fd, const void *data, size_t len) {
    return (intptr_t) send((SOCKET) fd, (const char *) data, (int) len, 0);
}

intptr_t hermes_socket_recv_bytes(hermes_socket_handle fd, void *data, size_t len) {
    return (intptr_t) recv((SOCKET) fd, (char *) data, (int) len, 0);
}

void hermes_socket_close(hermes_socket_handle fd) {
    if (fd >= 0) {
        closesocket((SOCKET) fd);
    }
}

int hermes_socket_set_reuseaddr(hermes_socket_handle fd) {
    BOOL yes = 1;
    return setsockopt((SOCKET) fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof(yes));
}

#endif
