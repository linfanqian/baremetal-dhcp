/*
 * dhcp_bench.c — Burst-mode benchmark comparing all DHCP lease store designs.
 *
 * Scenario
 * ────────
 * A "burst" is N DHCP_DISCOVER packets arriving back-to-back with no
 * inter-arrival gap, simulating a network segment where many clients
 * power-on simultaneously (e.g. a classroom, a rack reboot).
 *
 * For each lease-store implementation the benchmark:
 *   1. Initialises a fresh pool instance.
 *   2. Feeds BURST_SIZE synthetic DISCOVER messages through the pool's
 *      allocator, recording wall-clock time.
 *   3. Reports total time, average latency per DISCOVER→OFFER, and
 *      throughput in offers/second.
 *
 * Implementations compared
 * ────────────────────────
 *  dhcp_server  — dhcp_array linear-scan array keyed by MAC; O(N) lookup.
 *  dhcp_bitmap  — dhcp_bitmap_unitime range-bitmap pool; O(1) alloc.
 *  dhcp_vartime — dhcp_bitmap_vartime single-range pool with sliding window.
 *  dhcp_hashmap — dhcp_hashmap MAC→IP hash table; O(1) lookup, O(N) IP scan.
 *  dhcp_nprc    — No-Per-Request-Context bitmap cache; O(1) offer + O(1) ack.
 *
 * Build (from bench/)
 * ───────────────────
 *   make          # builds dhcp_bench with -O2
 *   ./dhcp_bench
 *
 * Compiler flags of interest (set in Makefile)
 * ─────────────────────────────────────────────
 *   -DDHCP_BITMAP_MAX_RANGES=4       number of bitmap ranges per pool
 *   -DDHCP_BITMAP_RANGE_SIZE=256     IPs per range (keep <= pool size)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/*
 * Include dhcp_server.h WITHOUT any DHCP_LEASE_MODE_* flag.
 * dhcp_server_t will only contain dhcp_config_t config (no pool member).
 * We still get all shared message utilities: dhcp_add_option,
 * dhcp_get_message_type, dhcp_build_offer, dhcp_build_nak, etc.
 */
#include "dhcp_server.h"

/* Pull in all pool implementations directly. */
#include "dhcp_array.h"
#include "dhcp_bitmap_ops.h"      /* defines DHCP_BITMAP_RANGE_SIZE / MAX_RANGES */
#include "dhcp_bitmap_unitime.h"
#include "dhcp_bitmap_vartime.h"
#include "dhcp_hashmap.h"         /* requires -I../ds-lib for hash.h */
#include "dhcp_nprc.h"

/* ─────────────────────────────────────────────────────────────────────────
 * Tunable parameters
 * ───────────────────────────────────────────────────────────────────────── */

/* IP pool: 10.0.0.1 – 10.255.255.254  (/8, 16 777 214 addresses) */
#define POOL_START  0x0A000001u   /* 10.0.0.1       */
#define POOL_END    0x0AFFFFFEu   /* 10.255.255.254  */
#define POOL_SIZE   ((POOL_END) - (POOL_START) + 1u)   /* 16,777,214 */

/* Lease parameters */
#define LEASE_TIME  3600u   /* seconds (1 hour) */

/* Burst sizes to sweep */
static const uint32_t BURST_SIZES[] = { 16, 64, 128, 254, 300 };
#define NUM_BURST_SIZES (sizeof(BURST_SIZES) / sizeof(BURST_SIZES[0]))

/* Number of repetitions per (impl, burst-size) pair for stable timing */
#define REPS  20

/* Memory limit used to flag OOM in pool-size comparisons */
#define MEM_LIMIT_BYTES  (1ULL * 1024 * 1024)   /* 1 MB */

/*
 * Pool-size sweep: burst = pool_size (fill the pool completely).
 *
 * ARRAY cost analysis (full-fill):
 *   kth DISCOVER with k leases committed: O(k²) inner work (nested loop).
 *   Sum over k = 0..N-1 → O(N³/6) total.  Filling 2× the pool ≈ 8× slower.
 *
 * BITMAP cost: O(1) per DISCOVER regardless of fill level → O(N) total.
 *
 * IMPORTANT: each successful ARRAY offer commits the lease via
 * dhcp_arraypool_alloc_lease() so the array actually fills up and the
 * O(N²)-per-DISCOVER scan penalty is visible.
 *
 * 1 rep only — ARRAY at N=512 takes ~80ms; N=1024 ~640ms.
 */
#define REPS_POOL  1
#define NUM_POOL_BENCH_SIZES (sizeof(POOL_BENCH_SIZES) / sizeof(POOL_BENCH_SIZES[0]))

/* ─────────────────────────────────────────────────────────────────────────
 * Timing helpers
 * ───────────────────────────────────────────────────────────────────────── */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Synthetic DHCP DISCOVER builder
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * Build a minimal DHCP DISCOVER for client index `idx`.
 * Each client gets a unique MAC so the ARRAY implementation sees distinct
 * clients (important for its MAC-keyed lookup path).
 */
static void make_discover(dhcp_message_t *msg, uint32_t idx) {
    memset(msg, 0, sizeof(*msg));
    msg->op           = 1;                  /* BOOTREQUEST */
    msg->htype        = 1;                  /* Ethernet    */
    msg->hlen         = 6;
    msg->xid          = 0xBEEF0000u | idx; /* unique transaction ID */
    msg->magic_cookie = DHCP_MAGIC_COOKIE;

    /* Unique MAC: DE:AD:00:<idx byte 2>:<idx byte 1>:<idx byte 0> */
    msg->chaddr[0] = 0xDE;
    msg->chaddr[1] = 0xAD;
    msg->chaddr[2] = (uint8_t)((idx >> 16) & 0xFF);
    msg->chaddr[3] = (uint8_t)((idx >>  8) & 0xFF);
    msg->chaddr[4] = (uint8_t)( idx        & 0xFF);
    msg->chaddr[5] = 0x00;

    uint8_t type = DHCP_DISCOVER;
    dhcp_add_option(msg, DHCP_OPT_MESSAGE_TYPE, 1, &type);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Shared server config builder
 * ───────────────────────────────────────────────────────────────────────── */

static dhcp_config_t make_config(void) {
    dhcp_config_t cfg;
    cfg.server_ip   = 0xC0A80101u;  /* 192.168.1.1  */
    cfg.subnet_mask = 0xFFFFFF00u;  /* /24           */
    cfg.gateway_ip  = 0xC0A80101u;  /* 192.168.1.1  */
    cfg.dns_ip      = 0x08080808u;  /* 8.8.8.8      */
    cfg.pool_start  = POOL_START;
    cfg.pool_end    = POOL_END;
    cfg.lease_time  = LEASE_TIME;
    return cfg;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Result accumulator
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t burst_size;
    uint32_t offers_sent;   /* how many OFFERs the server actually produced */
    double   total_ns;      /* wall time for the whole burst (best of REPS) */
    double   avg_ns;        /* total_ns / burst_size  (latency per DISCOVER) */
    double   throughput;    /* offers/second */
} bench_result_t;

/* ─────────────────────────────────────────────────────────────────────────
 * dhcp_server (ARRAY) benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_server(uint32_t burst_size) {
    /* Allocate only as many lease slots as the burst can produce.
     * POOL_SIZE is now /8 (16 M IPs) — a static array would be ~448 MB. */
    dhcp_lease_t    *leases = (dhcp_lease_t *)malloc(burst_size * sizeof(dhcp_lease_t));
    dhcp_arraypool_t pool;
    dhcp_server_t    server;    /* holds only config (no mode flag set) */
    dhcp_message_t   req, resp;
    dhcp_config_t    cfg = make_config();
    uint32_t          sim_time = 0;  /* simulated clock; stays 0 during burst */

    if (!leases) {
        bench_result_t oom = { burst_size, UINT32_MAX, 0.0, 0.0, 0.0 };
        return oom;
    }

    server.config = cfg;

    uint64_t best   = UINT64_MAX;
    uint32_t offers = 0;

    for (int rep = 0; rep < REPS; rep++) {
        /* Fresh pool each rep so it is always empty at burst start */
        dhcp_arraypool_init(&pool, cfg.pool_start, leases, (uint16_t)burst_size);
        offers = 0;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));

            uint32_t offered_ip = dhcp_arraypool_find_available_ip(
                &pool, cfg.pool_start, cfg.pool_end, req.chaddr, sim_time);
            if (offered_ip) {
                dhcp_build_offer(&server, &req, &resp, offered_ip);
                dhcp_arraypool_alloc_lease(&pool, offered_ip, req.chaddr, LEASE_TIME, sim_time);
                offers++;
            }
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    free(leases);

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = offers;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

/* ─────────────────────────────────────────────────────────────────────────
 * dhcp_bitmap (BITMAP_UNITIME) benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_bitmap(uint32_t burst_size) {
    dhcp_bmpool_uni_t pool;
    dhcp_server_t     server;   /* holds only config (no mode flag set) */
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg      = make_config();
    uint32_t          sim_time = 0;  /* simulated clock; stays 0 during burst */

    server.config = cfg;

    uint64_t best   = UINT64_MAX;
    uint32_t offers = 0;

    for (int rep = 0; rep < REPS; rep++) {
        dhcp_bmpool_uni_init(&pool, cfg.pool_start, cfg.lease_time);
        offers = 0;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));

            uint32_t offered_ip = dhcp_bmpool_uni_peek(&pool, sim_time);
            if (offered_ip != 0) {
                dhcp_build_offer(&server, &req, &resp, offered_ip);
                dhcp_bmpool_uni_commit_ip(&pool, offered_ip);
                offers++;
            }
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = offers;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}


/* ─────────────────────────────────────────────────────────────────────────
 * dhcp_vartime (BITMAP_VARTIME) benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_vartime(uint32_t burst_size) {
    dhcp_bmpool_var_t pool;
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg      = make_config();
    uint32_t          sim_time = 0;
    uint32_t          lt;        /* actual lease time from vartime peek */

    server.config = cfg;

    uint64_t best   = UINT64_MAX;
    uint32_t offers = 0;

    for (int rep = 0; rep < REPS; rep++) {
        dhcp_bmpool_var_init(&pool, cfg.pool_start, cfg.lease_time);
        offers = 0;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));

            uint32_t offered_ip = dhcp_bmpool_var_peek(&pool, sim_time, &lt);
            if (offered_ip != 0) {
                dhcp_build_offer(&server, &req, &resp, offered_ip);
                dhcp_bmpool_var_commit_ip(&pool, offered_ip);
                offers++;
            }
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = offers;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

/* ─────────────────────────────────────────────────────────────────────────
 * dhcp_hashmap benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_hashmap(uint32_t burst_size) {
    /* dhcp_hashpool_t embeds a struct hash_tab with HASH_N=4096 buckets
     * (~86 KB).  Heap-allocate to avoid blowing the default stack frame. */
    dhcp_hashpool_t  *pool = (dhcp_hashpool_t *)malloc(sizeof(dhcp_hashpool_t));
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg      = make_config();
    uint32_t          sim_time = 0;

    if (!pool) {
        bench_result_t oom = { burst_size, UINT32_MAX, 0.0, 0.0, 0.0 };
        return oom;
    }

    server.config = cfg;

    uint64_t best   = UINT64_MAX;
    uint32_t offers = 0;

    for (int rep = 0; rep < REPS; rep++) {
        dhcp_hashpool_init(pool, cfg.pool_start, HASH_N * 4 / 5);
        offers = 0;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));

            uint32_t offered_ip = dhcp_hashpool_find_available_ip(
                pool, cfg.pool_start, cfg.pool_end, req.chaddr, sim_time);
            if (offered_ip) {
                dhcp_build_offer(&server, &req, &resp, offered_ip);
                dhcp_hashpool_alloc_lease(pool, offered_ip, req.chaddr,
                                          LEASE_TIME, sim_time);
                offers++;
            }
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    free(pool);

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = offers;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

/* ─────────────────────────────────────────────────────────────────────────
 * dhcp_nprc benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_nprc(uint32_t burst_size) {
    dhcp_nprcpool_t   pool;
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg = make_config();

    server.config = cfg;

    uint64_t best   = UINT64_MAX;
    uint32_t offers = 0;

    for (int rep = 0; rep < REPS; rep++) {
        /* Zero-init: cache_base=0, offer_next=0, ack_bitmap=0 */
        pool = (dhcp_nprcpool_t){ 0, 0, 0UL };
        offers = 0;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));

            int offered_ip = dhcp_nprc_find_available_ip(
                &pool, cfg.pool_start, cfg.pool_end);
            if (offered_ip) {
                dhcp_build_offer(&server, &req, &resp, (uint32_t)offered_ip);
                dhcp_nprc_commit_ip(&pool, (unsigned)offered_ip,
                                    cfg.pool_start, cfg.pool_end);
                offers++;
            }
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = offers;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Synthetic DHCP REQUEST builder (renewal)
 *
 * Uses the same MAC as make_discover(idx) so pool implementations that
 * track MAC→IP (array, hashmap) can find the existing lease.
 * requested_ip is placed in ciaddr (unicast renewal style).
 * ───────────────────────────────────────────────────────────────────────── */

static void make_request(dhcp_message_t *msg, uint32_t idx, uint32_t requested_ip) {
    memset(msg, 0, sizeof(*msg));
    msg->op           = 1;
    msg->htype        = 1;
    msg->hlen         = 6;
    msg->xid          = 0xDEAD0000u | idx;
    msg->magic_cookie = DHCP_MAGIC_COOKIE;
    msg->ciaddr       = requested_ip;   /* client's current IP (unicast renewal) */

    msg->chaddr[0] = 0xDE;
    msg->chaddr[1] = 0xAD;
    msg->chaddr[2] = (uint8_t)((idx >> 16) & 0xFF);
    msg->chaddr[3] = (uint8_t)((idx >>  8) & 0xFF);
    msg->chaddr[4] = (uint8_t)( idx        & 0xFF);
    msg->chaddr[5] = 0x00;

    uint8_t type = DHCP_REQUEST;
    dhcp_add_option(msg, DHCP_OPT_MESSAGE_TYPE, 1, &type);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Renewal benchmarks — REQUEST for an already-assigned IP
 *
 * Each run_*_renew() function:
 *   1. Fills the pool with burst_size DISCOVER/alloc pairs (setup, not timed).
 *   2. Times burst_size REQUEST messages for those same clients.
 *
 * For array / hashmap: find_available_ip recognises the MAC and returns
 *   the same IP → lease is extended.  This is the true renewal path.
 *
 * For bitmap / vartime / nprc: no MAC→IP mapping, so the server cannot
 *   honour the specific IP.  It either allocates a new IP or NAKs.
 *   We still measure the round-trip time (peek+commit) for comparison.
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_server_renew(uint32_t burst_size) {
    dhcp_lease_t    *leases      = (dhcp_lease_t *)malloc(burst_size * sizeof(dhcp_lease_t));
    uint32_t        *assigned    = (uint32_t     *)malloc(burst_size * sizeof(uint32_t));
    dhcp_arraypool_t pool;
    dhcp_server_t    server;
    dhcp_message_t   req, resp;
    dhcp_config_t    cfg      = make_config();
    uint32_t         sim_time = 0;

    if (!leases || !assigned) {
        free(leases); free(assigned);
        bench_result_t oom = { burst_size, UINT32_MAX, 0.0, 0.0, 0.0 };
        return oom;
    }

    server.config = cfg;

    uint64_t best = UINT64_MAX;
    uint32_t acks = 0;

    for (int rep = 0; rep < REPS; rep++) {
        /* Setup: fill pool with burst_size leases */
        dhcp_arraypool_init(&pool, cfg.pool_start, leases, (uint16_t)burst_size);
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            uint32_t ip = dhcp_arraypool_find_available_ip(
                &pool, cfg.pool_start, cfg.pool_end, req.chaddr, sim_time);
            dhcp_arraypool_alloc_lease(&pool, ip, req.chaddr, LEASE_TIME, sim_time);
            assigned[i] = ip;
        }

        /* Timed: renewal REQUESTs — find_available_ip returns same IP for known MAC */
        acks = 0;
        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_request(&req, i, assigned[i]);
            memset(&resp, 0, sizeof(resp));
            uint32_t ip = dhcp_arraypool_find_available_ip(
                &pool, cfg.pool_start, cfg.pool_end, req.chaddr, sim_time);
            if (ip) {
                dhcp_build_ack(&server, &req, &resp, ip);
                dhcp_arraypool_alloc_lease(&pool, ip, req.chaddr, LEASE_TIME, sim_time);
                acks++;
            }
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    free(leases);
    free(assigned);

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = acks;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

static bench_result_t run_bitmap_renew(uint32_t burst_size) {
    dhcp_bmpool_uni_t pool;
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg      = make_config();
    uint32_t          sim_time = 0;

    server.config = cfg;

    uint64_t best = UINT64_MAX;
    uint32_t acks = 0;

    for (int rep = 0; rep < REPS; rep++) {
        /* Setup: fill pool with burst_size leases */
        dhcp_bmpool_uni_init(&pool, cfg.pool_start, cfg.lease_time);
        for (uint32_t i = 0; i < burst_size; i++) {
            uint32_t ip = dhcp_bmpool_uni_peek(&pool, sim_time);
            if (ip) dhcp_bmpool_uni_commit_ip(&pool, ip);
        }

        /*
         * Timed: full renewal-failure cycle (3 messages per client):
         *   1. REQUEST → NAK   (no MAC tracking; can't verify ownership)
         *   2. DISCOVER → OFFER (fallback; peek+commit new IP)
         *   3. REQUEST → ACK   (client requests offered IP; peek+commit again
         *                       because server is stateless between OFFER/ACK)
         */
        acks = 0;
        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            /* Step 1: REQUEST → NAK */
            make_request(&req, i, 0);
            memset(&resp, 0, sizeof(resp));
            dhcp_build_nak(&req, &resp);

            /* Step 2: DISCOVER → OFFER */
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));
            uint32_t offered_ip = dhcp_bmpool_uni_peek(&pool, sim_time);
            if (!offered_ip) continue;
            dhcp_build_offer(&server, &req, &resp, offered_ip);
            dhcp_bmpool_uni_commit_ip(&pool, offered_ip);

            /* Step 3: REQUEST → ACK */
            make_request(&req, i, offered_ip);
            memset(&resp, 0, sizeof(resp));
            uint32_t ack_ip = dhcp_bmpool_uni_peek(&pool, sim_time);
            if (!ack_ip) continue;
            dhcp_build_ack(&server, &req, &resp, ack_ip);
            dhcp_bmpool_uni_commit_ip(&pool, ack_ip);
            acks++;
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = acks;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

static bench_result_t run_vartime_renew(uint32_t burst_size) {
    dhcp_bmpool_var_t pool;
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg      = make_config();
    uint32_t          sim_time = 0;
    uint32_t          lt;

    server.config = cfg;

    uint64_t best = UINT64_MAX;
    uint32_t acks = 0;

    for (int rep = 0; rep < REPS; rep++) {
        /* Setup: fill pool with burst_size leases */
        dhcp_bmpool_var_init(&pool, cfg.pool_start, cfg.lease_time);
        for (uint32_t i = 0; i < burst_size; i++) {
            uint32_t ip = dhcp_bmpool_var_peek(&pool, sim_time, &lt);
            if (ip) dhcp_bmpool_var_commit_ip(&pool, ip);
        }

        /*
         * Timed: full renewal-failure cycle (3 messages per client):
         *   1. REQUEST → NAK
         *   2. DISCOVER → OFFER (peek+commit new IP)
         *   3. REQUEST → ACK   (stateless: another peek+commit)
         */
        acks = 0;
        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            /* Step 1: REQUEST → NAK */
            make_request(&req, i, 0);
            memset(&resp, 0, sizeof(resp));
            dhcp_build_nak(&req, &resp);

            /* Step 2: DISCOVER → OFFER */
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));
            uint32_t offered_ip = dhcp_bmpool_var_peek(&pool, sim_time, &lt);
            if (!offered_ip) continue;
            dhcp_build_offer(&server, &req, &resp, offered_ip);
            dhcp_bmpool_var_commit_ip(&pool, offered_ip);

            /* Step 3: REQUEST → ACK */
            make_request(&req, i, offered_ip);
            memset(&resp, 0, sizeof(resp));
            uint32_t ack_ip = dhcp_bmpool_var_peek(&pool, sim_time, &lt);
            if (!ack_ip) continue;
            dhcp_build_ack(&server, &req, &resp, ack_ip);
            dhcp_bmpool_var_commit_ip(&pool, ack_ip);
            acks++;
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = acks;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

static bench_result_t run_hashmap_renew(uint32_t burst_size) {
    dhcp_hashpool_t  *pool    = (dhcp_hashpool_t *)malloc(sizeof(dhcp_hashpool_t));
    uint32_t         *assigned = (uint32_t *)malloc(burst_size * sizeof(uint32_t));
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg      = make_config();
    uint32_t          sim_time = 0;

    if (!pool || !assigned) {
        free(pool); free(assigned);
        bench_result_t oom = { burst_size, UINT32_MAX, 0.0, 0.0, 0.0 };
        return oom;
    }

    server.config = cfg;

    uint64_t best = UINT64_MAX;
    uint32_t acks = 0;

    for (int rep = 0; rep < REPS; rep++) {
        /* Setup: fill pool with burst_size leases */
        dhcp_hashpool_init(pool, cfg.pool_start, HASH_N * 4 / 5);
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            uint32_t ip = dhcp_hashpool_find_available_ip(
                pool, cfg.pool_start, cfg.pool_end, req.chaddr, sim_time);
            dhcp_hashpool_alloc_lease(pool, ip, req.chaddr, LEASE_TIME, sim_time);
            assigned[i] = ip;
        }

        /* Timed: renewal REQUESTs — find_available_ip returns same IP for known MAC */
        acks = 0;
        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_request(&req, i, assigned[i]);
            memset(&resp, 0, sizeof(resp));
            uint32_t ip = dhcp_hashpool_find_available_ip(
                pool, cfg.pool_start, cfg.pool_end, req.chaddr, sim_time);
            if (ip) {
                dhcp_build_ack(&server, &req, &resp, ip);
                dhcp_hashpool_alloc_lease(pool, ip, req.chaddr, LEASE_TIME, sim_time);
                acks++;
            }
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    free(pool);
    free(assigned);

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = acks;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

static bench_result_t run_nprc_renew(uint32_t burst_size) {
    dhcp_nprcpool_t   pool;
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg = make_config();

    server.config = cfg;

    uint64_t best = UINT64_MAX;
    uint32_t acks = 0;

    for (int rep = 0; rep < REPS; rep++) {
        /* Setup: fill pool with burst_size leases */
        pool = (dhcp_nprcpool_t){ 0, 0, 0UL };
        for (uint32_t i = 0; i < burst_size; i++) {
            int ip = dhcp_nprc_find_available_ip(&pool, cfg.pool_start, cfg.pool_end);
            if (ip) dhcp_nprc_commit_ip(&pool, (unsigned)ip, cfg.pool_start, cfg.pool_end);
        }

        /*
         * Timed: full renewal-failure cycle (3 messages per client):
         *   1. REQUEST → NAK
         *   2. DISCOVER → OFFER (find_available_ip+commit)
         *   3. REQUEST → ACK   (stateless: another find_available_ip+commit)
         */
        acks = 0;
        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            /* Step 1: REQUEST → NAK */
            make_request(&req, i, 0);
            memset(&resp, 0, sizeof(resp));
            dhcp_build_nak(&req, &resp);

            /* Step 2: DISCOVER → OFFER */
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));
            int offered_ip = dhcp_nprc_find_available_ip(&pool, cfg.pool_start, cfg.pool_end);
            if (!offered_ip) continue;
            dhcp_build_offer(&server, &req, &resp, (uint32_t)offered_ip);
            dhcp_nprc_commit_ip(&pool, (unsigned)offered_ip, cfg.pool_start, cfg.pool_end);

            /* Step 3: REQUEST → ACK */
            make_request(&req, i, (uint32_t)offered_ip);
            memset(&resp, 0, sizeof(resp));
            int ack_ip = dhcp_nprc_find_available_ip(&pool, cfg.pool_start, cfg.pool_end);
            if (!ack_ip) continue;
            dhcp_build_ack(&server, &req, &resp, (uint32_t)ack_ip);
            dhcp_nprc_commit_ip(&pool, (unsigned)ack_ip, cfg.pool_start, cfg.pool_end);
            acks++;
        }
        uint64_t elapsed = now_ns() - t0;
        if (elapsed < best) best = elapsed;
    }

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = acks;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Memory-footprint comparison
 * ───────────────────────────────────────────────────────────────────────── */

/* Pool sizes representative of common subnet allocations */
static const struct { const char *label; uint64_t size; } POOL_SCENARIOS[] = {
    { "/24   (254 IPs)",         254ULL        },
    { "/20   (4K IPs)",          4094ULL       },
    { "/16   (64K IPs)",         65534ULL      },
    { "/12   (1M IPs)",          1048574ULL    },
    { "/8    (16M IPs)",         16777214ULL   },
};
#define NUM_POOL_SCENARIOS (sizeof(POOL_SCENARIOS) / sizeof(POOL_SCENARIOS[0]))

static void fmt_bytes(uint64_t b, char *buf, size_t n) {
    if      (b < 1024ULL)              snprintf(buf, n, "%llu B",    (unsigned long long)b);
    else if (b < 1024ULL*1024)         snprintf(buf, n, "%.1f KB",   b / 1024.0);
    else if (b < 1024ULL*1024*1024)    snprintf(buf, n, "%.1f MB",   b / (1024.0*1024));
    else                               snprintf(buf, n, "%.2f GB",   b / (1024.0*1024*1024));
}

/*
 * Print how much memory each implementation needs as the IP pool grows.
 *
 * ARRAY    — one dhcp_lease_t per IP in the pool → O(pool_size).
 * HASHMAP  — one dhcp_hashpool_t (HASH_N=4096 buckets) → O(1), fixed.
 *            Can track at most HASH_N concurrent clients.
 * BITMAP   — one dhcp_bmpool_uni_t regardless of pool size → O(1).
 *            Tracks DHCP_BITMAP_MAX_RANGES × DHCP_BITMAP_RANGE_SIZE IPs;
 *            the window recycles after lease expiry.
 * VARTIME  — one dhcp_bmpool_var_t (single range) → O(1), fixed.
 *            Simpler variant of BITMAP: one range, variable lease per window.
 * NPRC     — one dhcp_nprcpool_t (a 64-bit bitmap cache) → O(1), tiny.
 *            No MAC tracking; offers up to BITMAP_SIZE IPs per cache window.
 */
static void print_memory_scenario(void) {
    uint64_t uni_mem     = (uint64_t)sizeof(dhcp_bmpool_uni_t);
    uint64_t var_mem     = (uint64_t)sizeof(dhcp_bmpool_var_t);
    uint64_t hash_mem    = (uint64_t)sizeof(dhcp_hashpool_t);
    uint64_t nprc_mem    = (uint64_t)sizeof(dhcp_nprcpool_t);
    char uni_str[24], var_str[24], hash_str[24], nprc_str[24];
    fmt_bytes(uni_mem,  uni_str,  sizeof(uni_str));
    fmt_bytes(var_mem,  var_str,  sizeof(var_str));
    fmt_bytes(hash_mem, hash_str, sizeof(hash_str));
    fmt_bytes(nprc_mem, nprc_str, sizeof(nprc_str));

    printf("\nMemory Footprint  (server memory limit: 1 MB)\n");
    printf("Fixed-size implementations (pool-size independent):\n");
    printf("  dhcp_bitmap  : %s  (%d ranges × %d IPs/range, recycles)\n",
           uni_str, DHCP_BITMAP_MAX_RANGES, DHCP_BITMAP_RANGE_SIZE);
    printf("  dhcp_vartime : %s  (1 range × %d IPs/range, sliding window)\n",
           var_str, DHCP_BITMAP_RANGE_SIZE);
    printf("  dhcp_hashmap : %s  (open-addressing, max %d clients)\n",
           hash_str, HASH_N);
    printf("  dhcp_nprc    : %s  (%lu-IP bitmap cache window, no MAC store)\n\n",
           nprc_str, (unsigned long)BITMAP_SIZE);

    printf("Pool-size-dependent implementation:\n");
    printf("  dhcp_server  : %zu B × pool_size  (O(N) — one entry per IP)\n\n",
           sizeof(dhcp_lease_t));

    printf("%-22s %16s  %s\n",
           "Pool Size", "dhcp_server", "fits in 1 MB?");
    printf("%-22s %16s  %s\n",
           "──────────────────────", "────────────────",
           "─────────────");

    for (size_t i = 0; i < NUM_POOL_SCENARIOS; i++) {
        uint64_t table_mem = POOL_SCENARIOS[i].size * (uint64_t)sizeof(dhcp_lease_t);
        char tm_str[24];
        fmt_bytes(table_mem, tm_str, sizeof(tm_str));

        const char *ok = (table_mem <= MEM_LIMIT_BYTES) ? "YES" : "NO  <-- OOM";
        printf("%-22s %16s  %s\n",
               POOL_SCENARIOS[i].label, tm_str, ok);
    }
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Reporting
 * ───────────────────────────────────────────────────────────────────────── */

static void print_header(void) {
    printf("DHCP DISCOVER→OFFER burst benchmark  (5 implementations)\n");
    printf("Pool: 10.0.0.1–10.255.255.254  /8  (%u IPs)\n", (unsigned)POOL_SIZE);
    printf("Bitmap: %d ranges × %d IPs/range  |  NPRC window: %lu IPs  |  lease_time: %us\n",
           DHCP_BITMAP_MAX_RANGES, DHCP_BITMAP_RANGE_SIZE,
           (unsigned long)BITMAP_SIZE, (unsigned)LEASE_TIME);
    printf("Repetitions per measurement: %d  (best of)\n\n", REPS);

    printf("%-16s %8s %8s %12s %12s %14s\n",
           "Implementation", "Burst", "Offers", "Total(us)", "Avg(ns)", "Offers/sec");
    printf("%-16s %8s %8s %12s %12s %14s\n",
           "────────────────", "────────", "────────",
           "────────────", "────────────", "──────────────");
}

static void print_result(const char *impl, const bench_result_t *r) {
    if (r->total_ns < 1.0) {
        /* Burst completed within a single clock tick — unmeasurable at this size */
        printf("%-16s %8u %8u %12s %12s %14s\n",
               impl, r->burst_size, r->offers_sent,
               "<1 ns", "<1 ns", "N/A (too fast)");
    } else {
        printf("%-16s %8u %8u %12.1f %12.1f %14.0f\n",
               impl,
               r->burst_size,
               r->offers_sent,
               r->total_ns / 1000.0,   /* ns → µs */
               r->avg_ns,
               r->throughput);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(void) {
    print_memory_scenario();

    print_header();

    for (size_t bi = 0; bi < NUM_BURST_SIZES; bi++) {
        uint32_t burst = BURST_SIZES[bi];

        bench_result_t rs = run_server(burst);
        bench_result_t rb = run_bitmap(burst);
        bench_result_t rv = run_vartime(burst);
        bench_result_t rh = run_hashmap(burst);
        bench_result_t rn = run_nprc(burst);

        print_result("dhcp_server",  &rs);
        print_result("dhcp_bitmap",  &rb);
        print_result("dhcp_vartime", &rv);
        print_result("dhcp_hashmap", &rh);
        print_result("dhcp_nprc",    &rn);

        if (bi + 1 < NUM_BURST_SIZES)
            printf("\n");   /* blank line between burst groups */
    }

    printf("\n\nDHCP renewal benchmark  (pool pre-filled; same clients renewing)\n");
    printf("array/hashmap : REQUEST→ACK  (1 round trip; MAC lookup extends existing lease)\n");
    printf("bitmap/vartime/nprc: REQUEST→NAK, DISCOVER→OFFER, REQUEST→ACK  (3 messages;\n");
    printf("  no MAC tracking forces full rediscover; 2 pool allocs per client)\n");
    printf("Repetitions per measurement: %d  (best of)\n\n", REPS);

    printf("%-16s %8s %8s %12s %12s %14s\n",
           "Implementation", "Burst", "ACKs", "Total(us)", "Avg(ns)", "ACKs/sec");
    printf("%-16s %8s %8s %12s %12s %14s\n",
           "────────────────", "────────", "────────",
           "────────────", "────────────", "──────────────");

    for (size_t bi = 0; bi < NUM_BURST_SIZES; bi++) {
        uint32_t burst = BURST_SIZES[bi];

        bench_result_t rs = run_server_renew(burst);
        bench_result_t rb = run_bitmap_renew(burst);
        bench_result_t rv = run_vartime_renew(burst);
        bench_result_t rh = run_hashmap_renew(burst);
        bench_result_t rn = run_nprc_renew(burst);

        print_result("dhcp_server",  &rs);
        print_result("dhcp_bitmap",  &rb);
        print_result("dhcp_vartime", &rv);
        print_result("dhcp_hashmap", &rh);
        print_result("dhcp_nprc",    &rn);

        if (bi + 1 < NUM_BURST_SIZES)
            printf("\n");
    }

    printf("\nNotes:\n");
    printf("  - Timing uses CLOCK_MONOTONIC; best of %d reps reported.\n", REPS);
    printf("  - With a /8 pool (%u IPs), dhcp_server/hashmap never exhaust for these sizes.\n",
           (unsigned)POOL_SIZE);
    printf("  - dhcp_bitmap  : %d ranges × %d IPs = %u IPs cap (unitime, O(1) alloc).\n",
           DHCP_BITMAP_MAX_RANGES, DHCP_BITMAP_RANGE_SIZE,
           DHCP_BITMAP_MAX_RANGES * DHCP_BITMAP_RANGE_SIZE);
    printf("  - dhcp_vartime : 1 range × %d IPs cap (sliding window lease, O(1) alloc).\n",
           DHCP_BITMAP_RANGE_SIZE);
    printf("  - dhcp_hashmap : O(1) MAC lookup; O(HASH_N=%d) linear scan per IP search.\n",
           HASH_N);
    printf("  - dhcp_nprc    : %lu-IP bitmap window; O(1) offer+commit, no MAC store.\n",
           (unsigned long)BITMAP_SIZE);
    printf("  - dhcp_server  : MAC→IP array; O(N) scan per DISCOVER.\n");
    printf("  - Renewal: array/hashmap do 1 round trip (REQUEST→ACK, MAC lookup + extend).\n");
    printf("    bitmap/vartime/nprc do 3 messages per client (REQUEST→NAK then full\n");
    printf("    DISCOVER→OFFER→ACK rediscover), consuming 2 pool slots per renewal.\n");
    printf("    dhcp_vartime may show 0 ACKs for large bursts: its single %d-IP range is\n",
           DHCP_BITMAP_RANGE_SIZE);
    printf("    exhausted (setup + 2 allocs/renewal exceeds range capacity).\n");
    return 0;
}
