#include "hermes/transport.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hermes/bundle.h"
#include "hermes/envelope.h"
#include "hermes/platform.h"
#include "hermes/sync.h"
#include "hermes/util.h"

#define HERMES_OP_HELLO 0u
#define HERMES_SYNC_MAGIC "HSY1"
#define HERMES_OP_INVENTORY 1u
#define HERMES_OP_REQUEST 2u
#define HERMES_OP_ENVELOPES 3u
#define HERMES_MAX_INVENTORY_IDS 65535u
#define HERMES_MAX_FRAME_SIZE (8u * 1024u * 1024u)

static hermes_status hermes_hello_payload_encode(const char *advertise_addr,
                                                 uint8_t **payload,
                                                 uint32_t *payload_len) {
    size_t addr_len = advertise_addr ? strlen(advertise_addr) : 0u;
    uint8_t *buffer;
    if (!payload || !payload_len) {
        return HERMES_ERR_ARGUMENT;
    }
    if (addr_len > 127u) {
        return HERMES_ERR_RANGE;
    }
    buffer = (uint8_t *) malloc(1u + addr_len);
    if (!buffer) {
        return HERMES_ERR_MEMORY;
    }
    buffer[0] = (uint8_t) addr_len;
    if (addr_len > 0) {
        memcpy(buffer + 1u, advertise_addr, addr_len);
    }
    *payload = buffer;
    *payload_len = (uint32_t) (1u + addr_len);
    return HERMES_OK;
}

static hermes_status hermes_hello_payload_decode(const uint8_t *payload,
                                                 uint32_t payload_len,
                                                 char *peer_addr,
                                                 size_t peer_addr_len) {
    size_t addr_len;
    if (!payload || payload_len < 1u) {
        return HERMES_ERR_ARGUMENT;
    }
    addr_len = payload[0];
    if (payload_len != 1u + addr_len) {
        return HERMES_ERR_FORMAT;
    }
    if (!peer_addr || peer_addr_len == 0u) {
        return HERMES_OK;
    }
    if (addr_len >= peer_addr_len) {
        return HERMES_ERR_RANGE;
    }
    if (addr_len > 0) {
        memcpy(peer_addr, payload + 1u, addr_len);
    }
    peer_addr[addr_len] = '\0';
    return HERMES_OK;
}

static hermes_status hermes_send_all(hermes_socket_handle fd, const uint8_t *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        intptr_t rc = hermes_socket_send_bytes(fd, data + sent, len - sent);
        if (rc <= 0) {
            return HERMES_ERR_IO;
        }
        sent += (size_t) rc;
    }
    return HERMES_OK;
}

static hermes_status hermes_recv_all(hermes_socket_handle fd, uint8_t *data, size_t len) {
    size_t got = 0;
    while (got < len) {
        intptr_t rc = hermes_socket_recv_bytes(fd, data + got, len - got);
        if (rc <= 0) {
            return HERMES_ERR_IO;
        }
        got += (size_t) rc;
    }
    return HERMES_OK;
}

static hermes_status hermes_send_frame(hermes_socket_handle fd,
                                       uint8_t opcode,
                                       const uint8_t *payload,
                                       uint32_t payload_len) {
    uint8_t header[9];
    memcpy(header, HERMES_SYNC_MAGIC, 4u);
    header[4] = opcode;
    hermes_write_be32(header + 5u, payload_len);
    if (hermes_send_all(fd, header, sizeof(header)) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    if (payload_len > 0 && hermes_send_all(fd, payload, payload_len) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    return HERMES_OK;
}

static hermes_status hermes_recv_frame(hermes_socket_handle fd,
                                       uint8_t *opcode,
                                       uint8_t **payload,
                                       uint32_t *payload_len) {
    uint8_t header[9];
    uint8_t *buffer = NULL;
    uint32_t len;
    if (!opcode || !payload || !payload_len) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_recv_all(fd, header, sizeof(header)) != HERMES_OK) {
        return HERMES_ERR_IO;
    }
    if (memcmp(header, HERMES_SYNC_MAGIC, 4u) != 0) {
        return HERMES_ERR_FORMAT;
    }
    len = hermes_read_be32(header + 5u);
    if (len > HERMES_MAX_FRAME_SIZE) {
        return HERMES_ERR_RANGE;
    }
    if (len > 0) {
        buffer = (uint8_t *) malloc(len);
        if (!buffer) {
            return HERMES_ERR_MEMORY;
        }
        if (hermes_recv_all(fd, buffer, len) != HERMES_OK) {
            free(buffer);
            return HERMES_ERR_IO;
        }
    }
    *opcode = header[4];
    *payload = buffer;
    *payload_len = len;
    return HERMES_OK;
}

static hermes_status hermes_inventory_payload_decode(const uint8_t *payload,
                                                     uint32_t payload_len,
                                                     uint8_t **ids,
                                                     size_t *count) {
    uint32_t n;
    if (!payload || payload_len < 4u || !ids || !count) {
        return HERMES_ERR_ARGUMENT;
    }
    n = hermes_read_be32(payload);
    if (payload_len != 4u + ((size_t) n * HERMES_ID_LEN)) {
        return HERMES_ERR_FORMAT;
    }
    *ids = (uint8_t *) malloc((size_t) n * HERMES_ID_LEN);
    if (n > 0 && !*ids) {
        return HERMES_ERR_MEMORY;
    }
    if (n > 0) {
        memcpy(*ids, payload + 4u, (size_t) n * HERMES_ID_LEN);
    }
    *count = n;
    return HERMES_OK;
}

static hermes_status hermes_inventory_payload_encode(const uint8_t *ids,
                                                     size_t count,
                                                     uint8_t **payload,
                                                     uint32_t *payload_len) {
    size_t len = 4u + (count * HERMES_ID_LEN);
    uint8_t *buffer;
    if ((!ids && count > 0) || !payload || !payload_len || count > HERMES_MAX_INVENTORY_IDS) {
        return HERMES_ERR_ARGUMENT;
    }
    buffer = (uint8_t *) malloc(len);
    if (!buffer) {
        return HERMES_ERR_MEMORY;
    }
    hermes_write_be32(buffer, (uint32_t) count);
    if (count > 0) {
        memcpy(buffer + 4u, ids, count * HERMES_ID_LEN);
    }
    *payload = buffer;
    *payload_len = (uint32_t) len;
    return HERMES_OK;
}

static hermes_status hermes_collect_requested(hermes_store *store,
                                              const uint8_t *ids,
                                              size_t count,
                                              hermes_envelope **envs,
                                              size_t *env_count) {
    hermes_envelope *items;
    size_t accepted = 0;
    size_t i;
    if ((!ids && count > 0) || !envs || !env_count) {
        return HERMES_ERR_ARGUMENT;
    }
    items = (hermes_envelope *) calloc(count == 0 ? 1u : count, sizeof(*items));
    if (!items) {
        return HERMES_ERR_MEMORY;
    }
    for (i = 0; i < count; ++i) {
        if (hermes_store_get_envelope(store, ids + (i * HERMES_ID_LEN), &items[accepted]) == HERMES_OK &&
            hermes_store_can_forward(store, &items[accepted], (uint64_t) hermes_now_utc())) {
            ++accepted;
        }
    }
    *envs = items;
    *env_count = accepted;
    return HERMES_OK;
}

static hermes_status hermes_import_bundle_bytes(hermes_store *store, const uint8_t *bundle, uint32_t bundle_len, uint64_t now_unix) {
    hermes_envelope *envs = NULL;
    size_t env_count = 0;
    size_t i;
    hermes_status status = hermes_bundle_decode_envelopes(bundle, bundle_len, &envs, &env_count);
    if (status != HERMES_OK) {
        return status;
    }
    for (i = 0; i < env_count; ++i) {
        (void) hermes_store_add_envelope(store, &envs[i], now_unix);
    }
    free(envs);
    return HERMES_OK;
}

static hermes_status hermes_bundle_to_frame_payload(const hermes_envelope *envs,
                                                    size_t env_count,
                                                    uint8_t **payload,
                                                    uint32_t *payload_len) {
    size_t raw_len = 0;
    hermes_status status;
    if ((!envs && env_count > 0) || !payload || !payload_len) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_bundle_encode_envelopes(envs, env_count, payload, &raw_len);
    if (status != HERMES_OK) {
        return status;
    }
    if (raw_len > UINT32_MAX) {
        free(*payload);
        *payload = NULL;
        return HERMES_ERR_RANGE;
    }
    *payload_len = (uint32_t) raw_len;
    return HERMES_OK;
}

static hermes_status hermes_parse_listen_addr(const char *listen_addr, char *host, size_t host_len, char *port, size_t port_len) {
    const char *colon;
    size_t host_part_len;
    if (!listen_addr || !host || !port) {
        return HERMES_ERR_ARGUMENT;
    }
    colon = strrchr(listen_addr, ':');
    if (!colon) {
        return HERMES_ERR_FORMAT;
    }
    host_part_len = (size_t) (colon - listen_addr);
    if (host_part_len >= host_len || strlen(colon + 1u) >= port_len) {
        return HERMES_ERR_RANGE;
    }
    memcpy(host, listen_addr, host_part_len);
    host[host_part_len] = '\0';
    memcpy(port, colon + 1u, strlen(colon + 1u) + 1u);
    return HERMES_OK;
}

static hermes_status hermes_tcp_exchange_hello(hermes_socket_handle fd,
                                               const char *advertise_addr,
                                               char *claimed_peer_addr,
                                               size_t claimed_peer_addr_len) {
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    uint8_t opcode = 0;
    uint8_t *frame = NULL;
    uint32_t frame_len = 0;
    hermes_status status;

    status = hermes_hello_payload_encode(advertise_addr, &payload, &payload_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_HELLO, payload, payload_len);
    free(payload);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK) {
        free(frame);
        return status;
    }
    if (opcode != HERMES_OP_HELLO) {
        free(frame);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_hello_payload_decode(frame, frame_len, claimed_peer_addr, claimed_peer_addr_len);
    free(frame);
    return status;
}

static hermes_status hermes_tcp_exchange_hello_server(hermes_socket_handle fd,
                                                      const char *advertise_addr,
                                                      char *claimed_peer_addr,
                                                      size_t claimed_peer_addr_len) {
    uint8_t opcode = 0;
    uint8_t *frame = NULL;
    uint32_t frame_len = 0;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    hermes_status status;

    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK) {
        free(frame);
        return status;
    }
    if (opcode != HERMES_OP_HELLO) {
        free(frame);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_hello_payload_decode(frame, frame_len, claimed_peer_addr, claimed_peer_addr_len);
    free(frame);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_hello_payload_encode(advertise_addr, &payload, &payload_len);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_HELLO, payload, payload_len);
    free(payload);
    return status;
}

static hermes_status hermes_tcp_handle_client(hermes_socket_handle fd,
                                              hermes_store *store,
                                              const char *advertise_addr,
                                              char *claimed_peer_addr,
                                              size_t claimed_peer_addr_len);

hermes_status hermes_transport_tcp_open_listener(const char *listen_addr, hermes_socket_handle *listen_fd) {
    char host[128];
    char port[32];
    if (!listen_addr || !listen_fd) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_parse_listen_addr(listen_addr, host, sizeof(host), port, sizeof(port)) != HERMES_OK) {
        return HERMES_ERR_FORMAT;
    }
    return hermes_socket_open_listener(host, port, listen_fd);
}

int hermes_transport_tcp_wait_listener(hermes_socket_handle listen_fd, uint32_t timeout_ms) {
    return hermes_socket_wait_readable(listen_fd, timeout_ms);
}

void hermes_transport_tcp_close_listener(hermes_socket_handle listen_fd) {
    hermes_socket_close(listen_fd);
}

hermes_status hermes_transport_tcp_accept_once(hermes_socket_handle listen_fd, hermes_store *store) {
    return hermes_transport_tcp_accept_once_peer(listen_fd, store, NULL, NULL, 0u);
}

hermes_status hermes_transport_tcp_accept_once_peer(hermes_socket_handle listen_fd,
                                                    hermes_store *store,
                                                    const char *advertise_addr,
                                                    char *peer_addr,
                                                    size_t peer_addr_len) {
    hermes_socket_handle client_fd = (hermes_socket_handle) -1;
    hermes_status status;
    if (listen_fd < 0 || !store) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_socket_accept(listen_fd, &client_fd);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_tcp_handle_client(client_fd, store, advertise_addr, peer_addr, peer_addr_len);
    hermes_socket_close(client_fd);
    return status;
}

static hermes_status hermes_tcp_handle_client(hermes_socket_handle fd,
                                              hermes_store *store,
                                              const char *advertise_addr,
                                              char *claimed_peer_addr,
                                              size_t claimed_peer_addr_len) {
    uint8_t *frame = NULL;
    uint8_t opcode;
    uint32_t frame_len = 0;
    uint8_t *client_ids = NULL;
    uint8_t *server_ids = NULL;
    uint8_t *request_ids = NULL;
    size_t client_count = 0;
    size_t server_count = 0;
    size_t request_count = 0;
    hermes_envelope *envs = NULL;
    size_t env_count = 0;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    hermes_status status;

    status = hermes_tcp_exchange_hello_server(fd, advertise_addr, claimed_peer_addr, claimed_peer_addr_len);
    if (status != HERMES_OK) {
        return status;
    }

    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK || opcode != HERMES_OP_INVENTORY) {
        free(frame);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_inventory_payload_decode(frame, frame_len, &client_ids, &client_count);
    free(frame);
    frame = NULL;
    if (status != HERMES_OK) {
        return status;
    }

    server_ids = (uint8_t *) malloc(HERMES_MAX_INVENTORY_IDS * HERMES_ID_LEN);
    if (!server_ids) {
        free(client_ids);
        return HERMES_ERR_MEMORY;
    }
    status = hermes_store_list_inventory(store, server_ids, HERMES_MAX_INVENTORY_IDS, &server_count);
    if (status != HERMES_OK) {
        free(client_ids);
        free(server_ids);
        return status;
    }
    status = hermes_inventory_payload_encode(server_ids, server_count, &payload, &payload_len);
    if (status != HERMES_OK) {
        free(client_ids);
        free(server_ids);
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_INVENTORY, payload, payload_len);
    free(payload);
    payload = NULL;
    if (status != HERMES_OK) {
        free(client_ids);
        free(server_ids);
        return status;
    }

    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK || opcode != HERMES_OP_REQUEST) {
        free(client_ids);
        free(server_ids);
        free(frame);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_inventory_payload_decode(frame, frame_len, &request_ids, &request_count);
    free(frame);
    frame = NULL;
    if (status != HERMES_OK) {
        free(client_ids);
        free(server_ids);
        return status;
    }
    status = hermes_collect_requested(store, request_ids, request_count, &envs, &env_count);
    free(request_ids);
    if (status != HERMES_OK) {
        free(client_ids);
        free(server_ids);
        return status;
    }
    status = hermes_bundle_to_frame_payload(envs, env_count, &payload, &payload_len);
    free(envs);
    if (status != HERMES_OK) {
        free(client_ids);
        free(server_ids);
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_ENVELOPES, payload, payload_len);
    free(payload);
    payload = NULL;
    if (status != HERMES_OK) {
        free(client_ids);
        free(server_ids);
        return status;
    }

    request_ids = (uint8_t *) malloc(client_count * HERMES_ID_LEN);
    if (!request_ids && client_count > 0) {
        free(client_ids);
        free(server_ids);
        return HERMES_ERR_MEMORY;
    }
    status = hermes_inventory_diff(server_ids,
                                   server_count,
                                   client_ids,
                                   client_count,
                                   request_ids,
                                   client_count,
                                   &request_count);
    free(client_ids);
    free(server_ids);
    if (status != HERMES_OK) {
        free(request_ids);
        return status;
    }
    status = hermes_inventory_payload_encode(request_ids, request_count, &payload, &payload_len);
    free(request_ids);
    if (status != HERMES_OK) {
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_REQUEST, payload, payload_len);
    free(payload);
    payload = NULL;
    if (status != HERMES_OK) {
        return status;
    }

    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK || opcode != HERMES_OP_ENVELOPES) {
        free(frame);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_import_bundle_bytes(store, frame, frame_len, (uint64_t) hermes_now_utc());
    free(frame);
    frame = NULL;
    return status;
}

static hermes_status hermes_tcp_serve(hermes_store *store, const char *listen_addr) {
    hermes_socket_handle server_fd = (hermes_socket_handle) -1;
    hermes_status status;
    if (!store || !listen_addr) {
        return HERMES_ERR_ARGUMENT;
    }
    status = hermes_transport_tcp_open_listener(listen_addr, &server_fd);
    if (status != HERMES_OK) {
        return status;
    }
    for (;;) {
        status = hermes_transport_tcp_accept_once_peer(server_fd, store, listen_addr, NULL, 0u);
        if (status != HERMES_OK) {
            hermes_transport_tcp_close_listener(server_fd);
            return status;
        }
    }
}

hermes_status hermes_transport_tcp_sync_peer_claim(hermes_store *store,
                                                   const char *peer_addr,
                                                   uint64_t now_unix,
                                                   const char *advertise_addr,
                                                   char *claimed_peer_addr,
                                                   size_t claimed_peer_addr_len) {
    char host[128];
    char port[32];
    hermes_socket_handle fd = (hermes_socket_handle) -1;
    uint8_t *local_ids = NULL;
    uint8_t *remote_ids = NULL;
    uint8_t *request_ids = NULL;
    size_t local_count = 0;
    size_t remote_count = 0;
    size_t request_count = 0;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    uint8_t opcode;
    uint8_t *frame = NULL;
    uint32_t frame_len = 0;
    hermes_envelope *envs = NULL;
    size_t env_count = 0;
    hermes_status status;

    if (!store || !peer_addr) {
        return HERMES_ERR_ARGUMENT;
    }
    if (hermes_parse_listen_addr(peer_addr, host, sizeof(host), port, sizeof(port)) != HERMES_OK) {
        return HERMES_ERR_FORMAT;
    }
    status = hermes_socket_connect(host, port, &fd);
    if (status != HERMES_OK) {
        return status;
    }

    status = hermes_tcp_exchange_hello(fd, advertise_addr, claimed_peer_addr, claimed_peer_addr_len);
    if (status != HERMES_OK) {
        hermes_socket_close(fd);
        return status;
    }

    local_ids = (uint8_t *) malloc(HERMES_MAX_INVENTORY_IDS * HERMES_ID_LEN);
    if (!local_ids) {
        hermes_socket_close(fd);
        return HERMES_ERR_MEMORY;
    }
    status = hermes_store_list_inventory(store, local_ids, HERMES_MAX_INVENTORY_IDS, &local_count);
    if (status != HERMES_OK) {
        free(local_ids);
        hermes_socket_close(fd);
        return status;
    }
    status = hermes_inventory_payload_encode(local_ids, local_count, &payload, &payload_len);
    if (status != HERMES_OK) {
        free(local_ids);
        hermes_socket_close(fd);
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_INVENTORY, payload, payload_len);
    free(payload);
    payload = NULL;
    if (status != HERMES_OK) {
        free(local_ids);
        hermes_socket_close(fd);
        return status;
    }

    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK || opcode != HERMES_OP_INVENTORY) {
        free(local_ids);
        free(frame);
        hermes_socket_close(fd);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_inventory_payload_decode(frame, frame_len, &remote_ids, &remote_count);
    free(frame);
    frame = NULL;
    if (status != HERMES_OK) {
        free(local_ids);
        hermes_socket_close(fd);
        return status;
    }

    request_ids = (uint8_t *) malloc(remote_count * HERMES_ID_LEN);
    if (!request_ids && remote_count > 0) {
        free(local_ids);
        free(remote_ids);
        hermes_socket_close(fd);
        return HERMES_ERR_MEMORY;
    }
    status = hermes_inventory_diff(local_ids,
                                   local_count,
                                   remote_ids,
                                   remote_count,
                                   request_ids,
                                   remote_count,
                                   &request_count);
    if (status != HERMES_OK) {
        free(local_ids);
        free(remote_ids);
        free(request_ids);
        hermes_socket_close(fd);
        return status;
    }
    status = hermes_inventory_payload_encode(request_ids, request_count, &payload, &payload_len);
    free(request_ids);
    if (status != HERMES_OK) {
        free(local_ids);
        free(remote_ids);
        hermes_socket_close(fd);
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_REQUEST, payload, payload_len);
    free(payload);
    payload = NULL;
    if (status != HERMES_OK) {
        free(local_ids);
        free(remote_ids);
        hermes_socket_close(fd);
        return status;
    }

    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK || opcode != HERMES_OP_ENVELOPES) {
        free(local_ids);
        free(remote_ids);
        free(frame);
        hermes_socket_close(fd);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_import_bundle_bytes(store, frame, frame_len, now_unix);
    free(frame);
    frame = NULL;
    if (status != HERMES_OK) {
        free(local_ids);
        free(remote_ids);
        hermes_socket_close(fd);
        return status;
    }

    status = hermes_recv_frame(fd, &opcode, &frame, &frame_len);
    if (status != HERMES_OK || opcode != HERMES_OP_REQUEST) {
        free(local_ids);
        free(remote_ids);
        free(frame);
        hermes_socket_close(fd);
        return HERMES_ERR_PROTOCOL;
    }
    status = hermes_inventory_payload_decode(frame, frame_len, &request_ids, &request_count);
    free(frame);
    frame = NULL;
    if (status != HERMES_OK) {
        free(local_ids);
        free(remote_ids);
        hermes_socket_close(fd);
        return status;
    }
    status = hermes_collect_requested(store, request_ids, request_count, &envs, &env_count);
    free(request_ids);
    free(local_ids);
    free(remote_ids);
    if (status != HERMES_OK) {
        hermes_socket_close(fd);
        return status;
    }
    status = hermes_bundle_to_frame_payload(envs, env_count, &payload, &payload_len);
    free(envs);
    if (status != HERMES_OK) {
        hermes_socket_close(fd);
        return status;
    }
    status = hermes_send_frame(fd, HERMES_OP_ENVELOPES, payload, payload_len);
    free(payload);
    payload = NULL;
    hermes_socket_close(fd);
    return status;
}

static hermes_status hermes_tcp_sync_peer(hermes_store *store, const char *peer_addr, uint64_t now_unix) {
    return hermes_transport_tcp_sync_peer_claim(store, peer_addr, now_unix, NULL, NULL, 0u);
}

static const hermes_transport_ops hermes_tcp_ops = {
    "tcp",
    hermes_tcp_serve,
    hermes_tcp_sync_peer
};

const hermes_transport_ops *hermes_transport_tcp(void) {
    return &hermes_tcp_ops;
}
