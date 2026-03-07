# Build Circle From Source Code
This includes open-source c++ bare-bone environment directly imported from https://github.com/rsta2/circle.

Circle architecture:
```
circle/

    (implicitly used by makefile builds)
    addon/

    include/

    lib/

    boot/

    doc/

    (main modification happens)
    sample/

        (current code base)
        06-ethernet/ 

        (some prior experiments, provide useful and more sophisticated data structures
        like UDP socket, IP data type etc. in Net module. But requires circumventing
        its default dhcp process)
        21-webserver/ 

        ...

    (not sure yet)
    test/

    app/
```

To setup environment to build any modified kernel image, do:
```
circle$ ./build.sh
circle$ cd sample/06-ethernet/ (technically this can be any folder, just copy build.sh to other samples)
circle/sample/06-ethernet$ ./build.sh
```
This should give you an `kernel8-32.img` (together with `kernel8-32.map`, `kernel8-32.elf` etc.), which can be directly copied into MicroSD card on Raspberry Pi.

Note that two important environment variables inside the build system is specified in `Rules.mk`
```
AARCH	 ?= 32
RASPPI	 ?= 3

PREFIX	 ?= arm-none-eabi-
PREFIX64 ?= aarch64-none-elf-
```
both AARCH and RASPPI are global environment variables that dictate what kernel image is built. `AARCH` is used to specify whether the hardware uses 32-bit address or 64-bit address. Raspberry Pi 3 should support both. 32-bit kernel image requires `arm-none-eabi-` series cross compiler, whereas 64-bit kernel image requires `aarch64-none-elf` series cross compiler. Based on the availability of cross compilers either can be chosen for the current hardware. `RASPPI` defines the raspberry pi version it compiles to.

To change the configuration, go to `build.sh` (BOTH the sh at circle main directory and at sample directory) and modify
```
export AARCH=32
export RASPPI=3
```

# Install RaspBerry Pi Firmware

In order to prepare micro SD card for Raspberry Pi hardware, use existing firmware set inside:
```
circle-microusb-files/ (64-bit)

    bootcode.bin

    config.txt

    fixup.dat

    kernel8.img (or some variables in name, like kernel8-32.img, kernel8-32-eth.img etc.)

    start.elf

circle-microusb-files-32bit/

    <same thing>
```
All files need to be copied inside micro SD card for Pi to properly function, among the files, two can be subject to change: `config.txt` and `kernel-<something>.img`. 

`kernel-<something>.img` can be replaced with any self-compiled kernel image

`config.txt` the former contains configurations that determines how the Pi will be booted:
```
arm_64bit=1                 # set to 0 for 32-bit
kernel_address=0x80000      # set to 0x8000 for 32-bit
initial_turbo=0

# Enable this for JTAG / SWD debugging!
#enable_jtag_gpio=1

# technically any label that is not [pi3] should not matter, but for safety can set to the same kernel image
[pi02]                      
...

[pi2]
...

[pi3]
kernel=kernel-<something>.img    # the kernel image you want the Pi to use

[pi3+]
...

[pi4]
...

[cm4]
...

[pi5]
...
```

Easy way: 
copy whatever in circle-microusb-files into micro sd card.

To add changes: after modify the code in sample/<some sample>, do `make` (or use `build.sh` to maintain more controlled environment variables), make sure that kernel8.img is regenerated. Copy kernel8.img to micro sd card to replace the current one and plug into pi. 

# Runtime Observation
To see the output log (MacOS): 
`ls /dev/cu.*`
see the output, you will find something like: /dev/cu.SLAB_USBtoUART

Then `screen /dev/cu.SLAB_USBtoUART 115200`, you will see the output. 
**The device name can be difference across OS! on Linux it is /dev/ttyUSB<some number>, not sure about Windows.**
To detach the screen: ctrl + a then d. (Or directly ctrl + d) 
