CC      = cc
CFLAGS  = -O2 -Wall \
           -I../dhcp-lib \
           -I../ds-lib \
           -DDHCP_BITMAP_MAX_RANGES=4 \
           -DDHCP_BITMAP_RANGE_SIZE=256

SRCS = dhcp_bench.c \
       ../dhcp-lib/dhcp_server.c \
       ../dhcp-lib/dhcp_array.c \
       ../dhcp-lib/dhcp_bitmap_ops.c \
       ../dhcp-lib/dhcp_bitmap_unitime.c \
       ../dhcp-lib/dhcp_bitmap_vartime.c \
       ../dhcp-lib/dhcp_hashmap.c \
       ../dhcp-lib/dhcp_nprc.c \
       ../ds-lib/hash.c

TARGET = dhcp_bench

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
