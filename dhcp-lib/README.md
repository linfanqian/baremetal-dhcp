# dhcp-lib

A portable, bare-metal DHCP server library. Handles DHCP message parsing, response building, and IP lease management. Designed to run on the Raspberry Pi under the Circle bare-metal framework, but portable to any C environment (no OS dependencies).

The lease management backend is selected at **compile time** via a preprocessor flag, allowing different pool algorithms to be benchmarked by swapping a single build option.

---

## Files

| File | Purpose |
|---|---|
| `dhcp_server.h` / `dhcp_server.c` | Core DHCP message processing, option parsing/building, shared utilities |
| `dhcp_array.h` / `dhcp_array.c` | ARRAY backend: flat lease array keyed by MAC |
| `dhcp_bitmap_ops.h` / `dhcp_bitmap_ops.c` | Shared bitmap primitives used by both bitmap backends |
| `dhcp_bitmap_vartime.h` / `dhcp_bitmap_vartime.c` | BITMAP_VARTIME backend: single range, variable lease time |
| `dhcp_bitmap_unitime.h` / `dhcp_bitmap_unitime.c` | BITMAP_UNITIME backend: multiple ranges, unified lease time |
| `dhcp_nprc.h` / `dhcp_nprc.c` | NPRC backend: sliding bitmap window, no MAC tracking |
| `dhcp_hashmap.h` / `dhcp_hashmap.c` | HASHMAP backend: two hash tables (MAC→IP and IP set) |
| `dhcp_compat.h` | Portability shim: maps `kmemcpy/kmemset/kmemcmp` to GCC builtins |

---

## Core API (`dhcp_server.h`)

### Data Structures

**`dhcp_config_t`** — server configuration (set once at startup):
| Field | Meaning |
|---|---|
| `server_ip` | Server's own IP address |
| `subnet_mask` | Network subnet mask |
| `gateway_ip` | Router/gateway IP to tell clients |
| `dns_ip` | DNS server IP to tell clients |
| `pool_start` | First IP address in the lease pool |
| `pool_end` | Last IP address in the lease pool |
| `lease_time` | Default lease duration in seconds |

All IPs are stored in **host byte order**.

**`dhcp_server_t`** — server state (holds config + the selected pool backend):
```c
dhcp_server_t server;
```

**`dhcp_message_t`** — in-memory DHCP message (not packed, not cast onto wire frames):
```c
dhcp_message_t request, response;
```

### Init & Process (per backend)

Each backend has its own init and process function. Example for HASHMAP:

```c
dhcp_config_t config = {
    .server_ip  = ip_to_uint32(192,168,1,1),
    .subnet_mask= ip_to_uint32(255,255,255,0),
    .gateway_ip = ip_to_uint32(192,168,1,1),
    .dns_ip     = ip_to_uint32(8,8,8,8),
    .pool_start = ip_to_uint32(192,168,1,10),
    .pool_end   = ip_to_uint32(192,168,1,200),
    .lease_time = 3600,
};

dhcp_server_t server;
dhcp_init_server_hashmap(&server, &config, 256);   // max 256 leases

// In your packet receive loop:
dhcp_process_message_hashmap(&server, &request, &response, cur_time);
// Then send `response` if response.op == BOOTREPLY (2)
```

| Backend | Init function | Process function |
|---|---|---|
| ARRAY | `dhcp_init_server_array(&server, &config, leases, max)` | `dhcp_process_message_array(...)` |
| BMVAR | `dhcp_init_server_bmvar(&server, &config, range, size)` | `dhcp_process_message_bmvar(...)` |
| BMUNI | `dhcp_init_server_bmuni(&server, &config, ranges, size, num)` | `dhcp_process_message_bmuni(...)` |
| NPRC | `dhcp_init_server_nprc(&server, &config)` | `dhcp_process_message_nprc(...)` |
| HASHMAP | `dhcp_init_server_hashmap(&server, &config, max_leases)` | `dhcp_process_message_hashmap(...)` |

### Utility functions

```c
// IP conversion
uint32_t ip_to_uint32(a, b, c, d);          // e.g. ip_to_uint32(192,168,1,1)
void uint32_to_ip(ip, &a, &b, &c, &d);

// Message type
uint8_t dhcp_get_message_type(msg);          // returns DHCP_DISCOVER, DHCP_REQUEST, etc.
void    dhcp_set_message_type(msg, type);

// Options
void    dhcp_add_option(msg, option, length, data);
uint8_t *dhcp_get_option(msg, option, &length);  // returns NULL if not present
```

---

## Backend Comparison

| Backend | MAC→IP mapping | Lease expiry | Memory | Notes |
|---|---|---|---|---|
| **ARRAY** | Yes (flat array) | Per-lease timestamp | `O(N)` leases | Simple; linear scan to find/expire leases |
| **BITMAP_VARTIME** | No | Per-range timer | `O(pool_size/32)` bits | Single range resets when full or expired; lease time = remaining range time |
| **BITMAP_UNITIME** | No | Per-range timer (range-wide) | `O(pool_size/32 × num_ranges)` | Multiple ranges rotate; all IPs in a range expire together |
| **NPRC** | No | None (no tracking) | `O(1)` — 1–4 `uint64_t` words | Sliding bitmap window; fastest allocation; no per-client state |
| **HASHMAP** | Yes (hash table) | Per-lease timestamp | `O(N)` hash buckets | Fast MAC lookup; uses `ds-lib/hash` internally |

### NPRC window size

NPRC has an additional compile-time parameter in `dhcp_nprc.h`:

| Flag | Window | `BITMAP_FLUSH` |
|---|---|---|
| `-DNPRC64` (default) | 64-bit (1 × uint64_t) | 16 |
| `-DNPRC256` | 256-bit (4 × uint64_t) | 64 |

A larger window means fewer resets under high load but slightly more memory.

---

## Portability

`dhcp_compat.h` provides `kmemcpy`, `kmemset`, `kmemcmp` as GCC builtin wrappers so the library compiles under both:
- The bare-metal pi-boot toolchain (which has `kstring.h`)
- The Circle `arm-none-eabi` / `aarch64-none-elf` cross-compilers (which do not)
