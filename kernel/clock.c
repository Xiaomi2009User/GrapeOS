#include "io.h"
#include "platform.h"

typedef unsigned long long u64;
static u64 clock_base;
static u32 cycles_per_tick, frequency_khz;
static const char *clock_source = "PIT IRQ0";

static u64 timestamp(void)
{
    u32 low, high;
    __asm__ volatile("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");
    return ((u64)high << 32) | low;
}

/* Return the quotient modulo 2^32, matching the existing tick counter.
 * A 32-bit fast path keeps the frequently polled clock inexpensive.
 */
static u32 divide(u64 value, u32 divisor)
{
    if (!(value >> 32)) return (u32)value / divisor;
    u64 remainder = 0;
    u32 answer = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((value >> bit) & 1u);
        if (remainder >= divisor) {
            remainder -= divisor;
            if (bit < 32) answer |= 1u << bit;
        }
    }
    return answer;
}

static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static u32 calibrate_channel2(void)
{
    /* Channel 2 runs independently of PIC/APIC routing. The timeout is
     * bounded even when a machine has no working legacy PIT at all. */
    u8 speaker = inb(0x61);
    outb(0x61, speaker & ~3u);
    outb(0x43, 0xb0);
    outb(0x42, (u8)23864);
    outb(0x42, (u8)(23864 >> 8));
    u64 started = timestamp();
    outb(0x61, (speaker & ~2u) | 1u);
    bool low_seen = false, completed = false;
    u64 elapsed = 0;
    for (u32 retry = 0; retry < 2000000u; ++retry) {
        u8 status = inb(0x61);
        elapsed = timestamp() - started;
        if (!(status & 0x20)) low_seen = true;
        if (low_seen && (status & 0x20)) { completed = true; break; }
        if (elapsed > 1000000000ull) break;
        __asm__ volatile("pause");
    }
    outb(0x61, speaker);
    if (!completed || elapsed < 200000u || elapsed > 200000000u) return 0;
    return divide(elapsed, 20u * 1000u);
}

void cpu_clock_init(void)
{
    u32 maximum, a, b, c, d;
    cpuid(0, &maximum, &b, &c, &d);
    if (maximum >= 0x15) {
        cpuid(0x15, &a, &b, &c, &d);
        if (a && b && c) {
            /* CPUID.15: TSC Hz = crystal Hz * numerator / denominator. */
            u32 khz = divide((u64)(c / 1000u) * b, a);
            if (khz >= 100000u && khz <= 10000000u) {
                frequency_khz = khz;
                clock_source = "CPU TSC / CPUID.15";
            }
        }
    }
    if (!frequency_khz) {
        u32 mhz = calibrate_channel2();
        if (mhz >= 100 && mhz <= 10000) {
            frequency_khz = mhz * 1000u;
            clock_source = "CPU TSC / PIT channel 2 calibration";
        }
    }
    if (!frequency_khz && maximum >= 0x16) {
        cpuid(0x16, &a, &b, &c, &d);
        if (a >= 100 && a <= 10000) {
            frequency_khz = a * 1000u;
            clock_source = "CPU TSC / estimated CPUID.16";
        }
    }
    if (!frequency_khz) {
        frequency_khz = 1000000u;
        clock_source = "CPU TSC / approximate fallback";
    }
    cycles_per_tick = frequency_khz * (1000u / TIMER_HZ);
    clock_base = timestamp();
}

u32 cpu_clock_ticks(void) { return divide(timestamp() - clock_base, cycles_per_tick); }
const char *platform_clock_source(void) { return clock_source; }
u32 platform_clock_khz(void) { return frequency_khz; }
