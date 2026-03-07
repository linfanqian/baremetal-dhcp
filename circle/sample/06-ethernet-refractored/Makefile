#
# Makefile
#

CIRCLEHOME = ../..

OBJS	= main.o kernel.o mystring.o debug-frame.o dhcp-options.o dhcp-leases.o dhcp-server.o

LIBS	= $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

-include $(DEPS)
