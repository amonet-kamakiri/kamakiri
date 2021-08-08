#include "common.h"
#include "payload_common.h"

void sleepy(void) {
    // TODO: do better
    for (volatile int i = 0; i < 0x80000; ++i) {}
}

void mdelay (unsigned long msec)
{
    (void)msec;
    sleepy();
}

/* delay usec useconds */
void udelay (unsigned long usec)
{
    (void)usec;
    sleepy();
}

void hex_dump(const void* data, size_t size) {
    size_t i, j;
    for (i = 0; i < size; ++i) {
        printf("%02X ", ((unsigned char*)data)[i]);
        if ((i+1) % 8 == 0 || i+1 == size) {
            printf(" ");
            if ((i+1) % 16 == 0) {
                printf("\n");
            } else if (i+1 == size) {
                if ((i+1) % 16 <= 8) {
                    printf(" ");
                }
                for (j = (i+1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("\n");
            }
        }
    }
}

void command_loop(struct msdc_host *host) {
    char buf[0x200] = { 0 };
    int ret = 0;

    printf("Entering command loop\n");

    send_dword(0xB1B2B3B4);

    while (1) {
        memset(buf, 0, sizeof(buf));
        uint32_t magic = recv_dword();
        if (magic != 0xf00dd00d) {
            printf("Protocol error\n");
            printf("Magic received = 0x%08X\n", magic);
            break;
        }
        uint32_t cmd = recv_dword();
        switch (cmd) {
        case 0x1000: {
            uint32_t block = recv_dword();
            printf("Read block 0x%08X\n", block);
            memset(buf, 0, sizeof(buf));
            if (mmc_read(host, block, buf) != 0) {
                printf("Read error!\n");
            } else {
                send_data(buf, sizeof(buf));
            }
            break;
        }
        case 0x1001: {
            uint32_t block = recv_dword();
            printf("Write block 0x%08X ", block);
            memset(buf, 0, sizeof(buf));
            recv_data(buf, 0x200, 0);
            if (mmc_write(host, block, buf) != 0) {
                printf("Write error!\n");
            } else {
                printf("OK\n");
                send_dword(0xD0D0D0D0);
            }
            break;
        }
        case 0x1002: {
            uint32_t part = recv_dword();
            printf("Switch to partition %d => ", part);
            ret = mmc_set_part(host, part);
            printf("0x%08X\n", ret);
            mdelay(500); // just in case
            break;
        }
        case 0x2000: {
            printf("Read rpmb\n");
            mmc_rpmb_read(host, buf);
            send_data(buf, 0x100);
            break;
        }
        case 0x2001: {
            printf("Write rpmb\n");
            recv_data(buf, 0x100, 0);
            mmc_rpmb_write(host, buf);
            break;
        }
        case 0x5000: {
            uint32_t address = recv_dword();
            uint32_t size = recv_dword();
            printf("Read %d Bytes from address 0x%08X\n", size, address);
            send_data((char*)address, size);
            break;
        }
        case 0x7000: {
            char idme_buf[0x400] = { 0 };
            char field_name[16] = { 0 };
            const char beefdeed[] = "beefdeed";
            uint32_t result = 0;
            recv_data(field_name, 16, 0);
            printf("Read %s from IDME\n", field_name);
            printf("Switch to partition %d => ", 2);
            ret = mmc_set_part(host, 2);
            printf("0x%08X\n", ret);
            mdelay(500); // just in case
            uint32_t block = 0;
            void *type_offset = 0;
            while (!(type_offset = memmem(idme_buf, 0x400, &field_name, 16)) && block < 0x2000) {
                printf("Read block 0x%08X\n", block);
                memset(buf, 0, sizeof(buf));
                if (mmc_read(host, block++, buf) != 0) {
                    printf("Read error!\n");
                    result = 0xffffffff;
                    break;
                }
                if (block == 1 && memcmp(buf, &beefdeed, 8)) {
                    printf("IDME invalid!\n");
                    result = 0xbeefdeed;
                    break;
                }
                memcpy(idme_buf, idme_buf + 0x200, 0x200);
                memcpy(idme_buf + 0x200, buf, 0x200);
            }
            if (type_offset) {
                uint32_t len = *(uint32_t*)(type_offset + 16);
                type_offset += 16 + 12;
                uint32_t buf_len = sizeof(idme_buf) - (type_offset - (void *)idme_buf);
                send_dword(len);
                send_data(type_offset, len < buf_len ? len : buf_len);
                if(len > buf_len) {
                    len -= buf_len;
                    while (len > 0) {
                        printf("Read block 0x%08X\n", block);
                        memset(buf, 0, sizeof(buf));
                        if (mmc_read(host, block++, buf) != 0) {
                            printf("Read error!\n");
                            result = 0xffffffff;
                            break;
                        }
                        else {
                            send_data(buf, len < sizeof(buf) ? len : sizeof(buf));
                            len -= len < sizeof(buf) ? len : sizeof(buf);
                        }
                    }
                }
            }
            else if (!result){
                result = 0xdeadbeef;
            }
            if (result) {
                send_dword(4);
                send_dword(result);
            }
            break;
        }
        case 0x3000: {
            printf("Reboot\n");
            volatile uint32_t *reg = (volatile uint32_t *)0x10007000;
            reg[8/4] = 0x1971;
            reg[0/4] = 0x22000014;
            reg[0x14/4] = 0x1209;

            while (1) {

            }
        }
        case 0x3001: {
            printf("Kick watchdog\n");
            volatile uint32_t *reg = (volatile uint32_t *)0x10007000;
            reg[8/4] = 0x1971;
            break;
        }
        default:
            printf("Invalid command\n");
            break;
        }
    }

    printf("Exiting the payload\n");

    while (1) {

    }
}
