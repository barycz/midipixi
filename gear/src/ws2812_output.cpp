#include "ws2812_output.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "pico/time.h"

namespace gear {

namespace {

constexpr float kBitRate = 800000.0f;
constexpr uint32_t kUsPerPixel = 30;  // 24 bits at 1.25 us
constexpr uint32_t kLatchUs = 300;    // reset gap (WS2812B needs > 280 us)

// WS2812 state machine (after the pico-examples ws2812 program, BSD-3-Clause,
// Raspberry Pi), assembled at runtime with the SDK's instruction encoders so
// the build needs no host tools (pioasm):
//
//   .side_set 1
//   bitloop: out x, 1       side 0 [T3 - 1]
//            jmp !x do_zero side 1 [T1 - 1]
//   do_one:  jmp bitloop    side 1 [T2 - 1]
//   do_zero: nop            side 0 [T2 - 1]
//
// 10 PIO cycles per bit: "0" = 3 high / 7 low, "1" = 6 high / 4 low.
constexpr unsigned kT1 = 3, kT2 = 3, kT3 = 4;

uint16_t g_program[4];

pio_program_t buildProgram() {
    g_program[0] = static_cast<uint16_t>(pio_encode_out(pio_x, 1) | pio_encode_sideset(1, 0) | pio_encode_delay(kT3 - 1));
    g_program[1] = static_cast<uint16_t>(pio_encode_jmp_not_x(3) | pio_encode_sideset(1, 1) | pio_encode_delay(kT1 - 1));
    g_program[2] = static_cast<uint16_t>(pio_encode_jmp(0) | pio_encode_sideset(1, 1) | pio_encode_delay(kT2 - 1));
    g_program[3] = static_cast<uint16_t>(pio_encode_nop() | pio_encode_sideset(1, 0) | pio_encode_delay(kT2 - 1));
    pio_program_t p{};
    p.instructions = g_program;
    p.length = 4;
    p.origin = -1;  // relocatable: pio_add_program fixes up the jump targets
    return p;
}

PIO const kPio = pio0;

} // namespace

bool Ws2812Output::init(unsigned pin) {
    pio_program_t program = buildProgram();
    if (!pio_can_add_program(kPio, &program)) return false;
    unsigned offset = pio_add_program(kPio, &program);
    int sm = pio_claim_unused_sm(kPio, false);
    if (sm < 0) return false;
    sm_ = static_cast<unsigned>(sm);

    pio_gpio_init(kPio, pin);
    pio_sm_set_consecutive_pindirs(kPio, sm_, pin, 1, true);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 3);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 24);  // shift left, autopull at 24 bits
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    float div = static_cast<float>(clock_get_hz(clk_sys)) / (kBitRate * static_cast<float>(kT1 + kT2 + kT3));
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(kPio, sm_, offset, &c);
    pio_sm_set_enabled(kPio, sm_, true);

    dma_ = dma_claim_unused_channel(false);
    if (dma_ < 0) return false;
    dma_channel_config dc = dma_channel_get_default_config(static_cast<unsigned>(dma_));
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(kPio, sm_, true));
    dma_channel_configure(static_cast<unsigned>(dma_), &dc, &kPio->txf[sm_], words_, 0, false);
    return true;
}

bool Ws2812Output::busy() const { return dma_ >= 0 && dma_channel_is_busy(static_cast<unsigned>(dma_)); }

void Ws2812Output::show(const uint8_t* bytes, int count) {
    if (dma_ < 0 || count <= 0) return;
    if (count > mpx::kMaxPixels) count = mpx::kMaxPixels;
    // Previous frame: DMA done, FIFO drained and latch gap elapsed.
    dma_channel_wait_for_finish_blocking(static_cast<unsigned>(dma_));
    while (time_us_64() < readyAtUs_) tight_loop_contents();

    for (int i = 0; i < count; ++i) {
        const uint8_t* p = bytes + i * 3;
        words_[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                    (static_cast<uint32_t>(p[2]) << 8);
    }
    readyAtUs_ = time_us_64() + static_cast<uint64_t>(count) * kUsPerPixel + kLatchUs;
    dma_channel_transfer_from_buffer_now(static_cast<unsigned>(dma_), words_, static_cast<uint32_t>(count));
}

} // namespace gear
