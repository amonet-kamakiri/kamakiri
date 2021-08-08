#include "common.h"
#include "payload_common.h"

#include "printf.h"

#include "libc.h"

#include "drivers/types.h"
#include "drivers/core.h"
#include "drivers/mt_sd.h"
#include "drivers/errno.h"
#include "drivers/mmc.h"

#define PRELOADER_BASE 0x201000

uint16_t send_dword_pattern[]  = {0xB507, 0x0E03};
uint16_t recv_dword_pattern[]  = {0x4B0E, 0x2200};
uint16_t msdc_init_pattern[]  = {0xB538, 0x4604, 0x4D0F};

int (*send_dword)();
void (*__recv_dword)();

int _recv_dword() {
    int dword = 0;
    __recv_dword(&dword);
    return dword;
}

int (*recv_dword)() = _recv_dword;

void _send_data(char *addr, uint32_t sz) {
    for (uint32_t i = 0; i < (((sz + 3) & ~3) / 4); i++) {
        send_dword(__builtin_bswap32(((uint32_t *)addr)[i]));
    }
}

void _recv_data(char *addr, uint32_t sz, uint32_t flags __attribute__((unused))) {
    for (uint32_t i = 0; i < (((sz + 3) & ~3) / 4); i++) {
        ((uint32_t *)addr)[i] = __builtin_bswap32(recv_dword());
    }
}

// addr, sz
int (*send_data)() = (void *)_send_data;
// addr, sz, flags (=0)
int (*recv_data)() = (void *)_recv_data;


uint32_t searchfunc(uint32_t startoffset, uint32_t endoffset, uint16_t *pattern, uint32_t patternsize) {
    uint32_t matched = 0;
    for (uint32_t offset = startoffset; offset < endoffset; offset += 2) {
        for (uint32_t i = 0; i < patternsize; i++) {
            if (((uint16_t *)offset)[i] != pattern[i]) {
                matched = 0;
                break;
            }
            if (++matched == patternsize) return offset;
        }
    }
    return 0;
}


int main() {
    printf("Entered preloader payload\n");
    printf("Copyright xyz, k4y0z 2021\n");

    struct msdc_host host = { 0 };
    host.ocr_avail = MSDC_OCR_AVAIL;

    mmc_init(&host);

    send_dword = (void *)(searchfunc(PRELOADER_BASE + 0x100, PRELOADER_BASE + 0x20000, send_dword_pattern, 2) | 1);
    printf("send_dword = %p\n", send_dword);
    __recv_dword = (void *)(searchfunc(PRELOADER_BASE + 0x100, PRELOADER_BASE + 0x20000, recv_dword_pattern, 2) | 1);
    printf("__recv_dword = %p\n", __recv_dword);

    void (*msdc_init)(uint32_t card, uint32_t unk) = (void *)(searchfunc(PRELOADER_BASE + 0x100, PRELOADER_BASE + 0x20000, msdc_init_pattern, 3) | 1);
    printf("msdc_init = %p\n", msdc_init);
    msdc_init(0, 2);

    command_loop(&host);
}
