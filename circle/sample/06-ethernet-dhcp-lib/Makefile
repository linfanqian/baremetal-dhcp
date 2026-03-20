#
# Makefile
#

CIRCLEHOME = ../..

# Path to the shared C DHCP library (relative to this directory).
DHCPLIB = ../../../dhcp-lib

# Path to the shared data-structure library (provides hash.h / hash.c).
DSLIB = ../../../ds-lib

# dhcp-lib object files compiled from the library sources.
DHCPLIB_OBJS = \
	$(DHCPLIB)/dhcp_server.o \
	$(DHCPLIB)/dhcp_array.o \
	$(DHCPLIB)/dhcp_bitmap_ops.o \
	$(DHCPLIB)/dhcp_bitmap_vartime.o \
	$(DHCPLIB)/dhcp_bitmap_unitime.o \
	$(DHCPLIB)/dhcp_nprc.o \
	$(DHCPLIB)/dhcp_hashmap.o \
	$(DSLIB)/hash.o

OBJS	= main.o kernel.o mystring.o debug-frame.o \
	  dhcp-server.o \
	  $(DHCPLIB_OBJS)

LIBS	= $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/libcircle.a

# Expose dhcp-lib headers to the C++ compiler via EXTRAINCLUDE,
# which Rules.mk appends to CFLAGS / CPPFLAGS.
EXTRAINCLUDE = -I $(DHCPLIB) -I $(DSLIB)

CFLAGS += -DINPUTDEBUG -DOUTPUTDEBUG

# Lease mode selection.
# Build default (ARRAY) with:        make
# Build BITMAP_VARTIME with:         make DHCP_LEASE_MODE=BITMAP_VARTIME
# Build BITMAP_UNITIME with:         make DHCP_LEASE_MODE=BITMAP_UNITIME
DHCP_LEASE_MODE ?= ARRAY
ifeq ($(DHCP_LEASE_MODE),ARRAY)
CFLAGS += -DDHCP_LEASE_MODE_ARRAY
else ifeq ($(DHCP_LEASE_MODE),BITMAP_VARTIME)
CFLAGS += -DDHCP_LEASE_MODE_BMVAR
else ifeq ($(DHCP_LEASE_MODE),BITMAP_UNITIME)
CFLAGS += -DDHCP_LEASE_MODE_BMUNI
else ifeq ($(DHCP_LEASE_MODE),NPRC)
CFLAGS += -DDHCP_LEASE_MODE_NPRC
else ifeq ($(DHCP_LEASE_MODE),HASHMAP)
CFLAGS += -DDHCP_LEASE_MODE_HASHMAP
endif

# Tell the clean rule to also remove dhcp-lib objects built here.
EXTRACLEAN = $(DHCPLIB_OBJS)

include ../Rules.mk

# Explicit rule for compiling dhcp-lib C sources with Circle's flags.
# These files live outside the sample directory so the implicit %.o: %.c
# pattern rule in Rules.mk does not reach them.
$(DHCPLIB)/%.o: $(DHCPLIB)/%.c
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) $(C_STANDARD) -c -o $@ $<
$(DSLIB)/%.o: $(DSLIB)/%.c
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) $(C_STANDARD) -c -o $@ $<
-include $(DEPS)
