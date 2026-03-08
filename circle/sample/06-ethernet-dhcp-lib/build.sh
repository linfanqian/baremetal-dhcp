export AARCH=32
export RASPPI=3

# Lease mode selection.
# Build default (ARRAY) with:        make
# Build BITMAP_VARTIME with:         make DHCP_LEASE_MODE=BITMAP_VARTIME
# Build BITMAP_UNITIME with:         make DHCP_LEASE_MODE=BITMAP_UNITIME
# Build NPRC with:                   make DHCP_LEASE_MODE=NPRC
# Build HASHMAP with:                make DHCP_LEASE_MODE=HASHMAP
make clean
make DHCP_LEASE_MODE=ARRAY
