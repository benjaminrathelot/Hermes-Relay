#ifndef HERMES_TRANSPORT_H
#define HERMES_TRANSPORT_H

#include <stdint.h>

#include "platform.h"
#include "status.h"
#include "store.h"

typedef struct hermes_transport_ops {
    const char *name;
    hermes_status (*serve)(hermes_store *store, const char *listen_addr);
    hermes_status (*sync_peer)(hermes_store *store, const char *peer_addr, uint64_t now_unix);
} hermes_transport_ops;

const hermes_transport_ops *hermes_transport_tcp(void);
hermes_status hermes_transport_tcp_open_listener(const char *listen_addr, hermes_socket_handle *listen_fd);
int hermes_transport_tcp_wait_listener(hermes_socket_handle listen_fd, uint32_t timeout_ms);
hermes_status hermes_transport_tcp_accept_once(hermes_socket_handle listen_fd, hermes_store *store);
hermes_status hermes_transport_tcp_accept_once_peer(hermes_socket_handle listen_fd,
                                                    hermes_store *store,
                                                    const char *advertise_addr,
                                                    char *peer_addr,
                                                    size_t peer_addr_len);
hermes_status hermes_transport_tcp_sync_peer_claim(hermes_store *store,
                                                   const char *peer_addr,
                                                   uint64_t now_unix,
                                                   const char *advertise_addr,
                                                   char *claimed_peer_addr,
                                                   size_t claimed_peer_addr_len);
void hermes_transport_tcp_close_listener(hermes_socket_handle listen_fd);

#endif
