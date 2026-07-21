// XusbDevice.cpp — see XusbDevice.h for why this personality exists.
#include "XusbDevice.h"

#include <string.h>

#include <XboxControllerNotificationParser.h>

#include "esp32-hal-tinyusb.h"
#include "device/usbd_pvt.h"

namespace xusb {

static const uint8_t kEpIn = 0x81;
static const uint8_t kEpOut = 0x02;

// Interface + vendor descriptor + two interrupt endpoints, byte-identical to
// ArduinoXInput (dmadison/ArduinoXInput_Teensy usb_desc.c), whose install
// base has proven them against xusb22.sys, Steam and games for years. The
// type-0x21 descriptor embeds the endpoint plan (0x25 0x81 0x14 = IN 0x81,
// 20-byte report; 0x13 0x02 0x08 = OUT 0x02, 8-byte command), so the endpoint
// descriptors below must stay in lockstep with it.
static const uint8_t kItfDescriptor[] = {
    // Interface: class 0xFF (vendor), subclass 0x5D (XUSB), protocol 0x01
    9, TUSB_DESC_INTERFACE, 0, 0, 2, 0xFF, 0x5D, 0x01, 0,
    // Vendor descriptor, type 0x21
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25, kEpIn, 0x14, 0x00, 0x00, 0x00, 0x00,
    0x13, kEpOut, 0x08, 0x00, 0x00,
    // EP IN: interrupt, 32 bytes, 4 ms
    7, TUSB_DESC_ENDPOINT, kEpIn, TUSB_XFER_INTERRUPT, 0x20, 0x00, 4,
    // EP OUT: interrupt, 32 bytes, 8 ms
    7, TUSB_DESC_ENDPOINT, kEpOut, TUSB_XFER_INTERRUPT, 0x20, 0x00, 8,
};

// XUSB input report.
typedef struct __attribute__((packed)) {
  uint8_t type;      // 0x00 = input
  uint8_t len;       // 0x14 = 20 bytes
  uint8_t buttons1;  // DU DD DL DR | Start Back L3 R3
  uint8_t buttons2;  // LB RB Guide - | A B X Y
  uint8_t lt, rt;    // triggers 0..255
  int16_t lx, ly, rx, ry;
  uint8_t reserved[6];
} XusbReport;

static uint8_t s_inBuf[32];   // endpoint buffer (wMaxPacketSize)
static uint8_t s_outBuf[32];  // rumble / LED commands from the host
static volatile bool s_inFlight = false;
static volatile bool s_rumbleNew = false;
static volatile uint8_t s_rumbleL = 0, s_rumbleR = 0;
static XboxControllerNotificationParser s_parser;

// --- TinyUSB application class driver ---------------------------------------

static void drvInit(void) {}

static void drvReset(uint8_t) {
  s_inFlight = false;
  s_rumbleNew = false;
}

static uint16_t drvOpen(uint8_t rhport, tusb_desc_interface_t const* itf,
                        uint16_t max_len) {
  TU_VERIFY(itf->bInterfaceClass == 0xFF && itf->bInterfaceSubClass == 0x5D &&
                itf->bInterfaceProtocol == 0x01,
            0);
  uint16_t const drv_len = sizeof(kItfDescriptor);
  TU_VERIFY(max_len >= drv_len, 0);

  uint8_t const* p = (uint8_t const*)itf;
  p += tu_desc_len(p);  // interface descriptor
  p += tu_desc_len(p);  // type-0x21 vendor descriptor
  TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const*)p), 0);
  p += tu_desc_len(p);
  TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const*)p), 0);

  s_inFlight = false;
  // Arm the OUT endpoint for the first rumble/LED command.
  usbd_edpt_xfer(rhport, kEpOut, s_outBuf, sizeof(s_outBuf));
  return drv_len;
}

static bool drvControl(uint8_t rhport, uint8_t stage,
                       tusb_control_request_t const* req) {
  // Standard interface housekeeping only. xusb22's optional vendor requests
  // go unanswered (stall) — the field-proven behaviour (ArduinoXInput).
  if (stage != CONTROL_STAGE_SETUP) return true;
  if (req->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD &&
      req->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE) {
    if (req->bRequest == TUSB_REQ_GET_INTERFACE) {
      uint8_t alt = 0;
      return tud_control_xfer(rhport, req, &alt, 1);
    }
    if (req->bRequest == TUSB_REQ_SET_INTERFACE) {
      return tud_control_status(rhport, req);
    }
  }
  return false;
}

static bool drvXfer(uint8_t rhport, uint8_t ep, xfer_result_t result,
                    uint32_t nBytes) {
  (void)result;
  if (ep == kEpIn) {
    s_inFlight = false;
  } else if (ep == kEpOut) {
    // Type 0x00 = rumble: bytes 3/4 are the big/small motor magnitudes.
    // Type 0x01 = LED pattern — nothing to show it on; ignored.
    if (nBytes >= 5 && s_outBuf[0] == 0x00) {
      s_rumbleL = s_outBuf[3];
      s_rumbleR = s_outBuf[4];
      s_rumbleNew = true;
    }
    usbd_edpt_xfer(rhport, kEpOut, s_outBuf, sizeof(s_outBuf));
  }
  return true;
}

static const usbd_class_driver_t kDriver = {
#if CFG_TUSB_DEBUG >= CFG_TUD_LOG_LEVEL
    .name = "XUSB",
#endif
    .init = drvInit,
    .reset = drvReset,
    .open = drvOpen,
    .control_xfer_cb = drvControl,
    .xfer_cb = drvXfer,
    .sof = NULL,
};

}  // namespace xusb

// Registered unconditionally; the driver only ever claims an FF/5D/01
// interface, which exists solely when the XUSB personality is enabled.
extern "C" usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* count) {
  *count = 1;
  return &xusb::kDriver;
}

namespace xusb {

// --- Arduino descriptor plumbing --------------------------------------------

static uint16_t descriptorCb(uint8_t* dst, uint8_t* itf) {
  memcpy(dst, kItfDescriptor, sizeof(kItfDescriptor));
  dst[2] = *itf;  // bInterfaceNumber, assigned by the stack
  *itf += 1;
  return sizeof(kItfDescriptor);
}

void begin() {
  tinyusb_enable_interface(USB_INTERFACE_VENDOR, sizeof(kItfDescriptor),
                           descriptorCb);
}

// --- Bridge-facing API ------------------------------------------------------

bool ready() { return tud_mounted() && !s_inFlight; }

bool sendReport16(const uint8_t* report16) {
  if (!ready()) return false;
  if (s_parser.update((uint8_t*)report16, 16) != 0) return false;

  XusbReport r = {};
  r.type = 0x00;
  r.len = sizeof(XusbReport);
  r.buttons1 = (s_parser.btnDirUp << 0) | (s_parser.btnDirDown << 1) |
               (s_parser.btnDirLeft << 2) | (s_parser.btnDirRight << 3) |
               (s_parser.btnStart << 4) | (s_parser.btnSelect << 5) |
               (s_parser.btnLS << 6) | (s_parser.btnRS << 7);
  r.buttons2 = (s_parser.btnLB << 0) | (s_parser.btnRB << 1) |
               (s_parser.btnXbox << 2) | (s_parser.btnA << 4) |
               (s_parser.btnB << 5) | (s_parser.btnX << 6) |
               (s_parser.btnY << 7);
  // Share (btnShare) has no XUSB equivalent — X360 pads predate it.
  r.lt = s_parser.trigLT >> 2;  // 0..1023 -> 0..255
  r.rt = s_parser.trigRT >> 2;
  // BLE axes: 0..65535, centre 32768, 0 = up. XUSB: int16, +32767 = up.
  r.lx = (int16_t)((int32_t)s_parser.joyLHori - 32768);
  r.ly = (int16_t)(32767 - (int32_t)s_parser.joyLVert);
  r.rx = (int16_t)((int32_t)s_parser.joyRHori - 32768);
  r.ry = (int16_t)(32767 - (int32_t)s_parser.joyRVert);

  uint8_t const rhport = 0;
  if (!usbd_edpt_claim(rhport, kEpIn)) return false;
  memcpy(s_inBuf, &r, sizeof(r));
  s_inFlight = true;
  if (!usbd_edpt_xfer(rhport, kEpIn, s_inBuf, sizeof(XusbReport))) {
    s_inFlight = false;
    return false;
  }
  return true;
}

bool takeRumble(uint8_t out8[8]) {
  if (!s_rumbleNew) return false;
  s_rumbleNew = false;
  // XUSB magnitudes are 0..255; the pad's BLE rumble report wants 0..100.
  out8[0] = 0x03;  // enable the two main motors
  out8[1] = (uint8_t)((s_rumbleL * 100) / 255);
  out8[2] = (uint8_t)((s_rumbleR * 100) / 255);
  out8[3] = 0;
  out8[4] = 0;
  out8[5] = 0xFF;  // run until the host updates us again
  out8[6] = 0;
  out8[7] = 0;
  return true;
}

}  // namespace xusb
