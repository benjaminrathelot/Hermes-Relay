#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hermes/platform.h"
#include "hermes/version.h"

typedef struct sim_message {
    uint32_t id;
    uint32_t src;
    uint32_t dst;
    uint32_t created_day;
    uint32_t expires_day;
    uint32_t pow_difficulty;
    uint16_t size;
    int spam;
} sim_message;

typedef struct sim_node {
    uint32_t *msg_ids;
    size_t count;
    size_t capacity;
} sim_node;

typedef struct sim_config {
    uint32_t nodes;
    uint32_t days;
    uint32_t legit_per_day;
    uint32_t spam_per_day;
    uint32_t contacts_per_day;
    uint32_t node_store_cap;
    uint32_t min_pow;
    uint32_t rng_seed;
} sim_config;

static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t rnd_u32(uint64_t *state, uint32_t max_exclusive) {
    return (uint32_t) (splitmix64_next(state) % max_exclusive);
}

static int node_has(const sim_node *node, uint32_t msg_id) {
    size_t i;
    for (i = 0; i < node->count; ++i) {
        if (node->msg_ids[i] == msg_id) {
            return 1;
        }
    }
    return 0;
}

static double message_score(const sim_message *msg, uint32_t now_day) {
    uint32_t age = now_day > msg->created_day ? now_day - msg->created_day : 0u;
    return (double) msg->pow_difficulty * 1000.0 - (double) age - ((double) msg->size / 16.0);
}

static void node_store(sim_node *node,
                       const sim_message *messages,
                       uint32_t msg_id,
                       uint32_t now_day,
                       size_t cap) {
    size_t i;
    if (node_has(node, msg_id)) {
        return;
    }
    if (node->count < cap) {
        node->msg_ids[node->count++] = msg_id;
        return;
    }
    {
        size_t worst = 0;
        double worst_score = message_score(&messages[node->msg_ids[0]], now_day);
        double new_score = message_score(&messages[msg_id], now_day);
        for (i = 1; i < node->count; ++i) {
            double score = message_score(&messages[node->msg_ids[i]], now_day);
            if (score < worst_score) {
                worst = i;
                worst_score = score;
            }
        }
        if (new_score > worst_score) {
            node->msg_ids[worst] = msg_id;
        }
    }
}

static void node_exchange(sim_node *a,
                          sim_node *b,
                          const sim_message *messages,
                          uint32_t now_day,
                          size_t cap,
                          uint32_t min_pow) {
    size_t i;
    size_t a_count = a->count;
    size_t b_count = b->count;
    for (i = 0; i < a_count; ++i) {
        const sim_message *msg = &messages[a->msg_ids[i]];
        if (msg->pow_difficulty >= min_pow && msg->expires_day >= now_day) {
            node_store(b, messages, a->msg_ids[i], now_day, cap);
        }
    }
    for (i = 0; i < b_count; ++i) {
        const sim_message *msg = &messages[b->msg_ids[i]];
        if (msg->pow_difficulty >= min_pow && msg->expires_day >= now_day) {
            node_store(a, messages, b->msg_ids[i], now_day, cap);
        }
    }
}

static void usage(void) {
    puts("usage: hermes-sim [--nodes N] [--days D] [--legit-per-day N] [--spam-per-day N]");
    puts("                  [--contacts-per-day N] [--store-cap N] [--min-pow N] [--seed N]");
}

int main(int argc, char **argv) {
    int exit_code = 1;
    sim_config cfg = {100u, 10u, 8u, 16u, 200u, 256u, 18u, 1u};
    sim_node *nodes = NULL;
    sim_message *messages = NULL;
    uint32_t max_messages;
    uint32_t message_count = 0;
    uint32_t delivered = 0;
    uint32_t legit_total = 0;
    uint32_t spam_total = 0;
    uint64_t rng;
    uint32_t day;
    int i;

    if (hermes_platform_init() != HERMES_OK) {
        fprintf(stderr, "platform initialization failure\n");
        return 1;
    }
    for (i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            usage();
            goto cleanup;
        }
        if (strcmp(argv[i], "--nodes") == 0) cfg.nodes = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (strcmp(argv[i], "--days") == 0) cfg.days = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (strcmp(argv[i], "--legit-per-day") == 0) cfg.legit_per_day = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (strcmp(argv[i], "--spam-per-day") == 0) cfg.spam_per_day = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (strcmp(argv[i], "--contacts-per-day") == 0) cfg.contacts_per_day = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (strcmp(argv[i], "--store-cap") == 0) cfg.node_store_cap = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (strcmp(argv[i], "--min-pow") == 0) cfg.min_pow = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else if (strcmp(argv[i], "--seed") == 0) cfg.rng_seed = (uint32_t) strtoul(argv[i + 1], NULL, 10);
        else {
            usage();
            goto cleanup;
        }
    }

    max_messages = cfg.days * (cfg.legit_per_day + cfg.spam_per_day) + 1u;
    nodes = (sim_node *) calloc(cfg.nodes, sizeof(*nodes));
    messages = (sim_message *) calloc(max_messages, sizeof(*messages));
    if (!nodes || !messages) {
        fprintf(stderr, "allocation failure\n");
        free(nodes);
        free(messages);
        goto cleanup;
    }
    for (i = 0; i < (int) cfg.nodes; ++i) {
        nodes[i].msg_ids = (uint32_t *) calloc(cfg.node_store_cap, sizeof(uint32_t));
        if (!nodes[i].msg_ids) {
            fprintf(stderr, "allocation failure\n");
            goto cleanup;
        }
        nodes[i].capacity = cfg.node_store_cap;
    }

    rng = cfg.rng_seed;
    for (day = 0; day < cfg.days; ++day) {
        uint32_t j;
        for (j = 0; j < cfg.legit_per_day; ++j) {
            sim_message *msg = &messages[message_count];
            msg->id = message_count;
            msg->src = rnd_u32(&rng, cfg.nodes);
            do {
                msg->dst = rnd_u32(&rng, cfg.nodes);
            } while (msg->dst == msg->src);
            msg->created_day = day;
            msg->expires_day = day + (HERMES_MAX_TTL_SECONDS / 86400u);
            msg->pow_difficulty = cfg.min_pow + rnd_u32(&rng, 6u);
            msg->size = (uint16_t) (48u + rnd_u32(&rng, 160u));
            msg->spam = 0;
            node_store(&nodes[msg->src], messages, message_count, day, cfg.node_store_cap);
            ++message_count;
            ++legit_total;
        }
        for (j = 0; j < cfg.spam_per_day; ++j) {
            sim_message *msg = &messages[message_count];
            msg->id = message_count;
            msg->src = rnd_u32(&rng, cfg.nodes);
            msg->dst = rnd_u32(&rng, cfg.nodes);
            msg->created_day = day;
            msg->expires_day = day + (HERMES_MAX_TTL_SECONDS / 86400u);
            msg->pow_difficulty = rnd_u32(&rng, cfg.min_pow + 2u);
            msg->size = (uint16_t) (128u + rnd_u32(&rng, 256u));
            msg->spam = 1;
            node_store(&nodes[msg->src], messages, message_count, day, cfg.node_store_cap);
            ++message_count;
            ++spam_total;
        }
        for (j = 0; j < cfg.contacts_per_day; ++j) {
            uint32_t a = rnd_u32(&rng, cfg.nodes);
            uint32_t b = rnd_u32(&rng, cfg.nodes);
            if (a != b) {
                node_exchange(&nodes[a], &nodes[b], messages, day, cfg.node_store_cap, cfg.min_pow);
            }
        }
    }

    for (i = 0; i < (int) legit_total; ++i) {
        if (node_has(&nodes[messages[i].dst], messages[i].id)) {
            ++delivered;
        }
    }

    printf("nodes=%u days=%u legit=%u spam=%u contacts_per_day=%u store_cap=%u min_pow=%u\n",
           cfg.nodes,
           cfg.days,
           legit_total,
           spam_total,
           cfg.contacts_per_day,
           cfg.node_store_cap,
           cfg.min_pow);
    printf("delivery_probability=%.4f delivered=%u/%u\n",
           legit_total ? (double) delivered / (double) legit_total : 0.0,
           delivered,
           legit_total);

    exit_code = 0;
cleanup:
    if (nodes) {
        for (i = 0; i < (int) cfg.nodes; ++i) {
            free(nodes[i].msg_ids);
        }
    }
    free(nodes);
    free(messages);
    hermes_platform_shutdown();
    return exit_code;
}
