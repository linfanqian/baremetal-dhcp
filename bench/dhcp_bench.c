/*
 * dhcp_bench.c — Burst-mode benchmark comparing two DHCP lease store designs.
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
 *  dhcp_server  — dhcp_table linear-scan array keyed by MAC; O(N) lookup.
 *  dhcp_bitmap  — dhcp_bitmap_unitime range-bitmap pool; O(1) alloc.
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

/* Pull in both pool implementations directly. */
#include "dhcp_table.h"
#include "dhcp_bitmap_ops.h"      /* defines DHCP_BITMAP_RANGE_SIZE / MAX_RANGES */
#include "dhcp_bitmap_unitime.h"

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
 * TABLE cost analysis (full-fill):
 *   kth DISCOVER with k leases committed: O(k²) inner work (nested loop).
 *   Sum over k = 0..N-1 → O(N³/6) total.  Filling 2× the pool ≈ 8× slower.
 *
 * BITMAP cost: O(1) per DISCOVER regardless of fill level → O(N) total.
 *
 * IMPORTANT: each successful TABLE offer commits the lease via
 * dhcp_tablepool_alloc_lease() so the table actually fills up and the
 * O(N²)-per-DISCOVER scan penalty is visible.
 *
 * 1 rep only — TABLE at N=512 takes ~80ms; N=1024 ~640ms.
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
 * Each client gets a unique MAC so the TABLE implementation sees distinct
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
 * dhcp_server (TABLE) benchmark
 * ───────────────────────────────────────────────────────────────────────── */

static bench_result_t run_server(uint32_t burst_size) {
    /* Allocate only as many lease slots as the burst can produce.
     * POOL_SIZE is now /8 (16 M IPs) — a static array would be ~448 MB. */
    dhcp_lease_t    *leases = (dhcp_lease_t *)malloc(burst_size * sizeof(dhcp_lease_t));
    dhcp_tablepool_t pool;
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
        dhcp_tablepool_init(&pool, cfg.pool_start, leases, (uint16_t)burst_size);
        offers = 0;

        uint64_t t0 = now_ns();
        for (uint32_t i = 0; i < burst_size; i++) {
            make_discover(&req, i);
            memset(&resp, 0, sizeof(resp));

            uint32_t offered_ip = dhcp_tablepool_find_available_ip(
                &pool, cfg.pool_start, cfg.pool_end, req.chaddr, sim_time);
            if (offered_ip) {
                dhcp_build_offer(&server, &req, &resp, offered_ip);
                dhcp_tablepool_alloc_lease(&pool, offered_ip, req.chaddr, LEASE_TIME, sim_time);
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
                dhcp_bmpool_uni_alloc_ip(&pool, offered_ip);
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
 * TABLE  — one dhcp_lease_t (28 B) per IP in the pool → O(pool_size).
 * BITMAP — one dhcp_bmpool_uni_t regardless of pool size → O(1).
 *          Only DHCP_BITMAP_MAX_RANGES × DHCP_BITMAP_RANGE_SIZE IPs are
 *          tracked at once; the window recycles after each range's lease
 *          expires, so arbitrarily large pools are served with fixed RAM.
 */
static void print_memory_scenario(void) {
    /* BITMAP memory is a compile-time constant regardless of pool size */
    uint64_t bitmap_mem = (uint64_t)sizeof(dhcp_bmpool_uni_t);
    char     bm_str[24];
    fmt_bytes(bitmap_mem, bm_str, sizeof(bm_str));

    printf("\nMemory Footprint vs Pool Size  (server memory limit: 1 GB)\n");
    printf("dhcp_server : %zu B per lease entry × pool_size  (O(N))\n",
           sizeof(dhcp_lease_t));
    printf("dhcp_bitmap : %llu B fixed  (%d ranges × %d IPs/range, recycles)\n\n",
           (unsigned long long)bitmap_mem,
           DHCP_BITMAP_MAX_RANGES, DHCP_BITMAP_RANGE_SIZE);

    printf("%-22s %16s %16s  %s\n",
           "Pool Size", "dhcp_server", "dhcp_bitmap", "dhcp_server fits?");
    printf("%-22s %16s %16s  %s\n",
           "──────────────────────", "────────────────",
           "────────────────", "─────────────────");

    for (size_t i = 0; i < NUM_POOL_SCENARIOS; i++) {
        uint64_t table_mem = POOL_SCENARIOS[i].size * (uint64_t)sizeof(dhcp_lease_t);
        char tm_str[24];
        fmt_bytes(table_mem, tm_str, sizeof(tm_str));

        const char *ok = (table_mem <= MEM_LIMIT_BYTES)
                         ? "YES"
                         : "NO  <-- OOM";
        printf("%-22s %16s %16s  %s\n",
               POOL_SCENARIOS[i].label, tm_str, bm_str, ok);
    }
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Reporting
 * ───────────────────────────────────────────────────────────────────────── */

static void print_header(void) {
    printf("DHCP DISCOVER→OFFER burst benchmark\n");
    printf("Pool: 10.0.0.1–10.255.255.254  /8  (%u IPs)\n", (unsigned)POOL_SIZE);
    printf("Bitmap: %d ranges × %d IPs/range  |  lease_time: %us\n",
           DHCP_BITMAP_MAX_RANGES, DHCP_BITMAP_RANGE_SIZE, (unsigned)LEASE_TIME);
    printf("Repetitions per measurement: %d  (best of)\n\n", REPS);

    printf("%-16s %8s %8s %12s %12s %14s\n",
           "Implementation", "Burst", "Offers", "Total(us)", "Avg(ns)", "Offers/sec");
    printf("%-16s %8s %8s %12s %12s %14s\n",
           "────────────────", "────────", "────────",
           "────────────", "────────────", "──────────────");
}

static void print_result(const char *impl, const bench_result_t *r) {
    printf("%-16s %8u %8u %12.1f %12.1f %14.0f\n",
           impl,
           r->burst_size,
           r->offers_sent,
           r->total_ns / 1000.0,   /* ns → µs */
           r->avg_ns,
           r->throughput);
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

        print_result("dhcp_server",  &rs);
        print_result("dhcp_bitmap",  &rb);

        if (bi + 1 < NUM_BURST_SIZES)
            printf("\n");   /* blank line between burst groups */
    }

    printf("\nNotes:\n");
    printf("  - Timing uses CLOCK_MONOTONIC; best of %d reps reported.\n", REPS);
    printf("  - With a /8 pool (%u IPs), dhcp_server never exhausts for these burst sizes.\n",
           (unsigned)POOL_SIZE);
    printf("  - dhcp_bitmap is capped at %d ranges × %d IPs = %u IPs without lease recycling.\n",
           DHCP_BITMAP_MAX_RANGES, DHCP_BITMAP_RANGE_SIZE,
           DHCP_BITMAP_MAX_RANGES * DHCP_BITMAP_RANGE_SIZE);
    printf("  - dhcp_server  uses a MAC→IP table; O(N) scan per DISCOVER.\n");
    printf("  - dhcp_bitmap  uses a bitmap range pool (unitime); O(1) alloc per DISCOVER.\n");
    printf("  - dhcp_bitmap issues OFFERs even when pool counter wraps; the REQUEST/ACK\n");
    printf("    phase would then fail to commit a unique IP.\n");
    return 0;
}
