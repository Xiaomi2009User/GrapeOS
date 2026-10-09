#include "memory.h"

/* Volatile stores keep the compiler from lowering these loops into themselves. */
void *memcpy(void *destination, const void *source, usize count)
{
    volatile u8 *output = destination;
    const u8 *input = source;
    for (usize index = 0; index < count; ++index)
        output[index] = input[index];
    return destination;
}

void *memmove(void *destination, const void *source, usize count)
{
    volatile u8 *output = destination;
    const u8 *input = source;
    if ((usize)destination <= (usize)source) {
        for (usize index = 0; index < count; ++index)
            output[index] = input[index];
    } else {
        while (count) {
            --count;
            output[count] = input[count];
        }
    }
    return destination;
}

void *memset(void *destination, int value, usize count)
{
    volatile u8 *output = destination;
    for (usize index = 0; index < count; ++index)
        output[index] = (u8)value;
    return destination;
}

int memcmp(const void *left, const void *right, usize count)
{
    const u8 *left_bytes = left;
    const u8 *right_bytes = right;
    for (usize index = 0; index < count; ++index)
        if (left_bytes[index] != right_bytes[index])
            return (int)left_bytes[index] - (int)right_bytes[index];
    return 0;
}
