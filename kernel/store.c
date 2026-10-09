#include "bootinfo.h"
#include "io.h"
#include "platform.h"
#include "store.h"

#define ATA_DATA 0x1f0u
#define ATA_COUNT 0x1f2u
#define ATA_LBA_LOW 0x1f3u
#define ATA_LBA_MID 0x1f4u
#define ATA_LBA_HIGH 0x1f5u
#define ATA_DRIVE 0x1f6u
#define ATA_STATUS 0x1f7u
#define ATA_CONTROL 0x3f6u
#define ATA_BUSY 0x80u
#define ATA_READY 0x40u
#define ATA_FAULT 0x20u
#define ATA_DRQ 0x08u
#define ATA_ERROR 0x01u
#define ATA_POLL_LIMIT 4000000u
#define ATA_WAIT_TICKS (2u * TIMER_HZ)
#define STORE_DISK_SECTORS 2048u
#define RECORD_BYTES 512u
#define RECORD_CHECKSUM 508u
#define STORE_VERSION 1u
#define APP_MASK (STORE_APP_SNAKE | STORE_APP_NOTES | STORE_APP_GUESS)

static const u8 record_magic[8] = {'G', 'R', 'A', 'P', 'E', 'S', 'T', '1'};
static struct store_state current_state;
static bool disk_present;
static u32 generation;
static u8 active_slot;

static u16 ata_inw(void)
{
    u16 value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"((u16)ATA_DATA));
    return value;
}

static void ata_outw(u16 value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"((u16)ATA_DATA));
}

static void ata_delay(void)
{
    for (u32 count = 0; count < 4; ++count)
        (void)inb(ATA_CONTROL);
}

static bool ata_wait(bool data_ready, bool check_error)
{
    u32 started = timer_ticks();
    for (u32 retry = 0; retry < ATA_POLL_LIMIT; ++retry) {
        u8 status = inb(ATA_STATUS);
        if (status == 0xff)
            return false;
        /* Zero can be a transient device state, including during reset.
         * A PIT deadline allows asynchronous disk I/O to finish; the poll
         * cap also terminates if interrupts or the timer are unavailable.
         */
        if (status != 0 && !(status & ATA_BUSY)) {
            if (check_error && (status & (ATA_ERROR | ATA_FAULT)))
                return false;
            if (data_ready ? (status & ATA_DRQ) != 0 :
                             (status & ATA_READY) != 0 && (status & ATA_DRQ) == 0)
                return true;
        }
        if ((u32)(timer_ticks() - started) >= ATA_WAIT_TICKS)
            return false;
        __asm__ volatile("pause");
    }
    return false;
}

static bool ata_identify(void)
{
    u16 identify[256];
    outb(ATA_CONTROL, 0x02); /* Disable ATA interrupts; transfers use polling. */
    outb(ATA_DRIVE, 0xa0);   /* Primary master: the separate persistent disk. */
    ata_delay();
    if (!ata_wait(false, false))
        return false;
    outb(ATA_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_STATUS, 0xec);
    ata_delay();
    if (!ata_wait(true, true))
        return false;
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0)
        return false; /* An ATAPI device is not our writable ATA data disk. */
    for (u32 word = 0; word < 256; ++word)
        identify[word] = ata_inw();
    u32 sectors = (u32)identify[60] | ((u32)identify[61] << 16);
    /* Only the launcher-created 1 MiB volume is supported. Never write the
     * journal into an unrelated primary-master disk's partition sectors.
     */
    return (identify[49] & (1u << 9)) != 0 && sectors == STORE_DISK_SECTORS &&
           ata_wait(false, true);
}

static bool ata_begin(u32 lba, u8 command)
{
    outb(ATA_DRIVE, (u8)(0xe0u | ((lba >> 24) & 0x0fu)));
    ata_delay();
    if (!ata_wait(false, false))
        return false;
    outb(ATA_COUNT, 1);
    outb(ATA_LBA_LOW, (u8)lba);
    outb(ATA_LBA_MID, (u8)(lba >> 8));
    outb(ATA_LBA_HIGH, (u8)(lba >> 16));
    outb(ATA_STATUS, command);
    ata_delay();
    return ata_wait(true, true);
}

static bool ata_read(u32 lba, u8 sector[RECORD_BYTES])
{
    if (!ata_begin(lba, 0x20))
        return false;
    for (u32 word = 0; word < 256; ++word) {
        u16 value = ata_inw();
        sector[word * 2] = (u8)value;
        sector[word * 2 + 1] = (u8)(value >> 8);
    }
    ata_delay();
    return ata_wait(false, true);
}

static bool ata_write(u32 lba, const u8 sector[RECORD_BYTES])
{
    if (!ata_begin(lba, 0x30))
        return false;
    for (u32 word = 0; word < 256; ++word)
        ata_outw((u16)((u16)sector[word * 2] |
                      ((u16)sector[word * 2 + 1] << 8)));
    ata_delay();
    if (!ata_wait(false, true))
        return false;
    outb(ATA_STATUS, 0xe7); /* FLUSH CACHE before acknowledging a saved state. */
    ata_delay();
    return ata_wait(false, true);
}

static u32 read_u32(const u8 *bytes)
{
    return (u32)bytes[0] | ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

static void write_u32(u8 *bytes, u32 value)
{
    for (u32 index = 0; index < 4; ++index)
        bytes[index] = (u8)(value >> (index * 8));
}

static u32 checksum(const u8 record[RECORD_BYTES])
{
    u32 value = 2166136261u;
    for (u32 index = 0; index < RECORD_CHECKSUM; ++index) {
        value ^= record[index];
        value *= 16777619u;
    }
    return value;
}

static bool valid_string(const char *text, u32 capacity, bool allow_empty,
                         bool allow_whitespace)
{
    for (u32 index = 0; index < capacity; ++index) {
        u8 character = (u8)text[index];
        if (character == 0)
            return allow_empty || index != 0;
        if (character >= 32 && character <= 126)
            continue;
        if (allow_whitespace && (character == '\n' || character == '\r' ||
                                 character == '\t'))
            continue;
        return false;
    }
    return false;
}

static bool valid_state(const struct store_state *state)
{
    return state != NULL && state->configured <= 1 && state->foreground < 16 &&
           state->background < 8 && (state->installed_apps & ~APP_MASK) == 0 &&
           valid_string(state->username, sizeof(state->username), false, false) &&
           valid_string(state->hostname, sizeof(state->hostname), false, false) &&
           valid_string(state->note, sizeof(state->note), true, true);
}

static void encode_string(u8 *destination, const char *source, u32 capacity)
{
    for (u32 index = 0; index < capacity && source[index]; ++index)
        destination[index] = (u8)source[index];
}

static void encode(u8 record[RECORD_BYTES], const struct store_state *state,
                   u32 next_generation)
{
    for (u32 index = 0; index < RECORD_BYTES; ++index)
        record[index] = 0;
    for (u32 index = 0; index < sizeof(record_magic); ++index)
        record[index] = record_magic[index];
    write_u32(record + 8, STORE_VERSION);
    write_u32(record + 12, next_generation);
    record[16] = state->configured;
    record[17] = state->foreground;
    record[18] = state->background;
    write_u32(record + 20, state->installed_apps);
    write_u32(record + 24, state->snake_best);
    encode_string(record + 28, state->username, sizeof(state->username));
    encode_string(record + 52, state->hostname, sizeof(state->hostname));
    encode_string(record + 76, state->note, sizeof(state->note));
    write_u32(record + RECORD_CHECKSUM, checksum(record));
}

static bool decode(const u8 record[RECORD_BYTES], struct store_state *state,
                   u32 *record_generation)
{
    for (u32 index = 0; index < sizeof(record_magic); ++index)
        if (record[index] != record_magic[index])
            return false;
    if (read_u32(record + 8) != STORE_VERSION ||
        read_u32(record + RECORD_CHECKSUM) != checksum(record))
        return false;
    state->configured = record[16];
    state->foreground = record[17];
    state->background = record[18];
    state->installed_apps = read_u32(record + 20);
    state->snake_best = read_u32(record + 24);
    for (u32 index = 0; index < sizeof(state->username); ++index)
        state->username[index] = (char)record[28 + index];
    for (u32 index = 0; index < sizeof(state->hostname); ++index)
        state->hostname[index] = (char)record[52 + index];
    for (u32 index = 0; index < sizeof(state->note); ++index)
        state->note[index] = (char)record[76 + index];
    *record_generation = read_u32(record + 12);
    return valid_state(state);
}

void store_init(void)
{
    struct store_state initial = {0};
    encode_string((u8 *)initial.username, "grape", sizeof(initial.username));
    encode_string((u8 *)initial.hostname, "grapeos", sizeof(initial.hostname));
    initial.foreground = 7;
    current_state = initial;
    generation = 0;
    active_slot = 1; /* The first successful save writes slot zero. */
    /* The USB live image must never probe or write the host's ATA disks.
     * GrapeLoad explicitly sets this flag on every boot, including reboot.
     */
    const volatile u32 *boot_info = (const volatile u32 *)BOOT_INFO_ADDRESS;
    disk_present = false;
    if (boot_info[0] == BOOT_INFO_MAGIC && boot_info[1] == BOOT_INFO_LIVE_USB)
        return;
    disk_present = ata_identify();
    if (!disk_present)
        return;

    bool any_read = false;
    bool have_valid = false;
    for (u8 slot = 0; slot < 2; ++slot) {
        u8 record[RECORD_BYTES];
        struct store_state candidate;
        u32 candidate_generation;
        if (!ata_read(slot, record))
            continue;
        any_read = true;
        if (!decode(record, &candidate, &candidate_generation))
            continue;
        u32 distance = candidate_generation - generation;
        if (!have_valid || (distance != 0 && distance < 0x80000000u)) {
            current_state = candidate;
            generation = candidate_generation;
            active_slot = slot;
            have_valid = true;
        }
    }
    if (!any_read)
        disk_present = false;
}

const struct store_state *store_get(void)
{
    return &current_state;
}

bool store_available(void)
{
    return disk_present;
}

bool store_save(const struct store_state *candidate)
{
    if (!valid_state(candidate))
        return false;
    if (!disk_present) {
        current_state = *candidate;
        return false;
    }

    u8 record[RECORD_BYTES];
    u8 verified[RECORD_BYTES];
    u8 target = active_slot ^ 1u;
    u32 next_generation = generation + 1;
    encode(record, candidate, next_generation);
    if (!ata_write(target, record) || !ata_read(target, verified))
        return false;
    for (u32 index = 0; index < RECORD_BYTES; ++index)
        if (record[index] != verified[index])
            return false;
    current_state = *candidate;
    generation = next_generation;
    active_slot = target;
    return true;
}
