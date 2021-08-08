#include "brom_common.h"
#include "payload_common.h"

int main() {
    printf("Entered 2ND stage payload\n");
    printf("Copyright xyz, k4y0z 2019\n");

    struct msdc_host host = { 0 };
    host.ocr_avail = MSDC_OCR_AVAIL;

    mmc_init(&host);

    command_loop(&host);
}
