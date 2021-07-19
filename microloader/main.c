#include <inttypes.h>
#include <stddef.h>

#include "../lk-payload/common.h"

void low_uart_put(int ch) {
    volatile uint32_t *uart_reg0 = (volatile uint32_t*)0x11003014;
    volatile uint32_t *uart_reg1 = (volatile uint32_t*)0x11003000;

    while ( !((*uart_reg0) & 0x20) )
    {}

    *uart_reg1 = ch;
}

int putchar(int character) {
    if (character == '\n')
        low_uart_put('\r');
    low_uart_put(character);
    return character;
}

int puts(const char *line) {
    for (const char *c = line; *c; ++c) {
        putchar(*c);
    }
    putchar('\n');
    return 0;
}

int main() {
    cache_clean((void *)0x41E0037C, 0x400);
    puts("microloader");

    struct device_t *dev = (void*)get_device();

    uint32_t *dst = (void*)PAYLOAD_DST;
    size_t ret = dev->read(dev, PAYLOAD_SRC, dst, PAYLOAD_SIZE, BOOT0_PART); // boot0 partition, read 512K


    if(*dst != 0xe3a0d442) {
        puts("Try backup");
        size_t ret = dev->read(dev, BACKUP_SRC, dst, PAYLOAD_SIZE, BOOT0_PART); // boot0 partition, read 512K
    }

    cache_clean(dst, PAYLOAD_SIZE);

    // Jump to the payload
    void (*jump)(void) = (void*)dst;
    puts("Jump");
    jump();

    //puts("Something went horribly wrong!");

    while (1) {

    }
}
