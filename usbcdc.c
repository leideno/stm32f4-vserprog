#include <stdlib.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/stm32/desig.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/dwc/otg_fs.h>

#define UID_LEN  (12 * 2 + 1) /* 12-byte, each byte turnned into 2-byte hex, then '\0'. */

#define DEV_VID    0x0483 /* ST Microelectronics */
#define DEV_PID    0x5740 /* STM32 */
#define DEV_VER    0x0009 /* 0.9 */

#define EP_INT     0x83
#define EP_OUT     0x82
#define EP_IN      0x01

#define STR_MAN    0x01
#define STR_PROD   0x02
#define STR_SER    0x03

#include "usbcdc.h"

static const struct usb_device_descriptor dev = {
  .bLength            = USB_DT_DEVICE_SIZE,
  .bDescriptorType    = USB_DT_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass       = USB_CLASS_CDC,
  .bDeviceSubClass    = 0,
  .bDeviceProtocol    = 0,
  .bMaxPacketSize0    = USBCDC_PKT_SIZE_DAT,
  .idVendor           = DEV_VID,
  .idProduct          = DEV_PID,
  .bcdDevice          = DEV_VER,
  .iManufacturer      = STR_MAN,
  .iProduct           = STR_PROD,
  .iSerialNumber      = STR_SER,
  .bNumConfigurations = 1,
};

/*
 * This notification endpoint isn't implemented. According to CDC spec its
 * optional, but its absence causes a NULL pointer dereference in Linux
 * cdc_acm driver.
 */
static const struct usb_endpoint_descriptor comm_endp[] = {{
  .bLength            = USB_DT_ENDPOINT_SIZE,
  .bDescriptorType    = USB_DT_ENDPOINT,
  .bEndpointAddress   = EP_INT,
  .bmAttributes       = USB_ENDPOINT_ATTR_INTERRUPT,
  .wMaxPacketSize     = USBCDC_PKT_SIZE_INT,
  .bInterval          = 255,
}};

static const struct usb_endpoint_descriptor data_endp[] = {{
  .bLength            = USB_DT_ENDPOINT_SIZE,
  .bDescriptorType    = USB_DT_ENDPOINT,
  .bEndpointAddress   = EP_IN,
  .bmAttributes       = USB_ENDPOINT_ATTR_BULK,
  .wMaxPacketSize     = USBCDC_PKT_SIZE_DAT,
  .bInterval          = 1,
}, {
  .bLength            = USB_DT_ENDPOINT_SIZE,
  .bDescriptorType    = USB_DT_ENDPOINT,
  .bEndpointAddress   = EP_OUT,
  .bmAttributes       = USB_ENDPOINT_ATTR_BULK,
  .wMaxPacketSize     = USBCDC_PKT_SIZE_DAT,
  .bInterval          = 1,
}};

static const struct {
  struct usb_cdc_header_descriptor header;
  struct usb_cdc_call_management_descriptor call_mgmt;
  struct usb_cdc_acm_descriptor acm;
  struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
  .header = {
    .bFunctionLength    = sizeof(struct usb_cdc_header_descriptor),
    .bDescriptorType    = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_HEADER,
    .bcdCDC = 0x0110,
  },
  .call_mgmt = {
    .bFunctionLength    = sizeof(struct usb_cdc_call_management_descriptor),
    .bDescriptorType    = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
    .bmCapabilities     = 0,
    .bDataInterface     = 1,
  },
  .acm = {
    .bFunctionLength    = sizeof(struct usb_cdc_acm_descriptor),
    .bDescriptorType    = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_ACM,
    .bmCapabilities     = 0,
  },
  .cdc_union = {
    .bFunctionLength    = sizeof(struct usb_cdc_union_descriptor),
    .bDescriptorType    = CS_INTERFACE,
    .bDescriptorSubtype = USB_CDC_TYPE_UNION,
    .bControlInterface  = 0,
    .bSubordinateInterface0 = 1,
   },
};

static const struct usb_interface_descriptor comm_iface[] = {{
  .bLength              = USB_DT_INTERFACE_SIZE,
  .bDescriptorType      = USB_DT_INTERFACE,
  .bInterfaceNumber     = 0,
  .bAlternateSetting    = 0,
  .bNumEndpoints        = 1,
  .bInterfaceClass      = USB_CLASS_CDC,
  .bInterfaceSubClass   = USB_CDC_SUBCLASS_ACM,
  .bInterfaceProtocol   = USB_CDC_PROTOCOL_AT,
  .iInterface           = 0,

  .endpoint             = comm_endp,

  .extra                = &cdcacm_functional_descriptors,
  .extralen             = sizeof(cdcacm_functional_descriptors),
}};

static const struct usb_interface_descriptor data_iface[] = {{
  .bLength              = USB_DT_INTERFACE_SIZE,
  .bDescriptorType      = USB_DT_INTERFACE,
  .bInterfaceNumber     = 1,
  .bAlternateSetting    = 0,
  .bNumEndpoints        = 2,
  .bInterfaceClass      = USB_CLASS_DATA,
  .bInterfaceSubClass   = 0,
  .bInterfaceProtocol   = 0,
  .iInterface           = 0,

  .endpoint             = data_endp,
}};

static const struct usb_interface ifaces[] = {{
  .num_altsetting       = 1,
  .altsetting           = comm_iface,
}, {
  .num_altsetting       = 1,
  .altsetting           = data_iface,
}};

static const struct usb_config_descriptor config = {
  .bLength              = USB_DT_CONFIGURATION_SIZE,
  .bDescriptorType      = USB_DT_CONFIGURATION,
  .wTotalLength         = 0,
  .bNumInterfaces       = 2,
  .bConfigurationValue  = 1,
  .iConfiguration       = 0,
  .bmAttributes         = 0x80,
  .bMaxPower            = 0x32,

  .interface            = ifaces,
};

/* Buffer to be used for control requests. */
static uint8_t usbd_control_buffer[128];

static enum usbd_request_return_codes cdcacm_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
    uint16_t *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req)) {
  switch (req->bRequest) {
  case USB_CDC_REQ_SET_CONTROL_LINE_STATE: {
    /*
     * This Linux cdc_acm driver requires this to be implemented
     * even though it's optional in the CDC spec, and we don't
     * advertise it in the ACM functional descriptor.
     */
    char local_buf[10];
    struct usb_cdc_notification *notif = (void *)local_buf;

    /* We echo signals back to host as notification. */
    notif->bmRequestType = 0xa1;
    notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
    notif->wValue        = 0;
    notif->wIndex        = 0;
    notif->wLength       = 2;
    local_buf[8]         = req->wValue & 3;
    local_buf[9]         = 0;

    /*
     * The Linux cdc_acm driver expects this SerialState notification to be
     * echoed back on the interrupt endpoint; without it the port stays
     * reported as "not connected" and reads block. Send it now.
     */
    while (usbd_ep_write_packet(usbd_dev, EP_INT, local_buf, 10) == 0);
    return USBD_REQ_HANDLED;
  }
  case USB_CDC_REQ_SET_LINE_CODING:
    if (*len < sizeof(struct usb_cdc_line_coding))
      return USBD_REQ_NOTSUPP;
    return USBD_REQ_HANDLED;
  }
  return USBD_REQ_NOTSUPP;
}

volatile bool usb_ready = false;

static void cdcacm_reset(void) {
  usb_ready = false;
}

/*
 * RX user buffer. The DWC OTG core discards any OUT-packet data that is not
 * read inside the endpoint callback during usbd_poll(), so we must drain the
 * packet in the callback (ISR context) and buffer it here for the pull-style
 * API (usbcdc_getc/usbcdc_fetch_packet/usbcdc_get_remainder).
 */
char usbcdc_rxbuf[USBCDC_PKT_SIZE_DAT]; /* DMA needs access */
static volatile uint8_t usbcdc_rxbuf_head = 0;
static volatile uint8_t usbcdc_rxbuf_tail = 0; /* 0 == empty / no packet ready */

/*
 * OUT-endpoint callback. Runs in USB interrupt context from usbd_poll(). The
 * packet MUST be drained here or the core throws it away. After reading it we
 * NAK the endpoint so the host holds off sending the next packet until the
 * application has consumed this one (see usbcdc_fetch_packet()).
 */
static void cdcacm_rx_cb(usbd_device *dev, uint8_t ep) {
  uint16_t len = usbd_ep_read_packet(dev, ep, usbcdc_rxbuf, USBCDC_PKT_SIZE_DAT);

  if (len == 0) {
    /* Zero-length packet: nothing to consume, keep receiving. */
    return;
  }

  /* Hold off further OUT packets until the app drains this one. */
  usbd_ep_nak_set(dev, ep, 1);

  usbcdc_rxbuf_head = 0;
  usbcdc_rxbuf_tail = len; /* publish last: signals "packet ready" to consumer */
}

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue) {
  usbd_ep_setup(usbd_dev, EP_IN , USB_ENDPOINT_ATTR_BULK, 64, cdcacm_rx_cb);
  usbd_ep_setup(usbd_dev, EP_OUT, USB_ENDPOINT_ATTR_BULK, 64, NULL);
  usbd_ep_setup(usbd_dev, EP_INT, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

  usbd_register_control_callback(
        usbd_dev,
        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        cdcacm_control_request);

  if (wValue > 0) {
    usb_ready = true;
  }
}

static usbd_device *usbd_dev; /* Just a pointer, need not to be volatile. */

static char serial[UID_LEN];

/* Vendor, device, version. */
static const char *usb_strings[] = {
  "dword1511.info",
  "STM32 virtual serprog for flashrom",
  serial,
};

void usbcdc_init(void) {
  desig_get_unique_id_as_string(serial, UID_LEN);

  rcc_periph_clock_enable(RCC_OTGFS);

  /*
   * OTG FS uses PA11 (USB_DM) and PA12 (USB_DP). These must be switched to
   * alternate function AF10; the libopencm3 OTG driver does NOT do this for us.
   * Without it the data lines are disconnected from the peripheral and the
   * device never enumerates.
   */
  rcc_periph_clock_enable(RCC_GPIOA);
  gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
  gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);

    /*
   * This board does not connect the OTG VBUS pin to the MCU. Disable all
   * VBUS sensing and set the "no VBUS sense" bit so the core connects as soon
   * as the DP pullup is driven, instead of waiting for (absent) VBUS.
   */
  OTG_FS_GCCFG |= OTG_GCCFG_NOVBUSSENS;

  usbd_dev = usbd_init(&otgfs_usb_driver, &dev, &config, usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));

  usbd_register_set_config_callback(usbd_dev, cdcacm_set_config);
  usbd_register_reset_callback(usbd_dev, cdcacm_reset);

  /* NOTE: Must be called after USB setup since this enables calling usbd_poll(). */
  nvic_enable_irq(NVIC_OTG_FS_IRQ);
}

/* Application-level functions */
uint16_t usbcdc_write(void *buf, size_t len) {
  uint16_t ret;

  /* Blocking write */
  while (0 == (ret = usbd_ep_write_packet(usbd_dev, EP_OUT, buf, len)));
  return ret;
}

uint16_t usbcdc_putc(char c) {
  return usbcdc_write(&c, sizeof(c));
}

uint16_t usbcdc_putu32(uint32_t word) {
  //uint32_t l = __builtin_bswap32(word);
  //return usbcdc_write(&l, sizeof(word));
  /* We are using little endian, so no bit swap. */
  return usbcdc_write(&word, sizeof(word));
}

/* We need to maintain a RX user buffer since libopencm3 will throw rest of the packet away. */
/* (Buffer and callback are defined above, near cdcacm_set_config.) */

uint16_t usbcdc_fetch_packet(void) {
  /*
   * Release the current buffer and wait for the next OUT packet. Packets are
   * delivered asynchronously by cdcacm_rx_cb() from the USB interrupt, which
   * NAKs the endpoint after each packet; clearing NAK here lets the host send
   * the next one. The IRQ is briefly masked to avoid racing the driver's
   * endpoint re-arm (read-modify-write on DOEPCTL).
   */
  nvic_disable_irq(NVIC_OTG_FS_IRQ);
  usbcdc_rxbuf_head = 0;
  usbcdc_rxbuf_tail = 0;                 /* mark empty */
  usbd_ep_nak_set(usbd_dev, EP_IN, 0);   /* allow host to send the next packet */
  nvic_enable_irq(NVIC_OTG_FS_IRQ);

  while (usbcdc_rxbuf_tail == 0);        /* block until the callback fills it */
  return usbcdc_rxbuf_tail;
}

char usbcdc_getc(void) {
  char c;

  if (usbcdc_rxbuf_head >= usbcdc_rxbuf_tail) {
    usbcdc_fetch_packet();
  }

  c = usbcdc_rxbuf[usbcdc_rxbuf_head];
  usbcdc_rxbuf_head ++;
  return c;
}

uint32_t usbcdc_getu24(void) {
  uint32_t val = 0;

  val  = (uint32_t)usbcdc_getc() << 0;
  val |= (uint32_t)usbcdc_getc() << 8;
  val |= (uint32_t)usbcdc_getc() << 16;

  return val;
}

uint32_t usbcdc_getu32(void) {
  uint32_t val = 0;

  val  = (uint32_t)usbcdc_getc() << 0;
  val |= (uint32_t)usbcdc_getc() << 8;
  val |= (uint32_t)usbcdc_getc() << 16;
  val |= (uint32_t)usbcdc_getc() << 24;

  return val;
}

uint8_t usbcdc_get_remainder(char **bufpp) {
  uint8_t len = usbcdc_rxbuf_tail - usbcdc_rxbuf_head;

  *bufpp = &(usbcdc_rxbuf[usbcdc_rxbuf_head]);
  usbcdc_rxbuf_head = usbcdc_rxbuf_tail; /* Mark as used. */

  return len;
}

/* Interrupts */

static void usb_int_relay(void) {
  /* Need to pass a parameter... otherwise just alias it directly. */
  usbd_poll(usbd_dev);
}

void otg_fs_isr(void)
__attribute__ ((alias ("usb_int_relay")));
