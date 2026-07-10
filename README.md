# stm32-vserprog (STM32F401CE)

USB CDC-ACM serprog programmer firmware for the WeAct BlackPill
(STM32F401CE) driving an SPI NOR flash (e.g. SK25P128) via flashrom.

## Build

This repository uses two git submodules:

| Submodule     | URL                                      | Notes                          |
|---------------|------------------------------------------|--------------------------------|
| `libopencm3`  | https://github.com/libopencm3/libopencm3 | Tracks `master`; provides the `spi_init_master` / `spi_enable_pins` / `spi_reset` API this firmware uses. |
| `flashrom`    | https://review.coreboot.org/flashrom     | Modern flashrom, used as the host-side tool. The firmware no longer includes flashrom headers (see `serprog.h` below). |

```sh
git submodule update --init
make
```

Flash (ST-Link):

```sh
make flash-stlink      # requires st-flash / stlink tooling on the PATH
```

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
`flashrom/programmers/serprog.c`.

## Board

Default target is the WeAct BlackPill `STM32F401CE` (`boards/stm32f401ce.h`,
`boards/stm32f401ce.mk`). The build symlinks `board.h` to the selected board
header; it is git-ignored.
