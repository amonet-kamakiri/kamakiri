#!/usr/bin/env python3

import hashlib

key = b"\xC0\xFA\xC1\x1D\x9D\x91\x8D\x73\xC7\xF3\x88\xDB\x55\xC2\xD7\xD1\x51\x6A\xAC\x0F\xDD\x6F\xB5\x26\x80\x03\x2E\x44\xEA\xA0\x5E\x8E"

with open("../bin/preloader.img", "rb") as fin:
    preloader = fin.read()

for offset in range(len(preloader)):
    for size in range(0x200, 0x400):
        hash = hashlib.sha256(preloader[offset:offset + size]).digest()
        if hash == key:
            print("Match found, offset: " + hex(offset) + " size: " + hex(size))
            for i in range(offset, offset + size, 16):
                print(''.join( [ "%02X " %  x  for x in preloader[i:(i+16) if (i+16) < (offset+size) else (offset+size)] ] ).strip() )
            print("Writing key to key.bin")
            with open("key.bin", "wb") as keyfile:
                keyfile.write(preloader[offset:offset + size])
            break
    else:
        continue
    break

