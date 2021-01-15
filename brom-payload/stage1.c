#include "common.h"

int print(char* s){
    char c = s[0];
    int i = 0;
    while(c){
        _putchar(c);
        c = s[++i];
    }
    return i;
}

int main() {

    print("Entered 1ST stage payload\n");
    print("Copyright xyz, k4y0z 2019\n");

    //This is so we don't get a USB-Timeout
    print("Send USB response\n");
    send_usb_response(1,0,1);

    print("Entering command loop\n");
    send_dword(0xA1A2A3A4);

    while (1) {
        uint32_t magic = recv_dword();
        if (magic != 0xf00dd00d) {
            print("Protocol error\n");
            //printf("Magic received = 0x%08X\n", magic);
            break;
        }
        uint32_t cmd = recv_dword();
        switch (cmd) {
        case 0x4000: {
            uint32_t address = recv_dword();
            uint32_t size = recv_dword();
            //printf("Write %d Bytes to address 0x%08X\n", size, address);
            print("Write\n");
            if(recv_data(address, size, 0) == 0) {
                print("OK\n");
                send_dword(0xD0D0D0D0);
                //hex_dump((void *)address, size);
            } else {
                send_dword(0xF0F0F0F0);
                 print("Read fail\n");
            }
            break;
        }
        case 0x4001: {
            void (*jump_address)(void) = (void*) recv_dword();
            //printf("Jump to address 0x%08X\n", *jump_address);
	    print("Jump\n");
            jump_address();
            break;
        }
        case 0x3000: {
            print("Reboot\n");
            volatile uint32_t *reg = (volatile uint32_t *)0x10007000;
            reg[8/4] = 0x1971;
            reg[0/4] = 0x22000014;
            reg[0x14/4] = 0x1209;

            while (1) {

            }
        }
        case 0x3001: {
            print("Kick watchdog\n");
            volatile uint32_t *reg = (volatile uint32_t *)0x10007000;
            reg[8/4] = 0x1971;
            break;
        }
        default:
            print("Invalid command\n");
            break; 
        }
    }

    print("Exiting the payload\n");

    while (1) {

    }
}
