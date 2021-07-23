#include <inttypes.h>

#include "libc.h"

#include "common.h"

void low_uart_put(int ch) {
    volatile uint32_t *uart_reg0 = (volatile uint32_t*)0x11003014;
    volatile uint32_t *uart_reg1 = (volatile uint32_t*)0x11003000;

    while ( !((*uart_reg0) & 0x20) )
    {}

    *uart_reg1 = ch;
}

void _putchar(char character)
{
    if (character == '\n')
        low_uart_put('\r');
    low_uart_put(character);
}

size_t (*original_read)(struct device_t *dev, uint64_t block_off, void *dst, uint32_t sz, uint32_t part); 
uint32_t * boot_reason = 0;
uint8_t * y_cable = 0;
uint8_t boot_system = 0;

uint64_t g_boot, g_recovery, g_lk, g_misc;

void ** ramdisk = 0;
void ** rootfs_mem_offset = (void *)0x41e557e4;
uint32_t * rootfs_size = 0;

size_t read_func(struct device_t *dev, uint64_t block_off, void *dst, uint32_t sz, uint32_t part) {
    printf("read_func hook\n");
    int ret = 0;
    if (block_off == g_boot * 0x200 || block_off == g_recovery * 0x200) {
        printf("demangle boot image - from 0x%08X\n", __builtin_return_address(0));

        if (block_off == g_boot * 0x200 && *y_cable == 1 && !boot_system && *boot_reason == 0) {
            printf("Boot into recovery to display bootmenu");
            block_off = g_recovery * 0x200;
        }
        if (sz < 0x400) {
            ret = original_read(dev, block_off + 0x400, dst, sz, part);
            ramdisk = dst + 0x14;
            rootfs_size = dst + 0x10;
            printf("ramdisk: 0x%08X\n", *ramdisk);
            printf("rootfs_size: 0x%08X\n", *rootfs_size);
            printf("rootfs_mem_offset: 0x%08X\n", *rootfs_mem_offset);
        } else {
            void *second_copy = (char*)dst + 0x400;
            ret = original_read(dev, block_off, dst, sz, part);
            memcpy(dst, second_copy, 0x400);
            memset(second_copy, 0, 0x400);
            if(*g_boot_mode != 2 && *ramdisk && *rootfs_mem_offset && *rootfs_size){
                // We want ramdisk during normal boot to support Magisk etc.
                printf("copy ramdisk from 0x%08X to 0x%08X (size = 0x%08X)\n", *rootfs_mem_offset, *ramdisk, *rootfs_size);
                memcpy(*ramdisk, *rootfs_mem_offset, *rootfs_size);
            }
        }
    } else {
        ret = original_read(dev, block_off, dst, sz, part);
    }
    return ret;
}

static void parse_gpt() {
    uint8_t raw[0x800] = { 0 };
    struct device_t *dev = get_device();
    dev->read(dev, 0x400, raw, sizeof(raw), USER_PART);
    for (size_t i = 0; i < sizeof(raw) / 0x80; ++i) {
        uint8_t *ptr = &raw[i * 0x80];
        uint8_t *name = ptr + 0x38;
        uint32_t start;
        memcpy(&start, ptr + 0x20, 4);
        if (memcmp(name, "b\x00o\x00o\x00t\x00\x00\x00", 10) == 0) {
            printf("found boot at 0x%08X\n", start);
            g_boot = start;
        } else if (memcmp(name, "r\x00\x65\x00\x63\x00o\x00v\x00\x65\x00r\x00y\x00\x00\x00", 18) == 0) {
            printf("found recovery at 0x%08X\n", start);
            g_recovery = start;
        } else if (memcmp(name, "l\x00k\x00\x00\x00", 6) == 0) {
            printf("found lk at 0x%08X\n", start);
            g_lk = start;
        } else if (memcmp(name, "M\x00I\x00S\x00\x43\x00\x00\x00", 10) == 0) {
            printf("found misc at 0x%08X\n", start);
            g_misc = start;
        }
    }
}

int main() {
    int ret = 0;
    printf("This is LK-payload for mantis by xyz and k4y0z. Copyright 2019\n");
    printf("Ported to sheldon/p by k4y0z, t0x1cSH and R0rt1z2. Copyright 2021\n");

    uint32_t **argptr = (void*)0x41E00020;
    uint32_t *arg = *argptr;
    uint32_t* o_boot_mode = (uint32_t*) *argptr + 1; // argptr boot mode

    int fastboot = 0;

    boot_reason = (uint32_t *)(*(uint32_t *)(0x41E5E954) + 272);
    y_cable = (uint8_t *)(*(uint32_t *)(0x41E5E954) + 365);

    printf("boot_reason: %u\n", *boot_reason);
    printf("y_cable: %u\n", *y_cable);

    parse_gpt();

    if (!g_boot || !g_recovery || !g_lk) {
        printf("failed to find boot, recovery or lk\n");
        printf("falling back to fastboot mode\n");
        fastboot = 1;
    }


    uint8_t bootloader_msg[0x20] = { 0 };

    // Need to read this before we restore the data
    uint8_t *tmp = (void*)0x41e0062c;

    // microloader
    if (strncmp(tmp, "FASTBOOT_PLEASE", 15) == 0 ) {
      fastboot = 1;
    }

    struct device_t *dev = get_device();

    // Restore the 0x41E00200-0x41E01200 range, a part of it was overwritten by microloader
    // this is way more than we actually need to restore, but it shouldn't hurt
    // we can't restore 0x41E00000-0x41E00200 as that contains important pointers
    dev->read(dev, g_lk * 0x200 + 0x200 + 0x200, (char*)LK_BASE + 0x200, 0x1000, USER_PART); // +0x200 to skip lk header

    //Check if backup payload is present and copy if not
    dev->read(dev, BACKUP_SRC, (void*)0x45000000, PAYLOAD_SIZE, BOOT0_PART); // boot0 partition, read 512K
    if (memcmp((void*)PAYLOAD_DST, (void*)0x45000000, PAYLOAD_SIZE)) {
	printf("Backup payload not found...\n");
	printf("...copy payload to backup location\n");
        dev->write(dev, (void*)PAYLOAD_DST, BACKUP_SRC, PAYLOAD_SIZE, BOOT0_PART);
    }

    // factory and factory advanced boot
    if(*o_boot_mode == 4 ) {
      fastboot = 1;
    }

    // use advanced factory mode to boot recovery
    else if(*o_boot_mode == 6) {
      *g_boot_mode = 2;
    }

    else if(g_misc) {
      // Read amonet-flag from MISC partition
      dev->read(dev, g_misc * 0x200, bootloader_msg, 0x20, USER_PART);
      //dev->read(dev, g_misc * 0x200 + 0x4000, bootloader_msg, 0x10, USER_PART);
      printf("bootloader_msg: %s\n", bootloader_msg);

      // temp flag on MISC
      if(strncmp(bootloader_msg, "boot-amonet", 11) == 0) {
        fastboot = 1;
        // reset flag
        memset(bootloader_msg, 0, 0x10);
        dev->write(dev, bootloader_msg, g_misc * 0x200, 0x10, USER_PART);
      }

      // perm flag on MISC
      else if(strncmp(bootloader_msg, "FASTBOOT_PLEASE", 15) == 0) {
        // only reset flag in recovery-boot
        if(*g_boot_mode == 2) {
          memset(bootloader_msg, 0, 0x10);
          dev->write(dev, bootloader_msg, g_misc * 0x200, 0x10, USER_PART);
        }
        else {
          fastboot = 1;
        }
      }

      // recovery flag on MISC
      else if(strncmp(bootloader_msg, "boot-recovery", 13) == 0) {
        *g_boot_mode = 2;
        // reset flag
        memset(bootloader_msg, 0, 0x10);
        dev->write(dev, bootloader_msg, g_misc * 0x200, 0x10, USER_PART);
      }

      // force normal boot flag on MISC
      else if(strncmp(bootloader_msg, "boot-system", 11) == 0) {
        boot_system = 1;
        // reset flag
        memset(bootloader_msg, 0, 0x10);
        dev->write(dev, bootloader_msg, g_misc * 0x200, 0x10, USER_PART);
      }

      // UART flag on MISC
      if(strncmp(bootloader_msg + 0x10, "UART_PLEASE", 11) == 0) {
        // Force uart enable
        char* disable_uart = (char*)0x41e3b663;
        strcpy(disable_uart, "printk.disable_uart=0");
      }

    }

    uint16_t *patch;

    // force fastboot mode
    if (fastboot) {
        printf("well since you're asking so nicely...\n");

	if(*g_boot_mode == 2) *o_boot_mode = 2;
	
        *g_boot_mode = 99;

        video_printf("=> HACKED FASTBOOT mode: (%d) - xyz, k4y0z, t0x1cSH, R0rt1z2\n", *o_boot_mode);
    }
    else if(*g_boot_mode == 2) {
        video_printf("=> RECOVERY mode...");
    }

    printf("g_boot_mode %u\n", *g_boot_mode);
    printf("o_boot_mode %u\n", *o_boot_mode);

    // device is unlocked
    patch = (void*)0x41E01D34;
    *patch++ = 0x2001; // movs r0, #1
    *patch = 0x4770;   // bx lr

    // amzn_verify_unlock (enable all commands)
    patch = (void*)0x41e01ea8;
    *patch++ = 0x2000; // movs r0, #0
    *patch = 0x4770;   // bx lr

    // We want ramdisk during normal boot to support Magisk etc.
    patch = (void*)0x41e24f1a;
    *patch++ = 0xe010;

    // hook bootimg read function
    uint32_t *patch32;
    original_read = (void*)dev->read;
    patch32 = (void*)&dev->read;
    *patch32 = (uint32_t)read_func;

    printf("Clean lk\n");
    cache_clean((void*)LK_BASE, LK_SIZE);

    printf("Jump lk\n");
    int (*app)() = (void*)0x41e2585d;
    app();

    while (1) {

    }
}
