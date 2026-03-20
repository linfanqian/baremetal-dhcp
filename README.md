# baremetal-dhcp

A bare-metal DHCP server running on a Raspberry Pi (Circle framework), paired with a live benchmarking client that runs on a laptop. The project explores and compares different IP lease management algorithms under real network load, investigates how DHCP servers can be redesigned for resource-constrained environments. 

---

## Network Setup with router

```
  +-----------+
  |           |
  |  Laptop   |
  |           |
  +-----------+
       |
       | Wi-Fi
       v
  +-----------+                             +---------------+
  |  Router   |          Ethernet           | Raspberry Pi  |
  | (AP mode) | <=========================> | DHCP Server   |
  +           +     forward DHCP packet     | (bare-metal)  |
  +-----------+                             +---------------+
```

## Network Setup without router (for benchmarking)
```
  +-----------+                             +---------------+
  |           |          Ethernet           | Raspberry Pi  |
  |  Laptop   | <=========================> | DHCP Server   |
  |           |                             | (bare-metal)  |
  +-----------+                             +---------------+
```


## Benchmark Setup

```
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
```

## Repository Structure

```
baremetal-dhcp/
├── circle/                     # Circle bare-metal C++ framework (from rsta2/circle)
│   ├── lib/                    # Circle core libraries (build once)
│   ├── include/                # Circle headers
│   ├── boot/                   # Boot firmware files
│   └── sample/
│       └── 06-ethernet-dhcp-lib/   ← Pi DHCP server (main code)
│
├── dhcp-lib/                   # Portable C DHCP server library
│   ├── dhcp_server.{h,c}       # Core message processing
│   ├── dhcp_array.{h,c}        # Backend: per-MAC array
│   ├── dhcp_bitmap_vartime.{h,c}   # Backend: bitmap, variable lease time
│   ├── dhcp_bitmap_unitime.{h,c}   # Backend: bitmap, unified lease time
│   ├── dhcp_nprc.{h,c}         # Backend: NPRC sliding window
│   └── dhcp_hashmap.{h,c}      # Backend: hash map
│
├── ds-lib/                     # Shared data structure library
│   ├── hash.{h,c}              # Open-addressing hash table
│   └── cqueue.{h,c}            # Lock-free circular queue
│
├── bench-live/                 # Live benchmark client (runs on laptop)
│   ├── dhcp-client-async.c     # Main entry point
│   ├── workers-async.c         # Worker threads (simulate DHCP clients)
│   └── dhcp-client.h           # NUM_CLIENTS, MAX_WORKERS config
│
└── pi-connect/                 # UART serial monitor for Pi log output
    └── connect.py              # Reads Pi logs over USB-to-serial
```

### Which folders are needed for what

| Goal | Folders needed |
|---|---|
| Build and run the Pi DHCP server | `circle/` + `dhcp-lib/` + `ds-lib/` + `circle/sample/06-ethernet-dhcp-lib/` |
| Run the live benchmark | `bench-live/` only |
| Monitor Pi serial output | `pi-connect/` |

---

## How to Build

### 1. Build Circle libraries

```bash
# Install the official ARM toolchain if not already installed
brew install --cask gcc-arm-embedded
export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"

cd circle
export AARCH=32
export RASPPI=3
./makeall --nosample      # builds all Circle libs, skips samples
```

### 2. Build a Pi kernel image

```bash
cd circle/sample/06-ethernet-dhcp-lib
export AARCH=32 RASPPI=3

make                              # ARRAY backend (default)
make DHCP_LEASE_MODE=HASHMAP      # Hashmap backend
make DHCP_LEASE_MODE=NPRC         # NPRC backend
make DHCP_LEASE_MODE=BITMAP_VARTIME
make DHCP_LEASE_MODE=BITMAP_UNITIME
```

Output: `kernel8-32.img`

### 3. Flash the Pi SD card

Copy these files to the SD card boot partition:

```
bootcode.bin    fixup.dat    start.elf    config.txt    kernel8-32.img
```

Edit `config.txt` to select the kernel:

```ini
arm_64bit=0
kernel_address=0x8000

[pi3]
kernel=kernel8-32.img
```

If you have multiple pre-built images saved with distinct names on the SD card, you can switch between them by just editing the `kernel=` line — no reflashing needed:

```ini
kernel=kernel8-32-hashmap.img     # switch to hashmap backend
```

### 4. Build and run the benchmark (on macOS)

```bash
cd bench-live
make
sudo ./bench-async en12       # change interface number if needed.
```

Find your USB Ethernet interface:
```bash
networksetup -listallhardwareports
```

---

## Build Variables

### Circle / Pi kernel

| Variable | Default | Meaning |
|---|---|---|
| `AARCH` | 32 | Address width: `32` for ARMv7, `64` for AArch64 |
| `RASPPI` | 3 | Raspberry Pi version (3, 4, 5) |
| `DHCP_LEASE_MODE` | `ARRAY` | Lease backend: `ARRAY`, `BITMAP_VARTIME`, `BITMAP_UNITIME`, `NPRC`, `HASHMAP` |

### Benchmark (`bench-live/dhcp-client.h`)

| Variable | Default | Meaning |
|---|---|---|
| `NUM_CLIENTS` | 1024 | Total DHCP handshakes to run |
| `MAX_WORKERS` | 32 | Concurrent worker threads |
| `TIMEOUT_SEC` | 1 | Per-client response timeout (seconds) |
| `MAX_RETRIES` | 3 | Retransmissions before marking a client failed |

---

## Monitoring Pi Output

The Pi logs over UART at 115200 baud. Connect a USB-to-serial adapter to the Pi's UART pins, then:

```bash
# macOS
screen /dev/cu.SLAB_USBtoUART 115200

# Or use the Python monitor in pi-connect/
cd pi-connect
pip install -r requirements.txt
python connect.py
```

To detach `screen`: `Ctrl+A` then `D`.

On Linux the device is `/dev/ttyUSB0` (update `SERIAL_PORT` in `connect.py` accordingly).

---

## Further Reading

- [`dhcp-lib/README.md`](dhcp-lib/README.md) — DHCP library API and backend comparison
- [`ds-lib/README.md`](ds-lib/README.md) — Hash table and circular queue
- [`circle/sample/06-ethernet-dhcp-lib/README.md`](circle/sample/06-ethernet-dhcp-lib/README.md) — Pi kernel architecture and packet flow
- [`bench-live/README.md`](bench-live/README.md) — Benchmark configuration and output
