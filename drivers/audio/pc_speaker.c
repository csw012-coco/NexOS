#include "drivers/audio/pc_speaker.h"

#include "hal/hal.h"

enum {
    PC_SPEAKER_PIT_HZ = 1193182u,
    PC_SPEAKER_PIT_CHANNEL2 = 0x42u,
    PC_SPEAKER_PIT_COMMAND = 0x43u,
    PC_SPEAKER_CONTROL = 0x61u
};

int pc_speaker_beep(uint32_t hz, uint32_t duration_ms) {
    uint32_t divisor;
    uint32_t timer_hz;
    uint32_t wait_ticks;
    uint32_t start_tick;
    uint8_t control;

    if (hz < 20u || hz > 20000u || duration_ms == 0u) {
        return 0;
    }
    divisor = PC_SPEAKER_PIT_HZ / hz;
    if (divisor == 0u || divisor > 0xffffu) {
        return 0;
    }

    control = hal_io_in8(PC_SPEAKER_CONTROL);
    hal_io_out8(PC_SPEAKER_PIT_COMMAND, 0xb6u);
    hal_io_out8(PC_SPEAKER_PIT_CHANNEL2, (uint8_t)(divisor & 0xffu));
    hal_io_out8(PC_SPEAKER_PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xffu));
    hal_io_out8(PC_SPEAKER_CONTROL, (uint8_t)(control | 0x03u));

    timer_hz = hal_timer_hz();
    if (timer_hz == 0u) {
        hal_io_out8(PC_SPEAKER_CONTROL, control);
        return 0;
    }
    wait_ticks = (uint32_t)(((uint64_t)duration_ms * timer_hz + 999u) / 1000u);
    if (wait_ticks == 0u) {
        wait_ticks = 1u;
    }
    start_tick = hal_timer_current_ticks();
    while ((uint32_t)(hal_timer_current_ticks() - start_tick) < wait_ticks) {
        __asm__ volatile("pause");
    }

    hal_io_out8(PC_SPEAKER_CONTROL, control);
    return 1;
}
