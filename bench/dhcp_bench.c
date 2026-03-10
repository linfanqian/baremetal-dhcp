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
#include "dhcp_bitmap_ops.h"
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
 * dhcp_bmuni (BITMAP_UNITIME) benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_unitime(uint32_t burst_size) {
    dhcp_bmpool_uni_t pool;
    dhcp_server_t     server;   /* holds only config (no mode flag set) */
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg        = make_config();
    uint32_t          sim_time   = 0;
    uint8_t           num_ranges = 4;
    uint32_t          range_size = POOL_SIZE / num_ranges;
    uint32_t          words      = (range_size + 31u) / 32u;

    dhcp_bmrange_t *ranges = (dhcp_bmrange_t *)malloc(num_ranges * sizeof(dhcp_bmrange_t));
    if (!ranges) {
        bench_result_t oom = { burst_size, UINT32_MAX, 0.0, 0.0, 0.0 };
        return oom;
    }
    for (uint8_t i = 0; i < num_ranges; i++) {
        ranges[i].ips = (uint32_t *)malloc(words * sizeof(uint32_t));
        if (!ranges[i].ips) {
            for (uint8_t j = 0; j < i; j++) free(ranges[j].ips);
            free(ranges);
            bench_result_t oom = { burst_size, UINT32_MAX, 0.0, 0.0, 0.0 };
            return oom;
        }
    }

    server.config = cfg;

    uint64_t best   = UINT64_MAX;
    uint32_t offers = 0;

    for (int rep = 0; rep < REPS; rep++) {
        dhcp_bmpool_uni_init(&pool, cfg.pool_start, cfg.lease_time,
                             ranges, range_size, num_ranges);
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

    for (uint8_t i = 0; i < num_ranges; i++) free(ranges[i].ips);
    free(ranges);

    bench_result_t r;
    r.burst_size  = burst_size;
    r.offers_sent = offers;
    r.total_ns    = (double)best;
    r.avg_ns      = (burst_size > 0) ? r.total_ns / burst_size : 0.0;
    r.throughput  = (r.total_ns > 0) ? (burst_size * 1e9 / r.total_ns) : 0.0;
    return r;
}


/* ─────────────────────────────────────────────────────────────────────────
 * dhcp_bmvar (BITMAP_VARTIME) benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_vartime(uint32_t burst_size) {
    dhcp_bmpool_var_t pool;
    dhcp_server_t     server;
    dhcp_message_t    req, resp;
    dhcp_config_t     cfg        = make_config();
    uint32_t          sim_time   = 0;
    uint32_t          lt;
    uint32_t          range_size = POOL_SIZE;
    uint32_t          words      = (range_size + 31u) / 32u;

    dhcp_bmrange_t range;
    range.ips = (uint32_t *)malloc(words * sizeof(uint32_t));
    if (!range.ips) {
        bench_result_t oom = { burst_size, UINT32_MAX, 0.0, 0.0, 0.0 };
        return oom;
    }

    server.config = cfg;

    uint64_t best   = UINT64_MAX;
    uint32_t offers = 0;

    for (int rep = 0; rep < REPS; rep++) {
        dhcp_bmpool_var_init(&pool, cfg.pool_start, cfg.lease_time, &range, range_size);
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

    free(range.ips);

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
 * Fixed-size (pool-size independent):
 * HASHMAP  — one dhcp_hashpool_t (HASH_N=4096 buckets) → O(1), fixed.
 * NPRC     — one dhcp_nprcpool_t (64-bit bitmap cache) → O(1), tiny.
 *
 * Pool-size-dependent:
 * ARRAY    — one dhcp_lease_t per IP → pool_size × 12 B.
 * BMUNI    — bitmap: 4 × [ceil(pool_size/4 / 8) + 4] B + additional bytes in 
 *            dhcp_bmpool_uni_t ≈ pool_size/8 B total.
 * BMVAR    — bitmap: ceil(pool_size / 8) B + additional bytes in dhcp_bmpool_var_t.
 */
static void print_memory_scenario(void) {
    uint64_t hash_mem = (uint64_t)sizeof(dhcp_hashpool_t);
    uint64_t nprc_mem = (uint64_t)sizeof(dhcp_nprcpool_t);
    char hash_str[24], nprc_str[24];
    fmt_bytes(hash_mem, hash_str, sizeof(hash_str));
    fmt_bytes(nprc_mem, nprc_str, sizeof(nprc_str));

    printf("\nMemory Footprint  (server memory limit: 1 MB)\n");
    printf("Fixed-size implementations (pool-size independent):\n");
    printf("  dhcp_hashmap : %s  (open-addressing, max %d clients)\n",
           hash_str, HASH_N);
    printf("  dhcp_nprc    : %s  (%lu-IP bitmap cache window, no MAC store)\n\n",
           nprc_str, (unsigned long)BITMAP_SIZE);

    printf("Pool-size-dependent implementations:\n");
    printf("  dhcp_server  : %zu B × pool_size  (one lease entry per IP)\n",
           sizeof(dhcp_lease_t));
    printf("  dhcp_bmuni   : %zu B + 4×[ceil(pool_size/4/8) + 4] B"
           "  (pool struct + 4 ranges bitmap+expire)\n",
           sizeof(dhcp_bmpool_uni_t));
    printf("  dhcp_bmvar   : %zu B + ceil(pool_size/8) B"
           "  (pool struct + bitmap)\n\n",
           sizeof(dhcp_bmpool_var_t));

    printf("%-22s %16s %16s %16s\n",
           "Pool Size", "dhcp_server", "dhcp_bmuni", "dhcp_bmvar");
    printf("%-22s %16s %16s %16s\n",
           "──────────────────────", "────────────────",
           "────────────────", "────────────────");

    for (size_t i = 0; i < NUM_POOL_SCENARIOS; i++) {
        uint64_t sz = POOL_SCENARIOS[i].size;

        uint64_t server_mem = sz * (uint64_t)sizeof(dhcp_lease_t);
        uint64_t uni_mem    = (uint64_t)sizeof(dhcp_bmpool_uni_t)
                            + 4u * ((sz / 4u + 7u) / 8u + 4u);
        uint64_t var_mem    = (uint64_t)sizeof(dhcp_bmpool_var_t)
                            + (sz + 7u) / 8u;

        char sm[24], um[24], vm[24];
        fmt_bytes(server_mem, sm, sizeof(sm));
        fmt_bytes(uni_mem,    um, sizeof(um));
        fmt_bytes(var_mem,    vm, sizeof(vm));

        /* Annotate with OOM marker if over limit */
        char sm_ann[32], um_ann[32], vm_ann[32];
        snprintf(sm_ann, sizeof(sm_ann), "%s%s", sm, server_mem > MEM_LIMIT_BYTES ? " OOM" : "");
        snprintf(um_ann, sizeof(um_ann), "%s%s", um, uni_mem    > MEM_LIMIT_BYTES ? " OOM" : "");
        snprintf(vm_ann, sizeof(vm_ann), "%s%s", vm, var_mem    > MEM_LIMIT_BYTES ? " OOM" : "");

        printf("%-22s %16s %16s %16s\n",
               POOL_SCENARIOS[i].label, sm_ann, um_ann, vm_ann);
    }
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Reporting
 * ───────────────────────────────────────────────────────────────────────── */

static void print_header(void) {
    printf("DHCP DISCOVER→OFFER burst benchmark  (5 implementations)\n");
    printf("Pool: 10.0.0.1–10.255.255.254  /8  (%u IPs)\n", (unsigned)POOL_SIZE);
    printf("Bitmap unitime: %d ranges × %u IPs/range | Bitmap vartime: %u IPs |  NPRC window: %lu IPs  |  lease_time: %us\n",
           4, (unsigned)(POOL_SIZE / 4),
           POOL_SIZE,
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
        bench_result_t rb = run_unitime(burst);
        bench_result_t rv = run_vartime(burst);
        bench_result_t rh = run_hashmap(burst);
        bench_result_t rn = run_nprc(burst);

        print_result("dhcp_server",  &rs);
        print_result("dhcp_bmuni",   &rb);
        print_result("dhcp_bmvar",   &rv);
        print_result("dhcp_hashmap", &rh);
        print_result("dhcp_nprc",    &rn);

        if (bi + 1 < NUM_BURST_SIZES)
            printf("\n");   /* blank line between burst groups */
    }

    printf("\nNotes:\n");
    printf("  - Timing uses CLOCK_MONOTONIC; best of %d reps reported.\n", REPS);
    printf("  - With a /8 pool (%u IPs), dhcp_server/hashmap never exhaust for these sizes.\n",
           (unsigned)POOL_SIZE);
    printf("  - dhcp_bmuni   : %d ranges × %u IPs = %u IPs cap (unitime, O(1) alloc).\n",
           4, (unsigned)(POOL_SIZE / 4), (unsigned)POOL_SIZE);
    printf("  - dhcp_bmvar   : 1 range × %u IPs cap (variable lease time, O(1) alloc).\n",
           (unsigned)POOL_SIZE);
    printf("  - dhcp_hashmap : O(1) MAC lookup; O(HASH_N=%d) linear scan per IP search.\n",
           HASH_N);
    printf("  - dhcp_nprc    : %lu-IP bitmap window; O(1) offer+commit, no MAC store.\n",
           (unsigned long)BITMAP_SIZE);
    printf("  - dhcp_server  : MAC→IP array; O(N) scan per DISCOVER.\n");
    return 0;
}
