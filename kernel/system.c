#include "system.h"

extern char __kernel_start[];
extern char __kernel_end[];

static bool cpuid_available(void)
{
#ifdef GRAPE_FIRMWARE
    return true;
#else
    u32 original, modified;
    /* EFLAGS.ID is writable exactly when the processor supports CPUID. */
    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"
        : "=&r" (original), "=&r" (modified)
        : : "cc", "memory"
    );
    return ((original ^ modified) & 0x200000u) != 0;
#endif
}

static void cpuid(u32 leaf, u32 result[4])
{
    u32 eax = leaf, ebx, ecx = 0, edx;
    __asm__ volatile ("cpuid"
        : "+a" (eax), "=b" (ebx), "+c" (ecx), "=d" (edx));
    result[0] = eax;
    result[1] = ebx;
    result[2] = ecx;
    result[3] = edx;
}

static void copy_word(char *destination, u32 word)
{
    for (u32 byte = 0; byte < 4; ++byte)
        destination[byte] = (char)(word >> (8 * byte));
}

static void trim_name(char name[49])
{
    u32 first = 0, length = 0;
    while (length < 48 && name[length])
        ++length;
    while (first < length && name[first] == ' ')
        ++first;
    while (length > first && name[length - 1] == ' ')
        --length;
    for (u32 index = first; index < length; ++index)
        name[index - first] = name[index];
    name[length - first] = '\0';
}

void system_cpu_name(char name[49])
{
    u32 result[4];
    if (!cpuid_available()) {
        name[0] = 'i';
        name[1] = '3';
        name[2] = '8';
        name[3] = '6';
        name[4] = '\0';
        return;
    }
    cpuid(0x80000000u, result);
    if (result[0] >= 0x80000004u) {
        for (u32 leaf = 0; leaf < 3; ++leaf) {
            cpuid(0x80000002u + leaf, result);
            for (u32 word = 0; word < 4; ++word)
                copy_word(name + leaf * 16 + word * 4, result[word]);
        }
        name[48] = '\0';
        trim_name(name);
        if (name[0])
            return;
    }
    cpuid(0, result);
    copy_word(name, result[1]);
    copy_word(name + 4, result[3]);
    copy_word(name + 8, result[2]);
    name[12] = '\0';
    trim_name(name);
    if (!name[0]) {
        name[0] = 'i';
        name[1] = '3';
        name[2] = '8';
        name[3] = '6';
        name[4] = '\0';
    }
}

u32 system_kernel_bytes(void)
{
    return (u32)((usize)__kernel_end - (usize)__kernel_start);
}
