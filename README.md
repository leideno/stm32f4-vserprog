## stm32-vserprog ## 
A STM32F4 remix of dword1511's stm32-vserprog (https://github.com/dword1511/stm32-vserprog.git)

This has been tested on a STM32F401CE, it should also work on similar F4 boards like the STM32F411CE
with the necessary header and linker .ld files. 

## Build

1. Install `stm32flash` and the `gcc-arm-none-eabi` toolchain. You may also need `stlink-tools` 

    On Debian, simply do the following:

    ```bash
    sudo apt-get install stm32flash gcc-arm-none-eabi stlink-tools
    ```
    

2. Clone this repository.

   ```bash
   git clone --recurse-submodules https://github.com/leideno/stm32f4-vserprog
   ```

    This will also pull in the following submodules:

    | Submodule     | URL                                      | Notes                          |
    |---------------|------------------------------------------|--------------------------------|
    | `libopencm3`  | https://github.com/libopencm3/libopencm3 | Tracks `master`; provides the `spi_init_master` / `spi_enable_pins` / `spi_reset` API this firmware uses. |
    | `flashrom`    | https://review.coreboot.org/flashrom     | Modern flashrom, used as the host-side tool. The firmware no longer includes flashrom headers (see `serprog.h` below). |


3. Build and flash the firmware for STM32F4

   ```bash
   make
   make flash-stlink
   ```
   You can replace 'flash-stlink' with 'flash-dfu' or 'flash-uart' depending on your preferred firmware upload method.

## Host side (flashrom)

Build the host serprog tool from the `flashrom` submodule (or use any modern
flashrom >= 1.3, which auto-detects the chip via SFDP):

```sh
cd flashrom
meson setup build && ninja -C build
# or: ./configure && make
```

Then talk to the device (no root needed if your user is in `dialout`):

```sh
flashrom -p serprog:dev=/dev/ttyACM0:2000000
```

## Firmware / host protocol note

`serprog.h` is vendored locally. Modern flashrom removed the top-level
`serprog.h` (the protocol constants now live inside `programmers/serprog.c`
and are not meant to be included externally), so the firmware carries a copy of
the stable Serial Flasher Protocol constants plus the `BUS_SPI` bus-type bit.
Keep `serprog.h` in sync with the protocol defined in
`flashrom/programmers/serprog.c`.'

## Board

Default target is the WeAct BlackPill `STM32F401CE` (`boards/stm32f401ce.h`,
`boards/stm32f401ce.mk`). The build symlinks `board.h` to the selected board
header; it is git-ignored.
