# bench-live

A live DHCP benchmarking tool that sends real DHCP packets over Ethernet to a bare-metal DHCP server (running on a Raspberry Pi). Measures end-to-end handshake latency and pass/fail rate across many simulated clients.

        DHCP DISCOVER / REQUEST
  (raw Ethernet frames with random MAC)

        +---------+
        | Laptop  |
        |Benchmark|
        +----+----+
             |
             |  DHCP Request
             v
        +----+----+
        | Router  |
        |  (AP)   |
        +----+----+
             |
             |  Forward DHCP Request
             v
     +-------+-------+
     | Raspberry Pi  |
     | DHCP Server   |
     | (Bare-metal)  |
     +-------+-------+
             |
             |  DHCP OFFER / ACK
             v
        +----+----+
        | Router  |
        +----+----+
             |
             |  Forward DHCP Offer
             v
        +---------+
        | Laptop  |
        +---------+

## Requirements

- Linux with pre-installed libraries
- A network interface physically connected to the DHCP server (USB Ethernet adapter)

## Build

```bash
make
```

## Run

```bash
sudo ./bench-async [interface]
```

`interface` defaults to `en0` if not specified. Use the interface connected to the Pi (typically a USB Ethernet adapter, e.g., `en12`).

```bash
# Find your interface
networksetup -listallhardwareports

# Run on the correct interface
sudo ./bench-async en12
```

## Configuration

All tunable parameters are in `dhcp-client.h`:

| Parameter | Default | Meaning |
|---|---|---|
| `NUM_CLIENTS` | 1024 | Total number of simulated DHCP clients to run. Each client gets a random MAC address and runs one full Discover→Offer→Request→ACK handshake. |
| `MAX_WORKERS` | 32 | Number of parallel worker threads. Each worker drives clients sequentially from a shared queue. More workers = more concurrent in-flight handshakes = higher load on the server. |
| `TIMEOUT_SEC` | 1 | Seconds a worker waits for a server response before retrying. If the server is slow or dropping packets, increase this. |
| `MAX_RETRIES` | 3 | Maximum retransmissions per client before marking it as failed. |

### Choosing NUM_CLIENTS and MAX_WORKERS

- To measure **latency under low load**: set `MAX_WORKERS=1`, `NUM_CLIENTS=1` (one client at a time, sequential)
- To measure **throughput under high load**: set `MAX_WORKERS=32`, `NUM_CLIENTS=2048` (32 concurrent requests)

## Output

Results are printed to the terminal at the end of the run:

```
 === results: 1000/1024 passed, 24/1024 failed ===
 === average time usage: 5 ms (success 4 ms, fails 1000 ms) ===
```

- **passed**: clients that completed the full handshake (got a DHCP ACK)
- **failed**: clients that hit `MAX_RETRIES` without completing
- **average time usage**: mean total handshake time across all clients
- **success ms**: mean time for clients that passed
- **fails ms**: mean time for clients that failed (usually close to `TIMEOUT_SEC * MAX_RETRIES * 1000` ms)

## Architecture

```
Mac (bench-async)                     Raspberry Pi
─────────────────────────────         ──────────────────────
main()
  ├── push NUM_CLIENTS into queue
  ├── spawn MAX_WORKERS worker threads
  │     each worker:
  │       pop client from queue
  │       send DHCP Discover ──────────→  DHCP server
  │       wait for mailbox signal         send Offer ────────→ recv_manager
  │       send DHCP Request ──────────→  DHCP server          signals worker
  │       wait for mailbox signal         send ACK  ────────→ recv_manager
  │       record latency                                        signals worker
  │       loop to next client
  └── spawn recv_manager thread
        recv_manager:
          loop: raw_recv()  ←──────────  all incoming packets
          parse XID
          lookup worker by XID
          signal worker's mailbox
```

The `recv_manager` thread is the single reader of all incoming packets. It matches each packet to the correct worker via the XID hash table (`xid-tab`), then signals that worker's mailbox so the worker can proceed.

## Debug Build

To enable per-client verbose logging:

```bash
gcc ... -DDEBUG -o bench-async
```

This prints timeout/retry messages and per-worker client assignments to stdout.
