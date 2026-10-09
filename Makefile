CROSS ?=
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
PYTHON ?= python3
QEMU ?= qemu-system-i386
QEMU_BIOS_DIR ?=
QEMU_FLAGS ?=
GRUB_MKSTANDALONE ?= grub-mkstandalone
GRUB_FONT ?= /usr/share/grub/ascii.pf2

BUILD := build
IMAGE := $(BUILD)/grapeos.img
USB_IMAGE := $(BUILD)/grapeos-usb.img
UEFI_IMAGE := $(BUILD)/grapeos-uefi.img
UEFI_KERNEL := $(BUILD)/grapeos-uefi.elf
UEFI_BOOT := $(BUILD)/BOOTX64.EFI
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
BOOT_BIN := $(BUILD)/boot.bin
STAGE2_BIN := $(BUILD)/grapeload.bin
USB_BOOT_BIN := $(BUILD)/usb.bin
USB_STAGE2_BIN := $(BUILD)/grapeload-usb.bin
DATA_IMAGE := data/grapeos-data.img

CPPFLAGS := -Iinclude
CFLAGS := -m32 -std=c11 -O2 -g -ffreestanding -fno-builtin \
	-fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -mno-sse -mno-sse2 -mno-mmx \
	-msoft-float -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2 \
	-Wall -Wextra -Werror -Wpedantic
ASFLAGS := -m32 -g -ffreestanding -fno-pic -fno-pie
KERNEL_C := $(wildcard kernel/*.c)
KERNEL_ASM := $(wildcard kernel/*.S)
KERNEL_OBJECTS := $(BUILD)/boot/entry.o \
	$(patsubst %.c,$(BUILD)/%.o,$(KERNEL_C)) \
	$(patsubst %.S,$(BUILD)/%.o,$(KERNEL_ASM))
DEPS := $(KERNEL_OBJECTS:.o=.d)

.PHONY: all image usb uefi check check-uefi test test-uefi run run-headless clean
all: image usb $(DATA_IMAGE)
image: $(IMAGE)
usb: $(USB_IMAGE)
uefi: $(UEFI_IMAGE)

$(BUILD)/kernel/framebuffer.o: assets/framebuffer-font.h

$(UEFI_KERNEL): $(BUILD)/boot/uefi_entry.o $(filter-out $(BUILD)/boot/entry.o,$(KERNEL_OBJECTS)) boot/uefi.ld
	$(LD) -m elf_i386 -nostdlib -T boot/uefi.ld -Map $(BUILD)/grapeos-uefi.map \
		-o $@ $(BUILD)/boot/uefi_entry.o $(filter-out $(BUILD)/boot/entry.o,$(KERNEL_OBJECTS))
	grub-file --is-x86-multiboot2 $@

$(UEFI_BOOT): $(UEFI_KERNEL) boot/uefi-grub.cfg
	$(GRUB_MKSTANDALONE) -O x86_64-efi --locales= --fonts= --compress=xz \
		--modules="part_gpt fat multiboot2 normal efi_gop gfxterm font serial" \
		-o $@ "boot/grub/grub.cfg=boot/uefi-grub.cfg" \
		"boot/grapeos.elf=$(UEFI_KERNEL)" "boot/ascii.pf2=$(GRUB_FONT)"

$(UEFI_IMAGE): $(UEFI_BOOT) scripts/build_uefi.py
	$(PYTHON) scripts/build_uefi.py --efi $(UEFI_BOOT) --output $@

check-uefi: $(UEFI_IMAGE)
	$(PYTHON) scripts/build_uefi.py --check --efi $(UEFI_BOOT) --output $(UEFI_IMAGE)

test-uefi: check-uefi
	$(PYTHON) tests/uefi.py --qemu "$(QEMU)"
	$(PYTHON) tests/uefi_highfb.py --qemu "$(QEMU)"

$(DATA_IMAGE): scripts/ensure_data.py
	$(PYTHON) scripts/ensure_data.py "$@"

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJECTS) linker.ld
	$(LD) -m elf_i386 -nostdlib -T linker.ld -Map $(BUILD)/kernel.map \
		-o $@ $(KERNEL_OBJECTS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/boot/boot.o: boot/boot.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/boot/grapeload.o: boot/grapeload.S include/bootinfo.h $(KERNEL_BIN) scripts/build_image.py
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -DKERNEL_SECTORS=$$($(PYTHON) scripts/build_image.py \
		--kernel-sectors $(KERNEL_BIN)) -c $< -o $@

$(BUILD)/boot/usb.o: boot/usb.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/boot/grapeload-usb.o: boot/grapeload.S include/bootinfo.h $(KERNEL_BIN) scripts/build_image.py
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -DUSB_BOOT -DKERNEL_SECTORS=$$($(PYTHON) scripts/build_image.py \
		--kernel-sectors $(KERNEL_BIN)) -c $< -o $@

$(BUILD)/boot/usb.elf: $(BUILD)/boot/usb.o boot/boot.ld
	$(LD) -m elf_i386 -nostdlib -T boot/boot.ld -o $@ $<

$(USB_BOOT_BIN): $(BUILD)/boot/usb.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/boot/grapeload-usb.elf: $(BUILD)/boot/grapeload-usb.o boot/grapeload.ld
	$(LD) -m elf_i386 -nostdlib -T boot/grapeload.ld -o $@ $<

$(USB_STAGE2_BIN): $(BUILD)/boot/grapeload-usb.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/boot/boot.elf: $(BUILD)/boot/boot.o boot/boot.ld
	$(LD) -m elf_i386 -nostdlib -T boot/boot.ld -o $@ $<

$(BOOT_BIN): $(BUILD)/boot/boot.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/boot/grapeload.elf: $(BUILD)/boot/grapeload.o boot/grapeload.ld
	$(LD) -m elf_i386 -nostdlib -T boot/grapeload.ld -o $@ $<

$(STAGE2_BIN): $(BUILD)/boot/grapeload.elf
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) scripts/build_image.py
	$(PYTHON) scripts/build_image.py --boot $(BOOT_BIN) \
		--stage2 $(STAGE2_BIN) --kernel $(KERNEL_BIN) --output $@

$(USB_IMAGE): $(USB_BOOT_BIN) $(USB_STAGE2_BIN) $(KERNEL_BIN) scripts/build_usb.py scripts/build_image.py
	$(PYTHON) scripts/build_usb.py --boot $(USB_BOOT_BIN) \
		--stage2 $(USB_STAGE2_BIN) --kernel $(KERNEL_BIN) --output $@

check: $(IMAGE) $(USB_IMAGE)
	$(PYTHON) scripts/build_image.py --check --boot $(BOOT_BIN) \
		--stage2 $(STAGE2_BIN) --kernel $(KERNEL_BIN) --output $(IMAGE)
	$(PYTHON) scripts/build_usb.py --check --boot $(USB_BOOT_BIN) \
		--stage2 $(USB_STAGE2_BIN) --kernel $(KERNEL_BIN) --output $(USB_IMAGE)

test: check
	$(PYTHON) tests/smoke.py --qemu "$(QEMU)"
	$(PYTHON) tests/gui.py --qemu "$(QEMU)"
	$(PYTHON) tests/usb.py --qemu "$(QEMU)"

run:
	+./run.sh

run-headless:
	+./run.sh --headless

clean:
	rm -rf $(BUILD)

-include $(DEPS)

# Optional x86_64 desktop retaining firmware services for real USB files/input.
EFI_ROOT ?= $(if $(wildcard /usr/include/efi/efi.h),/usr,$(CURDIR)/.runtime/qemu/usr)
DESKTOP_C := firmware/runtime.c firmware/files.c firmware/photos.c kernel/gui.c kernel/shell.c \
 kernel/setup.c kernel/apps.c kernel/notes.c kernel/guess.c kernel/snake.c \
 kernel/fetch.c kernel/system.c kernel/memory.c kernel/input.c
DESKTOP_O := $(patsubst %.c,$(BUILD)/desktop/%.o,$(DESKTOP_C))
DESKTOP_FLAGS := -m64 -std=c11 -O2 -g -ffreestanding -fno-builtin -fpic \
 -fshort-wchar -mno-red-zone -fno-stack-protector -fno-asynchronous-unwind-tables \
 -DGNU_EFI_USE_MS_ABI -DGRAPE_FIRMWARE -Iinclude -Ifirmware \
 -isystem $(EFI_ROOT)/include/efi -isystem $(EFI_ROOT)/include/efi/x86_64 \
 -Wall -Wextra -Werror
$(BUILD)/desktop/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(DESKTOP_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/grapeos-desktop.so: $(DESKTOP_O)
	$(LD) -nostdlib -znocombreloc -T $(EFI_ROOT)/lib/elf_x86_64_efi.lds \
	 -shared -Bsymbolic --no-undefined --defsym=__kernel_start=0 --defsym=__kernel_end=_edata \
	 --defsym=__bss_start=_data --defsym=__bss_end=_edata \
	 -o $@ $(EFI_ROOT)/lib/crt0-efi-x86_64.o $(DESKTOP_O) -L$(EFI_ROOT)/lib -lgnuefi
$(BUILD)/BOOTX64-desktop.EFI: $(BUILD)/grapeos-desktop.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .dynstr -j .rel -j .rela \
	 -j .reloc -O efi-app-x86_64 $< $@
$(BUILD)/grapeos-desktop.img: $(BUILD)/BOOTX64-desktop.EFI scripts/build_uefi.py assets/images.png
	$(PYTHON) scripts/build_uefi.py --desktop --efi $< --output $@
.PHONY: desktop test-desktop test-renderer test-photos
desktop: $(BUILD)/grapeos-desktop.img
$(BUILD)/test-renderer: tests/renderer.c firmware/runtime.c assets/terminal-font.h
	@mkdir -p $(@D)
	$(CC) $(DESKTOP_FLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections $< -o $@
test-renderer: $(BUILD)/test-renderer
	./$(BUILD)/test-renderer
$(BUILD)/test-photos: tests/photos.c firmware/photos.c assets/sample-image.h third_party/stb/stb_image.h
	@mkdir -p $(@D)
	$(CC) $(DESKTOP_FLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections $< -o $@
test-photos: $(BUILD)/test-photos
	./$(BUILD)/test-photos
test-desktop: desktop test-renderer test-photos
	QEMU="$(QEMU)" $(PYTHON) tests/firmware.py
-include $(DESKTOP_O:.o=.d)
