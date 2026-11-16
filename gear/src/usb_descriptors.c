// USB descriptors: composite CDC (config port) + MIDI.

#include <string.h>

#include "midipixi/usb_ids.h"
#include "pico/unique_id.h"
#include "tusb.h"

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // Interface Association Descriptors are used (CDC needs two interfaces)
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = MPX_USB_VID,
    .idProduct = MPX_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t* tud_descriptor_device_cb(void) { return (const uint8_t*)&desc_device; }

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_MIDI, ITF_NUM_MIDI_STREAMING, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82
#define EPNUM_MIDI_OUT 0x03
#define EPNUM_MIDI_IN 0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char* const string_desc[] = {
    NULL,                  // 0: language (handled below)
    MPX_USB_MANUFACTURER,  // 1
    MPX_USB_PRODUCT,       // 2
    serial_str,            // 3
    "midipixi config",     // 4: CDC interface
    MPX_USB_PRODUCT,       // 5: MIDI interface (port name)
};

static uint16_t desc_str[33];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t count;
    if (index == 0) {
        desc_str[1] = 0x0409;  // English (US)
        count = 1;
    } else {
        if (index >= sizeof(string_desc) / sizeof(string_desc[0])) return NULL;
        if (index == 3 && serial_str[0] == 0) pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
        const char* s = string_desc[index];
        count = strlen(s);
        if (count > 32) count = 32;
        for (size_t i = 0; i < count; ++i) desc_str[1 + i] = (uint8_t)s[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc_str;
}
