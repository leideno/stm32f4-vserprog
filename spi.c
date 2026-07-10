#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/dma.h>

#include "spi.h"
#include "usbcdc.h"

#define likely(x)   __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

/*
 * DMA mapping for SPI1 on the STM32F4: SPI1_TX -> DMA2_Stream5 / channel 3,
 * SPI1_RX -> DMA2_Stream2 / channel 3.
 */
#define SPI_DMA_TX_DMA    DMA2
#define SPI_DMA_TX_STREAM DMA_STREAM5
#define SPI_DMA_RX_DMA    DMA2
#define SPI_DMA_RX_STREAM DMA_STREAM2
#define SPI_DMA_CHSEL     DMA_SxCR_CHSEL_3

static uint8_t dma_rxbuf[USBCDC_PKT_SIZE_DAT];

void spi_disable_pins(void) {
  /* Configure GPIOs: SS = PA4, SCK = PA5, MISO = PA6, MOSI = PA7 */
  gpio_mode_setup(GPIO_BANK_SPI1, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO_SPI1_SCK | GPIO_SPI1_MOSI | GPIO_SPI1_MISO | GPIO_SPI1_NSS);
}

void spi_enable_pins(void) {
  /* Configure GPIOs: SS = PA4, SCK = PA5, MISO = PA6, MOSI = PA7 */
  gpio_mode_setup(GPIO_BANK_SPI1, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_SPI1_SCK | GPIO_SPI1_MOSI);
  gpio_mode_setup(GPIO_BANK_SPI1, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_SPI1_MISO);
  gpio_mode_setup(GPIO_BANK_SPI1, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_SPI1_NSS); /* SS is manual */
  gpio_set_af(GPIO_BANK_SPI1, GPIO_AF5, GPIO_SPI1_SCK | GPIO_SPI1_MOSI | GPIO_SPI1_MISO);
  gpio_set_output_options(GPIO_BANK_SPI1, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_SPI1_SCK | GPIO_SPI1_MOSI | GPIO_SPI1_NSS);
  gpio_set(GPIO_BANK_SPI1, GPIO_SPI1_NSS);
}

uint32_t spi_setup(uint32_t speed_hz) {
  uint32_t clkdiv;
  uint32_t relspd;

  rcc_periph_clock_enable(RCC_SPI1);
  rcc_periph_clock_enable(RCC_DMA2);

  /* SPI1 is on APB2. Assume f = f_PCLK / 2 (whereas datasheet says 18MHz max
   * but reference manual has no such word). */
  /* Lowest available */
  clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_256;
  relspd = rcc_apb2_frequency / 256;

  if(speed_hz >= rcc_apb2_frequency / 128) {
    clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_128;
    relspd = rcc_apb2_frequency / 128;
  }

  if(speed_hz >= rcc_apb2_frequency / 64) {
    clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_64;
    relspd = rcc_apb2_frequency / 64;
  }

  if(speed_hz >= rcc_apb2_frequency / 32) {
    clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_32;
    relspd = rcc_apb2_frequency / 32;
  }

  if(speed_hz >= rcc_apb2_frequency / 16) {
    clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_16;
    relspd = rcc_apb2_frequency / 16;
  }

  if(speed_hz >= rcc_apb2_frequency / 8) {
    clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_8;
    relspd = rcc_apb2_frequency / 8;
  }

  if(speed_hz >= rcc_apb2_frequency / 4) {
    clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_4;
    relspd = rcc_apb2_frequency / 4;
  }

  if(speed_hz >= rcc_apb2_frequency / 2) {
    clkdiv = SPI_CR1_BAUDRATE_FPCLK_DIV_2;
    relspd = rcc_apb2_frequency / 2;
  }

  spi_enable_pins();

  /* Reset SPI, SPI_CR1 register cleared, SPI is disabled */
  rcc_periph_reset_pulse(RST_SPI1);

  /* Set up SPI in Master mode with:
   * Clock baud rate: 1/256 of peripheral clock frequency
   * Clock polarity: Idle Low
   * Clock phase: Data valid on rising edge (1st edge for idle low)
   * Data frame format: 8-bit
   * Frame format: MSB First
   */
  spi_init_master(SPI1, clkdiv, SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE, SPI_CR1_CPHA_CLK_TRANSITION_1, SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST);

  /*
   * Set NSS management to software.
   *
   * NOTE:
   * Setting nss high is very important, even if we are controlling the GPIO
   * ourselves this bit needs to be at least set to 1, otherwise the spi
   * peripheral will not send any data out.
   */
  spi_enable_software_slave_management(SPI1);
  spi_set_nss_high(SPI1);

  /* Misc. */
  spi_disable_crc(SPI1);
  spi_disable_error_interrupt(SPI1);
  spi_disable_rx_buffer_not_empty_interrupt(SPI1);
  spi_disable_tx_buffer_empty_interrupt(SPI1);
  spi_set_full_duplex_mode(SPI1);
  spi_set_unidirectional_mode(SPI1);

  /* Enable SPI1 periph. */
  spi_enable(SPI1);

  /* Report actual clock speed selected */
  return relspd;
}

static void spi_dma_write(uint16_t len, char *buf) {
  static uint8_t tmp = 0;

  /* Reset DMA streams */
  dma_stream_reset(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);
  dma_stream_reset(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);

  /* Configure TX (memory -> peripheral) */
  dma_set_peripheral_address(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, (uint32_t)&SPI1_DR);
  dma_set_memory_address(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, (uint32_t)buf);
  dma_set_number_of_data(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, len);
  dma_set_transfer_mode(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
  dma_enable_memory_increment_mode(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);
  dma_set_peripheral_size(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_PSIZE_8BIT);
  dma_set_memory_size(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_MSIZE_8BIT);
  dma_set_priority(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_PL_HIGH);
  dma_channel_select(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, SPI_DMA_CHSEL);

  /* Configure RX (peripheral -> memory, garbage collection) */
  dma_set_peripheral_address(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, (uint32_t)&SPI1_DR);
  dma_set_memory_address(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, (uint32_t)(&tmp));
  dma_set_number_of_data(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, len);
  dma_set_transfer_mode(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_DIR_PERIPHERAL_TO_MEM);
  dma_disable_memory_increment_mode(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
  dma_set_peripheral_size(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_PSIZE_8BIT);
  dma_set_memory_size(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_MSIZE_8BIT);
  dma_set_priority(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_PL_VERY_HIGH);
  dma_channel_select(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, SPI_DMA_CHSEL);

  dma_enable_stream(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
  dma_enable_stream(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);
  spi_enable_rx_dma(SPI1);
  spi_enable_tx_dma(SPI1);
}

static void spi_dma_read(uint16_t len) {
  static uint8_t tmp = 0;

  /* Reset DMA streams */
  dma_stream_reset(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
  dma_stream_reset(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);

  /* Configure RX (peripheral -> memory) */
  dma_set_peripheral_address(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, (uint32_t)&SPI1_DR);
  dma_set_memory_address(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, (uint32_t)dma_rxbuf);
  dma_set_number_of_data(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, len);
  dma_set_transfer_mode(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_DIR_PERIPHERAL_TO_MEM);
  dma_enable_memory_increment_mode(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
  dma_set_peripheral_size(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_PSIZE_8BIT);
  dma_set_memory_size(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_MSIZE_8BIT);
  dma_set_priority(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_SxCR_PL_VERY_HIGH);
  dma_channel_select(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, SPI_DMA_CHSEL);

  /* Configure TX (memory -> peripheral, SPI clock generation) */
  dma_set_peripheral_address(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, (uint32_t)&SPI1_DR);
  dma_set_memory_address(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, (uint32_t)(&tmp));
  dma_set_number_of_data(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, len);
  dma_set_transfer_mode(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
  dma_disable_memory_increment_mode(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);
  dma_set_peripheral_size(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_PSIZE_8BIT);
  dma_set_memory_size(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_MSIZE_8BIT);
  dma_set_priority(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, DMA_SxCR_PL_HIGH);
  dma_channel_select(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM, SPI_DMA_CHSEL);

  dma_enable_stream(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
  dma_enable_stream(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);
  spi_enable_rx_dma(SPI1);
  spi_enable_tx_dma(SPI1);
}

void spi_bulk_read(uint32_t rlen) {
  while (likely(rlen >= USBCDC_PKT_SIZE_DAT)) {
    spi_dma_read(USBCDC_PKT_SIZE_DAT);
    rlen -= USBCDC_PKT_SIZE_DAT;

    while (unlikely(!dma_get_interrupt_flag(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF))); /* It is likely, but we want to exit loop with low latency. */
    dma_clear_interrupt_flags(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF);
    spi_disable_rx_dma(SPI1);
    spi_disable_tx_dma(SPI1);
    dma_disable_stream(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
    dma_disable_stream(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);

    usbcdc_write(dma_rxbuf, USBCDC_PKT_SIZE_DAT);
  }

  /* Leftover only happens when reading individual registers. */
  if (unlikely(rlen > 0)) {
    spi_dma_read(rlen);

    while(unlikely(!dma_get_interrupt_flag(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF)));
    dma_clear_interrupt_flags(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF);
    spi_disable_rx_dma(SPI1);
    spi_disable_tx_dma(SPI1);
    dma_disable_stream(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
    dma_disable_stream(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);

    usbcdc_write(dma_rxbuf, rlen);
  }
}

void spi_bulk_write(uint32_t slen) {
  uint8_t urlen;
  char *urbuf;

  urlen = usbcdc_get_remainder(&urbuf);

  /* Due to the characteristics of flashrom and serprog protocol, slen >= urlen. */
  if (urlen > 0) {
    spi_dma_write(urlen, urbuf);
    slen -= urlen;

    /* Always check RX flag to avoid leftovers in SPI_DR, which messes data up. */
    while (unlikely(!dma_get_interrupt_flag(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF))); /* It is likely, but we want to exit loop with low latency. */
    dma_clear_interrupt_flags(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF);
    spi_disable_rx_dma(SPI1);
    spi_disable_tx_dma(SPI1);
    dma_disable_stream(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
    dma_disable_stream(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);
  }

  /* We have no control over packet size here. */
  while (likely(slen > 0)) {
    urlen = usbcdc_fetch_packet();
    spi_dma_write(urlen, usbcdc_rxbuf);
    slen -= urlen;

    while (unlikely(!dma_get_interrupt_flag(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF))); /* It is likely, but we want to exit loop with low latency. */
    dma_clear_interrupt_flags(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM, DMA_TCIF);
    spi_disable_rx_dma(SPI1);
    spi_disable_tx_dma(SPI1);
    dma_disable_stream(SPI_DMA_RX_DMA, SPI_DMA_RX_STREAM);
    dma_disable_stream(SPI_DMA_TX_DMA, SPI_DMA_TX_STREAM);
  }

  /* Mark USB RX buffer as used. */
  usbcdc_get_remainder(&urbuf);
}
