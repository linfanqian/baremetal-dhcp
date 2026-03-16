# 06-ethernet-dhcp-lib

A bare-metal DHCP server for the Raspberry Pi, built on the Circle C++ bare-metal framework. This sample wires Circle's raw Ethernet driver to the portable `dhcp-lib` C library, allowing different IP lease management algorithms to be benchmarked by changing a single build flag.

---

## Architecture

```
Circle (C++) layer                    dhcp-lib (C) layer
──────────────────────────────        ────────────────────────────
kernel.cpp                            dhcp_server.h / dhcp_server.c
  ├── ReceiveFrame()   ──────────→    CDHCPServer::ProcessDHCPHdr()
  │   (raw Ethernet)                    └── hdrToMsg()  (wire → library format)
  │                                         dhcp_process_message_<mode>()
  │                                         msgToHdr()  (library → wire format)
  └── SendFrame()      ←──────────   CraftDHCPOffer() / CraftDHCPAck()
      (raw Ethernet)
```

- **`kernel.cpp`** — Circle entry point. Initializes hardware (USB, Ethernet, UART logger), then runs a polling loop that receives raw Ethernet frames and dispatches DHCP packets.
- **`dhcp-server.cpp`** — C++ adapter (`CDHCPServer`) that bridges Circle's packed wire-format `DHCPHdr` to `dhcp-lib`'s portable `dhcp_message_t`. Handles byte-order conversion (`unpackIP`/`packIP`).
- **`dhcp.h`** — `CDHCPServer` class definition. Sets the IP pool range and pre-allocates backend storage as static members.
- **`dhcp_server.h`** / **`dhcp-lib/`** — Backend-agnostic DHCP logic (see `dhcp-lib/README.md`).

### IP Pool

Defined in `dhcp.h`:

| Constant | Value | Meaning |
|---|---|---|
| `DHCP_POOL_START` | `192.168.4.100` | First IP offered to clients |
| `DHCP_POOL_END` | `192.168.255.255` | Last IP in pool |
| `DHCP_POOL_SIZE` | `~65,180` | Total pool capacity |
| `DHCP_MAX_LEASES` | `min(pool_size, 1638)` | Max simultaneous leases (capped at 80% of hash table size) |
| `DHCP_BMUNI_NUM_RANGES` | `4` | Number of bitmap ranges for BITMAP_UNITIME mode |

---

## Build Requirements

- ARM cross-compiler: `arm-none-eabi-gcc`
  - Install: `brew install --cask gcc-arm-embedded`
  - Set PATH: `export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"`
- Circle libraries must be built first (one-time step):
  ```bash
  cd circle
  export AARCH=32 RASPPI=3
  ./makeall --nosample
  ```

---

## Build

```bash
cd circle/sample/06-ethernet-dhcp-lib
export AARCH=32 RASPPI=3
```

| Command | Lease backend |
|---|---|
| `make` | ARRAY (default) |
| `make DHCP_LEASE_MODE=BITMAP_VARTIME` | Bitmap, variable lease time |
| `make DHCP_LEASE_MODE=BITMAP_UNITIME` | Bitmap, unified lease time |
| `make DHCP_LEASE_MODE=NPRC` | NPRC sliding bitmap window |
| `make DHCP_LEASE_MODE=HASHMAP` | Hash map |

Output: `kernel8-32.img` in this directory.

To switch modes: `make clean && make DHCP_LEASE_MODE=<mode>`. No need to rebuild Circle libs.

---

## Deploying to the Pi

1. Copy `kernel8-32.img` to the Pi's SD card boot partition
2. Edit `config.txt` on the SD card to select the image:
   ```ini
   kernel=kernel8-32.img
   ```
   Or, if you have pre-built images saved with distinct names (e.g., `kernel8-32-hashmap.img`):
   ```ini
   kernel=kernel8-32-hashmap.img
   ```
3. Eject the SD card, insert into Pi, and power on

---

## Debug Logging

The kernel logs over UART at **115200 baud** (redirected away from screen since the Pi runs headless). To see logs, connect a USB-to-serial adapter to the Pi's UART pins and use:

```bash
screen /dev/tty.usbserial-* 115200
```

Two optional compile-time debug flags in `kernel.cpp`:

| Flag | What it logs |
|---|---|
| `-DINPUTDEBUG` | Every received Ethernet/IP/UDP/DHCP frame header |
| `-DOUTPUTDEBUG` | Every sent DHCP Offer and ACK frame header |

Enable by adding to the `make` command:
```bash
make DHCP_LEASE_MODE=HASHMAP CFLAGS_EXTRA="-DOUTPUTDEBUG"
```

---

## Packet Flow

```
Client sends DHCP Discover (broadcast)
  → kernel.cpp: ReceiveFrame()
  → CDHCPServer::ProcessDHCPHdr()   — validates magic cookie, extracts message type
  → CDHCPServer::CraftDHCPOffer()   — allocates an IP, builds Offer via dhcp-lib
  → kernel.cpp: SendFrame()         — sends Offer as raw Ethernet broadcast

Client sends DHCP Request
  → kernel.cpp: ReceiveFrame()
  → CDHCPServer::ProcessDHCPHdr()
  → CDHCPServer::CraftDHCPAck()     — commits the lease, builds ACK via dhcp-lib
  → kernel.cpp: SendFrame()         — sends ACK as raw Ethernet broadcast

Client sends DHCP Decline
  → CDHCPServer::HandleDHCPDecline() — marks the IP as declined in the pool
```
