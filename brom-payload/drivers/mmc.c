#include <inttypes.h>
#include "../libc.h"
#include "types.h"
#include "core.h"
#include "mmc.h"
#include "errno.h"
#include "mt_sd.h"
#include "../crypto/hmac-sha256.h"

#define be32_to_cpup(addr) __builtin_bswap32(*(uint32_t*)addr)
#define be16_to_cpup(addr) __builtin_bswap16(*(uint16_t*)addr)
#define cpu_to_be16p be16_to_cpup
#define cpu_to_be32p be32_to_cpup

unsigned int msdc_cmd(struct msdc_host *host, struct mmc_command *cmd);
void sleepy(void);
void hex_dump(const void* data, size_t size);

int mmc_go_idle(struct msdc_host *host)
{
    int err;
    struct mmc_command cmd = {0};

    cmd.opcode = MMC_GO_IDLE_STATE;
    cmd.arg = 0;
    cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_NONE | MMC_CMD_BC;

    err = msdc_cmd(host, &cmd);

    // host->use_spi_crc = 0;

    return err;
}

int mmc_all_send_cid(struct msdc_host *host, u32 *cid)
{
    int err;
    struct mmc_command cmd = {0};

    cmd.opcode = MMC_ALL_SEND_CID;
    cmd.arg = 0;
    cmd.flags = MMC_RSP_R2 | MMC_CMD_BCR;

    err = msdc_cmd(host, &cmd);
    if (err)
        return err;

    memcpy(cid, cmd.resp, sizeof(u32) * 4);

    return 0;
}

int mmc_send_op_cond(struct msdc_host *host, u32 ocr, u32 *rocr)
{
    struct mmc_command cmd = {0};
    int i, err = 0;

    cmd.opcode = MMC_SEND_OP_COND;
    // cmd.arg = mmc_host_is_spi(host) ? 0 : ocr;
    cmd.arg = ocr;
    cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R3 | MMC_CMD_BCR;

    for (i = 100; i; i--) {
        err = msdc_cmd(host, &cmd);
        if (err)
            break;

        /* if we're just probing, do a single pass */
        if (ocr == 0)
            break;

        /* otherwise wait until reset completes */
        if (cmd.resp[0] & MMC_CARD_BUSY)
            break;

        err = -ETIMEDOUT;

        // mmc_delay(10);
        sleepy(); // TODO
    }

    if (rocr)
        *rocr = cmd.resp[0];

    return err;
}

static u32 mmc_select_voltage(struct msdc_host *host, u32 ocr)
{
    int bit;

    ocr &= host->ocr_avail;

    bit = uffs(ocr);
    if (bit) {
        bit -= 1;
        ocr &= 3 << bit;
    } else {
        ocr = 0;
    }
    return ocr;
}

int mmc_set_relative_addr(struct msdc_host *host, uint32_t rca)
{
    int err;
    struct mmc_command cmd = {0};

    cmd.opcode = MMC_SET_RELATIVE_ADDR;
    cmd.arg = rca << 16;
    cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

    err = msdc_cmd(host, &cmd);
    if (err)
        return err;

    return 0;
}

static int mmc_select_card(struct mmc_host *host, uint32_t rca)
{
    int err;
    struct mmc_command cmd = {0};

    cmd.opcode = MMC_SELECT_CARD;

    cmd.arg = rca << 16;
    cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

    err = msdc_cmd(host, &cmd);
    if (err)
        return err;

    return 0;
}

int mmc_read(struct msdc_host *host, uint32_t blk, void *buf)
{
    int err;
    struct mmc_command cmd = { 0 };

    cmd.opcode = MMC_READ_SINGLE_BLOCK;

    cmd.arg = blk;
    cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

    msdc_set_blknum(host, 1);

    err = msdc_cmd(host, &cmd);
    if (err)
        return err;

    return msdc_pio_read(host, buf);
}

int mmc_write(struct msdc_host *host, uint32_t blk, void *buf)
{
    int err;
    struct mmc_command cmd = { 0 };

    cmd.opcode = MMC_WRITE_BLOCK;

    cmd.arg = blk;
    cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

    msdc_set_blknum(host, 1);

    err = msdc_cmd(host, &cmd);
    if (err)
        return err;

    return msdc_pio_write(host, buf);
}

int mmc_send_status(struct msdc_host *host, u32 *status)
{
    int err;
    struct mmc_command cmd = {0};

    cmd.opcode = MMC_SEND_STATUS;
    cmd.arg = 1 << 16;
    cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;

    err = msdc_cmd(host, &cmd);
    if (err)
        return err;

    /* NOTE: callers are required to understand the difference
     * between "native" and SPI format status words!
     */
    if (status)
        *status = cmd.resp[0];

    return 0;
}

/**
 *  __mmc_switch - modify EXT_CSD register
 *  @card: the MMC card associated with the data transfer
 *  @set: cmd set values
 *  @index: EXT_CSD register index
 *  @value: value to program into EXT_CSD register
 *  @timeout_ms: timeout (ms) for operation performed by register write,
 *                   timeout of zero implies maximum possible timeout
 *  @use_busy_signal: use the busy signal as response type
 *
 *  Modifies the EXT_CSD register for selected card.
 */
int __mmc_switch(struct msdc_host *host, u8 set, u8 index, u8 value,
           unsigned int timeout_ms, bool use_busy_signal)
{
    int err;
    struct mmc_command cmd = {0};
    // unsigned long timeout;
    u32 status;

    cmd.opcode = MMC_SWITCH;
    cmd.arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
          (index << 16) |
          (value << 8) |
          set;
    cmd.flags = MMC_CMD_AC;
    if (use_busy_signal)
        cmd.flags |= MMC_RSP_SPI_R1B | MMC_RSP_R1B;
    else
        cmd.flags |= MMC_RSP_SPI_R1 | MMC_RSP_R1;


    cmd.cmd_timeout_ms = timeout_ms;

    err = msdc_cmd(host, &cmd);
    if (err)
        return err;

    /* No need to check card status in case of unblocking command */
    if (!use_busy_signal)
        return 0;

    /* Must check status to be sure of no errors */
    // timeout = jiffies + msecs_to_jiffies(MMC_OPS_TIMEOUT_MS);
    do {
        err = mmc_send_status(host, &status);
        if (err)
            return err;
        //===ss6, bug, cmd6's status will be missed if set the WAIT_WHILE_BUSY flags 
        //if (card->host->caps & MMC_CAP_WAIT_WHILE_BUSY)
            //break;
        //===
        // if (mmc_host_is_spi(card->host))
        //     break;

        /* Timeout if the device never leaves the program state. */
        // if (time_after(jiffies, timeout)) {
        //     pr_err("%s: Card stuck in programming state! %s\n",
        //         mmc_hostname(card->host), __func__);
        //     return -ETIMEDOUT;
        // }
    } while (R1_CURRENT_STATE(status) == R1_STATE_PRG);

    // if (mmc_host_is_spi(card->host)) {
    //     if (status & R1_SPI_ILLEGAL_COMMAND)
    //         return -EBADMSG;
    // } else
    {
        if (status & 0xFDFFA000)
            printf("%s: unexpected status %#x after "
                   "switch", "MSDC0", status);
        if (status & R1_SWITCH_ERROR)
            return -EBADMSG;
    }

    return 0;
}

static int mmc_rpmb_send_command(struct msdc_host *host, u8 *buf, __u16 blks,
        __u16 type, u8 req_type)
{
    struct mmc_command cmd = {0};
    struct mmc_command sbc = {0};
    int ret = 0;

    /*
     * set CMD23
     */
    sbc.opcode = MMC_SET_BLOCK_COUNT;
    // printf("blks = %d\n", blks);
    sbc.arg = blks;
    if ((req_type == RPMB_REQ) && (type == RPMB_WRITE_DATA ||
                type == RPMB_PROGRAM_KEY))
        sbc.arg |= 1 << 31;
    sbc.flags = MMC_RSP_R1 | MMC_CMD_AC;

    ret = msdc_cmd(host, &sbc);
    if (ret) {
        printf("msdc_cmd SET_BLOCK_COUNT fail %d\n", ret);
        return ret;
    }

    msdc_set_blknum(host, blks);

    /*
     * set CMD25/18
     */
    if (req_type == RPMB_REQ) {
        cmd.opcode = MMC_WRITE_MULTIPLE_BLOCK;
    } else {
        cmd.opcode = MMC_READ_MULTIPLE_BLOCK;
    }
    cmd.arg = 0;
    cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

    ret = msdc_cmd(host, &cmd);
    if (ret) {
        printf("msdc_cmd READ/WRITE MULTIPLE_BLOCK fail %d\n", ret);
        return ret;
    }

    // printf("and the buf:\n");
    // hex_dump(buf, 0x200);

    // this only works for a single block
    if (req_type == RPMB_REQ) {
        msdc_pio_write(host, buf);
    } else {
        msdc_pio_read(host, buf);
    }

    return 0;
}

int mmc_switch(struct msdc_host *host, u8 set, u8 index, u8 value,
        unsigned int timeout_ms)
{
    return __mmc_switch(host, set, index, value, timeout_ms, 1);
}

int mmc_set_part(struct msdc_host *host, int part) {
    return mmc_switch(host, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_PART_CONFIG, 72 | part, 0);
}

int mmc_rpmb_partition_ops(struct mmc_core_rpmb_req *rpmb_req, struct msdc_host *host)
{
    int err = 0;
    struct mmc_ioc_rpmb_req *p_req;
    __u16 type, blks;
    __u8 *buf_frame;

    p_req = rpmb_req->req;
    buf_frame = rpmb_req->frame;

    if (!p_req || !rpmb_req->ready || !buf_frame) {
        printf("%s: mmc_ioc_rpmb_req is not prepared\n",
                "MSDC0");
        return -EINVAL;
    }

    type = p_req->type;
    blks = p_req->blk_cnt;

    /*
     * STEP 1: send request to RPMB partition
     */
    if (type == RPMB_WRITE_DATA)
        err = mmc_rpmb_send_command(host, buf_frame, blks,
                type, RPMB_REQ);
    else
        err = mmc_rpmb_send_command(host, buf_frame, 1, type, RPMB_REQ);

    if (err) {
        printf("%s: request write counter failed (%d)\n",
                "MSDC0", err);
        goto out;
    }

    memset(buf_frame, 0, 512 * blks);
    /*
     * STEP 2: check write result
     * Only for WRITE_DATA or Program key
     */
    if (type == RPMB_WRITE_DATA ||
            type == RPMB_PROGRAM_KEY) {
        buf_frame[RPMB_TYPE_BEG + 1] = RPMB_RESULT_READ;
        err = mmc_rpmb_send_command(host, buf_frame, 1,
                RPMB_RESULT_READ, RPMB_REQ);
        if (err) {
            printf("%s: request write counter failed (%d)\n",
                    "MSDC0", err);
            goto out;
        }
    }

    /*
     * STEP 3: get response from RPMB partition
     */

    if (type == RPMB_READ_DATA)
        err = mmc_rpmb_send_command(host, buf_frame,
                blks, type, RPMB_RESP);
    else
        err = mmc_rpmb_send_command(host, buf_frame,
                1, type, RPMB_RESP);
    if (err) {
        printf("%s: response write counter failed (%d)\n",
                "MSDC0", err);
    }
out:
    return err;
}

void mmc_rpmb_post_frame(struct mmc_core_rpmb_req *rpmb_req)
{
    int i;
    struct mmc_ioc_rpmb_req *p_req;
    __u8 *buf_frame;

    if (!rpmb_req || !rpmb_req->ready)
        return;

    p_req = rpmb_req->req;
    buf_frame = rpmb_req->frame;

    if (!p_req || !buf_frame)
        return;


    printf("post-frame:\n");
    hex_dump(buf_frame, 0x200);

    /*
     * Regarding to the check rules, here is the post
     * rules
     * All will return result.
     * GET_WRITE_COUNTER:
     *              must: write counter, nonce
     *              optional: MAC
     * WRITE_DATA:
     *              must: MAC, write counter
     * READ_DATA:
     *              must: nonce, data
     *              optional: MAC
     * PROGRAM_KEY:
     *              must: Nothing
     *
     * Except READ_DATA, all of these operations only need to parse
     * one frame. READ_DATA needs blks frames to get DATA
     */

    memcpy(p_req->result, buf_frame + RPMB_RES_BEG, 2);
    *p_req->result = be16_to_cpup(p_req->result);

    if (p_req->type == RPMB_PROGRAM_KEY)
        goto out;

    if (p_req->type == RPMB_GET_WRITE_COUNTER ||
            p_req->type == RPMB_WRITE_DATA) {
        memcpy(p_req->wc, buf_frame + RPMB_WCOUNTER_BEG, 4);
        *p_req->wc = be32_to_cpup(p_req->wc);
    }

    if (p_req->type == RPMB_GET_WRITE_COUNTER ||
            p_req->type == RPMB_READ_DATA) {
        /* nonce copy */
        memcpy(p_req->nonce, buf_frame + RPMB_NONCE_BEG, 16);
    }
    /*
     * Take MAC within the last package
     */
    if (p_req->type == RPMB_READ_DATA) {
        __u8 *data = p_req->data;
        for (i = 0; i < p_req->blk_cnt; i++) {
            memcpy(data, buf_frame + i * 512 + RPMB_DATA_BEG, 256);
            data += 256;
        }
        /*
         * MAC stored in the last package
         */
        if (p_req->mac)
            memcpy(p_req->mac, buf_frame + i * 512 + RPMB_MAC_BEG,
                    32);
    } else if (p_req->mac)
        memcpy(p_req->mac, buf_frame + RPMB_MAC_BEG, 32);
out:
    return;
}

static int mmc_rpmb_request_check(struct msdc_host *host,
        struct mmc_ioc_rpmb_req *p_req)
{
    (void)host;
    /*
     * Some parameters are a must for the operation. Different
     * operation expect different paramters. Below code is
     * used for checking this.
     *
     * All operations will need result.
     * GET_WRITE_COUNTER:
     *              must: write counter, nonce
     *              optional: MAC
     * WRITE_DATA:
     *              must: MAC, data, write counter
     * READ_DATA:
     *              must: nonce, data
     *              optional: MAC
     * PROGRAM_KEY:
     *              must: MAC
     *
     * So here, we only check the 'must' paramters
     */
    if (!p_req->result) {
        printf("%s: Type %d has NULL pointer for result\n",
                "MSDC0", p_req->type);
        return -EINVAL;
    }

    if (p_req->type == RPMB_GET_WRITE_COUNTER) {
        if (!p_req->nonce || !p_req->wc) {
            printf("%s: Type %d has NULL pointer for nonce/wc\n",
                    "MSDC0", p_req->type);
            return -EINVAL;
        }
        /*
         * used to allocate frame
         */
        p_req->blk_cnt = 1;
    } else if (p_req->type == RPMB_WRITE_DATA ||
            p_req->type == RPMB_READ_DATA) {
#if 0
        if ((__u32)(p_req->addr + p_req->blk_cnt) >
                card->ext_csd.rpmb_size) {
            printf("%s Type %d: beyond the RPMB partition rang addr %d, blk_cnt %d, rpmb_size %d\n",
                    "MSDC0",
                    p_req->type,
                    p_req->addr,
                    p_req->blk_cnt,
                    card->ext_csd.rpmb_size);
            return -EINVAL;
        }
#endif
        if (p_req->blk_cnt == 0) {
            printf("%s: Type %d has zero block count\n",
                    "MSDC0",
                    p_req->blk_cnt);
            return -EINVAL;
        }
#if 0
 else if (p_req->blk_cnt > card->rpmb_max_req) {
            printf("%s: Type %d has invalid block count, cannot large than %d\n",
                    "MSDC0",
                    p_req->blk_cnt,
                    card->rpmb_max_req);
            return -EINVAL;
        }
#endif
        if (!p_req->data) {
            printf("%s: Type %d has NULL pointer for data\n",
                    "MSDC0", p_req->type);
            return -EINVAL;
        }
        if (p_req->type == RPMB_WRITE_DATA) {
            if (!p_req->wc || !p_req->mac) {
                printf("%s: Type %d has NULL pointer for write counter/MAC\n",
                        "MSDC0",
                        p_req->type);
                return -EINVAL;
            }
        } else {
            if (!p_req->nonce) {
                printf("%s: Type %d has NULL pointer for nonce\n",
                        "MSDC0",
                        p_req->type);
                return -EINVAL;
            }
        }
    } else if (p_req->type == RPMB_PROGRAM_KEY) {
        if (!p_req->mac) {
            printf("%s: Type %d has NULL pointer for MAC\n",
                    "MSDC0", p_req->type);
            return -EINVAL;
        }
        /*
         * used to allocate frame
         */
        p_req->blk_cnt = 1;
    } else
        return -EOPNOTSUPP;

    return 0;
}

/*
 * prepare the request of RPMB frame
 * RPMB frame is MSB first
 * convert needed bytes
 * return how many frames will be prepared
 */
int mmc_rpmb_pre_frame(struct mmc_core_rpmb_req *rpmb_req,
        struct msdc_host *host)
{
    int i, ret;
    struct mmc_ioc_rpmb_req *p_req;
    __u8 *buf_frame;
    __u16 blk_cnt, addr, type;
    __u32 w_counter;

    if (!rpmb_req || !host)
        return -EINVAL;

    p_req = rpmb_req->req;
    if (!p_req) {
        printf("%s: mmc_ioc_rpmb_req is NULL. Wrong parameter\n",
                "MSDC0");
        return -EINVAL;
    }

    /*
     * make sure these two items are clear
     */
    rpmb_req->ready = 0;

    ret = mmc_rpmb_request_check(host, p_req);
    if (ret)
        return ret;

    if (p_req->blk_cnt != 1) {
        printf("rpmb only 1 block allowed, got %d\n", p_req->blk_cnt);
        return -ENOMEM;
    }

    buf_frame = rpmb_req->frame;
    if (!buf_frame) {
        printf("%s: cannot allocate frame for type %d\n",
                "MSDC0", p_req->type);
        return -ENOMEM;
    }

    type = cpu_to_be16p(&p_req->type);
    if (p_req->type == RPMB_GET_WRITE_COUNTER ||
            p_req->type == RPMB_READ_DATA) {
        /*
         * One package prepared
         * This request needs Nonce and type
         * If is data read, then also need addr
         */
        memcpy(buf_frame + RPMB_TYPE_BEG, &type, 2);
        if (p_req->type == RPMB_READ_DATA) {
            addr = cpu_to_be16p(&p_req->addr);
            memcpy(buf_frame + RPMB_ADDR_BEG, &addr, 2);
        }
        /* convert Nonce code */
        memcpy(buf_frame + RPMB_NONCE_BEG, p_req->nonce, 16);
    } else if (p_req->type == RPMB_WRITE_DATA) {
        __u8 *data = p_req->data;
        /*
         * multiple package prepared
         * This request nees blk_cnt, addr, write_counter,
         * data and mac
         */
        blk_cnt = cpu_to_be16p(&p_req->blk_cnt);
        addr = cpu_to_be16p(&p_req->addr);
        w_counter = cpu_to_be32p(p_req->wc);
        for (i = 0; i < p_req->blk_cnt; i++) {
            memcpy(buf_frame + i * 512 + RPMB_TYPE_BEG,
                    &type, 2);
            memcpy(buf_frame + i * 512 + RPMB_BLKS_BEG,
                    &blk_cnt, 2);
            memcpy(buf_frame + i * 512 + RPMB_ADDR_BEG,
                    &addr, 2);
            memcpy(buf_frame + i * 512 + RPMB_WCOUNTER_BEG,
                    &w_counter, 4);
            memcpy(buf_frame + i * 512 + RPMB_DATA_BEG,
                    data, 256);
            data += 256;
        }
        /* convert MAC code */
        memcpy(buf_frame + 512 * (i - 1) + RPMB_MAC_BEG,
                p_req->mac, 32);
    } else if (p_req->type == RPMB_PROGRAM_KEY) {
        /*
         * One package prepared
         * This request only need mac
         */
        memcpy(buf_frame + RPMB_TYPE_BEG, &type, 2);
        /* convert MAC code */
        memcpy(buf_frame + RPMB_MAC_BEG,
                p_req->mac, 32);
    } else {
        printf("%s: We shouldn't be here\n", "MSDC0");
        return -EINVAL;
    }
    rpmb_req->ready = 1;
    return 0;
}

int mmc_rpmb_get_write_count(struct msdc_host *host, uint32_t *wc) {
    struct mmc_core_rpmb_req rpmb_req = { 0 };
    struct mmc_ioc_rpmb_req req = { 0 };
    int ret = 0;
    uint16_t result = 0;
    uint8_t nonce[32] = { 0 };
    req.type = RPMB_GET_WRITE_COUNTER;
    req.wc = wc;
    req.result = &result;
    req.nonce = nonce;
    rpmb_req.req = &req;
    /* check request */
    ret = mmc_rpmb_pre_frame(&rpmb_req, host);
    if (ret) {
        printf("%s: prepare frame failed\n", "MSDC0");
        return ret;
    }

    /*
     * before start, let's change to RPMB partition first
     */
    ret = mmc_set_part(host, 3);
    if (ret) {
        printf("mmc_set_part fail %d\n", ret);
        return ret;
    }

    ret = mmc_rpmb_partition_ops(&rpmb_req, host);
    if (ret)
        printf("%s: failed (%d) to handle RPMB request type (%d)!\n",
                "MSDC0", ret, req.type);

    mmc_rpmb_post_frame(&rpmb_req);

    printf("result = %d\n", result);

    return ret;
}

static void byteswap(uint8_t *buf, size_t sz) {
    for (size_t i = 0; i < sz / 2; ++i) {
        size_t j = sz - i - 1;
        uint8_t o = buf[j];
        buf[j] = buf[i];
        buf[i] = o;
    }
}

int mmc_rpmb_read(struct msdc_host *host, void *buf) {
    struct mmc_core_rpmb_req rpmb_req = { 0 };
    struct mmc_ioc_rpmb_req req = { 0 };
    int ret = 0;
    uint16_t result = 0;
    uint8_t nonce[32] = { 0 };
    req.type = RPMB_READ_DATA;
    req.blk_cnt = 1;
    req.result = &result;
    req.nonce = nonce;
    req.addr = 0;
    req.data = buf;
    rpmb_req.req = &req;
    /* check request */
    ret = mmc_rpmb_pre_frame(&rpmb_req, host);
    if (ret) {
        printf("%s: prepare frame failed\n", "MSDC0");
        return ret;
    }

    /*
     * before start, let's change to RPMB partition first
     */
    ret = mmc_set_part(host, 3);
    if (ret) {
        printf("mmc_set_part fail %d\n", ret);
        return ret;
    }

    ret = mmc_rpmb_partition_ops(&rpmb_req, host);
    if (ret)
        printf("%s: failed (%d) to handle RPMB request type (%d)!\n",
                "MSDC0", ret, req.type);

    mmc_rpmb_post_frame(&rpmb_req);

    printf("result = %d\n", result);

    byteswap(buf, 0x100);

    return ret;
}

static void sej_init(int arg) {
    int param = 0;
    if ( arg )
        param = 3;
    else
        param = 2;

    sdr_write32(0x1000A020, 0);
    sdr_write32(0x1000A024, 0);
    sdr_write32(0x1000A028, 0);
    sdr_write32(0x1000A02C, 0);
    sdr_write32(0x1000A030, 0);
    sdr_write32(0x1000A034, 0);
    sdr_write32(0x1000A038, 0);
    sdr_write32(0x1000A03C, 0);
    sdr_write32(0x1000A004, 2);
    sdr_write32(0x1000A00C, 272);
    sdr_write32(0x1000A008, 2);
    sdr_write32(0x1000A040, 0x9ED40400);
    sdr_write32(0x1000A044, 0xE884A1);
    sdr_write32(0x1000A048, 0xE3F083BD);
    sdr_write32(0x1000A04C, 0x2F4E6D8A);

    uint32_t magic[12] = { 
        0x2D44BB70,
        0xA744D227,
        0xD0A9864B,
        0x83FFC244,
        0x7EC8266B,
        0x43E80FB2,
        0x1A6348A,
        0x2067F9A0,
        0x54536405,
        0xD546A6B1,
        0x1CC3EC3A,
        0xDE377A83
    };

    for (int i = 0; i < 3; ++i) {
        int pos = i * 4;
        sdr_write32(0x1000A010, magic[pos]);
        sdr_write32(0x1000A014, magic[pos + 1]);
        sdr_write32(0x1000A018, magic[pos + 2]);
        sdr_write32(0x1000A01C, magic[pos + 3]);
        sdr_write32(0x1000A008, 1);
        while ( !(sdr_read32(0x1000A008) & 0x8000) )
          ;
    }

    sdr_write32(0x1000A008, 2);
    sdr_write32(0x1000A040, 0x9ED40400);
    sdr_write32(0x1000A044, 0xE884A1);
    sdr_write32(0x1000A048, 0xE3F083BD);
    sdr_write32(0x1000A04C, 0x2F4E6D8A);
    sdr_write32(0x1000A004, param);
    sdr_write32(0x1000A00C, 0);
}

static void sej_run(uint32_t *buf1, size_t len, char *buf2) {
    char *i;
    for ( i = buf2; (size_t)(i - buf2) < len; *(uint32_t *)(i - 4) = sdr_read32(0x1000A05C) )
    {
        sdr_write32(0x1000A010, buf1[0]);
        sdr_write32(0x1000A014, buf1[1]);
        sdr_write32(0x1000A018, buf1[2]);
        sdr_write32(0x1000A01C, buf1[3]);
        sdr_write32(0x1000A008, 1);
        while ( !(sdr_read32(0x1000A008) & 0x8000) )
          ;
        buf1 += 4;
        i += 16;
        *(uint32_t *)(i - 16) = sdr_read32(0x1000A050);
        *(uint32_t *)(i - 12) = sdr_read32(0x1000A054);
        *(uint32_t *)(i - 8) = sdr_read32(0x1000A058);
    }
}

static void sej_fini() {
    sdr_write32(0x1000A008, 2);
    sdr_write32(0x1000A020, 0);
    sdr_write32(0x1000A024, 0);
    sdr_write32(0x1000A028, 0);
    sdr_write32(0x1000A02C, 0);
    sdr_write32(0x1000A030, 0);
    sdr_write32(0x1000A034, 0);
    sdr_write32(0x1000A038, 0);
    sdr_write32(0x1000A03C, 0);
}

static void sej_encrypt(void *buf, size_t len, void *buf2) {
    // printf("orig:\n");
    // hex_dump(buf, len);

    // printf("sej init\n");
    sej_init(1);
    // printf("sej run\n");
    sej_run(buf, len, buf2);
    // printf("sej fini\n");
    sej_fini();

    // printf("result:\n");
    // hex_dump(buf, len);
}

uint8_t rpmb_key[32];

void rpmb_calc_mac(struct mmc_core_rpmb_req *rpmb_req) {
    struct mmc_ioc_rpmb_req *req = rpmb_req->req;

    printf("hmac over \n");
    hex_dump(rpmb_req->frame + RPMB_DATA_BEG, 512 - RPMB_DATA_BEG);
    hmac_sha256(req->mac, rpmb_req->frame + RPMB_DATA_BEG, 512 - RPMB_DATA_BEG, rpmb_key, sizeof(rpmb_key));

    printf("using key \n");
    hex_dump(rpmb_key, sizeof(rpmb_key));

    printf("results in \n");
    hex_dump(req->mac, 32);

    memcpy(rpmb_req->frame + RPMB_MAC_BEG, req->mac, 32);

    printf("frame:\n");
    hex_dump(rpmb_req->frame, 0x200);
    // sej_encrypt(req->mac, 32, req->mac);
}

int mmc_rpmb_write(struct msdc_host *host, void *buf) {
    struct mmc_core_rpmb_req rpmb_req = { 0 };
    struct mmc_ioc_rpmb_req req = { 0 };
    int ret = 0;
    uint16_t result = 0;
    uint8_t nonce[32] = { 0 };
    uint8_t mac[32] = { 0 };
    uint32_t wc;

    uint8_t tmp[0x100];
    memcpy(tmp, buf, sizeof(tmp));
    byteswap(tmp, sizeof(tmp));

    ret = mmc_rpmb_get_write_count(host, &wc);
    if (ret) {
        printf("mmc_rpmb_get_write_count %d\n", ret);
        return ret;
    }
    printf("wc = %d\n", wc);

    req.type = RPMB_WRITE_DATA;
    req.blk_cnt = 1;
    req.result = &result;
    req.nonce = nonce;
    req.addr = 0;
    req.data = tmp;
    req.wc = &wc;
    req.mac = mac;

    rpmb_req.req = &req;
    /* check request */
    ret = mmc_rpmb_pre_frame(&rpmb_req, host);
    if (ret) {
        printf("%s: prepare frame failed\n", "MSDC0");
        return ret;
    }

    rpmb_calc_mac(&rpmb_req);

    /*
     * before start, let's change to RPMB partition first
     */
    ret = mmc_set_part(host, 3);
    if (ret) {
        printf("mmc_set_part fail %d\n", ret);
        return ret;
    }

    ret = mmc_rpmb_partition_ops(&rpmb_req, host);
    if (ret)
        printf("%s: failed (%d) to handle RPMB request type (%d)!\n",
                "MSDC0", ret, req.type);

    mmc_rpmb_post_frame(&rpmb_req);

    printf("result = %d\n", result);

    return ret;
}

static void derive_rpmb_key(uint8_t *in) {
    printf("in:\n");
    hex_dump(in, 16);
    printf("\n");

    uint8_t expand[32] = { 0 };
    for (int i = 0; i < 32; ++i) {
        expand[i] = in[i % 16];
    }

    printf("expand:\n");
    hex_dump(expand, 32);
    printf("\n");

    sej_encrypt(expand, 32, expand);
    printf("encrypted:\n");
    hex_dump(expand, 32);
    printf("\n");

    byteswap(expand, 32);
    printf("final:\n");
    hex_dump(expand, 32);
    printf("\n");

    memcpy(rpmb_key, expand, 32);
}

int mmc_init(struct msdc_host *host) {
    int ret = 0;

    host->blksz = 0x200;

    sdr_set_bits(MSDC_CFG, MSDC_CFG_PIO);
    sleepy();
    sdr_write32(MSDC_CFG, sdr_read32(MSDC_CFG) | 0x1000);
    sleepy();
    printf("MSDC_CFG: 0x%08X\n", sdr_read32(MSDC_CFG));

    ret = mmc_go_idle(host);
    printf("GO_IDLE = 0x%08X\n", ret);

    uint32_t ocr = 0;
    ret = mmc_send_op_cond(host, 0, &ocr);
    printf("SEND_OP_COND = 0x%08X ocr = 0x%08X\n", ret, ocr);

    ocr = mmc_select_voltage(host, ocr);
    ocr |= 1 << 30;
    printf("new ocr = 0x%08X\n", ocr);
    uint32_t rocr = 0;
    ret = mmc_send_op_cond(host, ocr, &rocr);
    printf("SEND_OP_COND = 0x%08X ocr = 0x%08X\n", ret, rocr);

    uint32_t cid[4] = { 0 };
    ret = mmc_all_send_cid(host, cid);
    printf("ALL_SEND_CID = 0x%08X cid = 0x%08X 0x%08X 0x%08X 0x%08X\n", ret, cid[0], cid[1], cid[2], cid[3]);

    uint32_t cid_be[4] = { 0 };
    for (int i = 0; i < 4; ++i)
        cid_be[i] = __builtin_bswap32(cid[i]);
    derive_rpmb_key((void*)cid_be);

    ret = mmc_set_relative_addr(host, 1);
    printf("SET_RELATIVE_ADDR = 0x%08X\n", ret);

    ret = mmc_select_card(host, 1);
    printf("SELECT_CARD = 0x%08X\n", ret);

    return 0;
}
