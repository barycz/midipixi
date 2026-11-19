#pragma once
// MPX_HOT marks the per-pixel render path. The RP2040 gear build defines
// MPX_HOT_IN_RAM to run it from RAM: flash is read through a 16 KB XIP cache
// that both cores share, and core 0 (USB) evicts the renderer's code from it.

#if defined(MPX_HOT_IN_RAM) && MPX_HOT_IN_RAM
#define MPX_HOT __attribute__((section(".time_critical.mpx")))
#else
#define MPX_HOT
#endif
