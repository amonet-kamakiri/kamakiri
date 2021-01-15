#!/usr/bin/env python3

import sys
import time

from common import Device
from logger import log
from load_payload import load_payload
from functions import *

import usb.core
import usb.util

import ctypes

import traceback


import struct
import os

def main(dev):

    load_payload(dev)

    # 1) Sanity check GPT
    log("Check GPT")
    switch_user(dev)

    # 1.1) Parse gpt
    gpt = parse_gpt(dev)
    log("gpt_parsed = {}".format(gpt))
    if "lk" not in gpt or "tee1" not in gpt or "boot" not in gpt or "recovery" not in gpt:
        raise RuntimeError("bad gpt")

    # 9.1) Wait some time so data is flushed to EMMC
    time.sleep(5)

    # Reboot (to fastboot or recovery)
    log("Reboot")
    dev.reboot()


if __name__ == "__main__":

    check_modemmanager()

    dev = Device()
    dev.find_device()

    main(dev)
