#ifndef __BOARD_H__
#define __BOARD_H__

#include <stdbool.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencmsis/core_cm3.h>

/*
 * Board definitions for STM32F401CE based boards (e.g. WeAct "BlackPill").
 * NOTE: The firmware source is currently written against STM32F1/F0 APIs and
 * needs to be ported to the STM32F4 (GPIO AF, DMA streams, USB OTG, clock
 * tree) before it will build/run for this target.
 *
 * Adjust the LED and USB pullup assignments to match your specific board.
 */

#define BOARD_USE_DEBUG_PINS_AS_GPIO false /* F4 SWD pins are not needed as GPIO here */

#define BOARD_RCC_LED                RCC_GPIOC
#define BOARD_PORT_LED               GPIOC
#define BOARD_PIN_LED                GPIO13
#define BOARD_LED_HIGH_IS_BUSY       true

/* STM32F4 has an integrated USB DP pullup (driven by the USB peripheral),
 * so no external GPIO pullup is used. */
//#define BOARD_HAS_INTERNAL_USB_PULLUP true
//#define BOARD_USE_INTERNAL_USB_PULLUP  true
//#define BOARD_RCC_USB_PULLUP          RCC_GPIOA
//#define BOARD_PORT_USB_PULLUP         GPIOA
//#define BOARD_PIN_USB_PULLUP          GPIO12

#define STM32F4
//#define USB_OTG_FS_BASE			(PERIPH_BASE_AHB2 + 0x00000)
//#define OTG_GCCFG			0x038
//#define OTG_FS_GCCFG		MMIO32(USB_OTG_FS_BASE + OTG_GCCFG)
//#define OTG_GCCFG_NOVBUSSENS	(1 << 21)

/* Currently you can only use SPI1, since it has highest clock. */

#endif /* __BOARD_H__ */
