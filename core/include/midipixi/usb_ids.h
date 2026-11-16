#pragma once
// USB identity of midipixi gear. Single source of truth for firmware and host.
//
// 1209:0001 is a pid.codes *test* PID: fine for development, NOT for release.
// Before distributing hardware, request a PID (free for RP2040/RP2350 products
// from Raspberry Pi: https://github.com/raspberrypi/usb-pid, or from pid.codes)
// and change it here.

#define MPX_USB_VID 0x1209
#define MPX_USB_PID 0x0001
#define MPX_USB_MANUFACTURER "midipixi"
#define MPX_USB_PRODUCT "midipixi gear"
