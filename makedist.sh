#!/bin/bash

rm -rf dist

mkdir -p dist/unlock/kamakiri/bin
cp bin/{preloader.img,lk.bin,tz.img,twrp.img,microloader.bin} dist/unlock/kamakiri/bin/

mkdir -p dist/unlock/kamakiri/lk-payload/build
cp lk-payload/build/payload.bin dist/unlock/kamakiri/lk-payload/build/

mkdir -p dist/unlock/kamakiri/modules
cp modules/{common.py,load_payload.py,logger.py,main.py,functions.py,handshake2.py} dist/unlock/kamakiri/modules/

mkdir -p dist/unlock/kamakiri/brom-payload/stage1
cp brom-payload/stage1/stage1.bin dist/unlock/kamakiri/brom-payload/stage1/

mkdir -p dist/unlock/kamakiri/brom-payload/stage2
cp brom-payload/stage2/stage2.bin dist/unlock/kamakiri/brom-payload/stage2/

cp {bootrom-step.sh,fastboot-step.sh,boot-recovery.sh,boot-fastboot.sh} dist/unlock/kamakiri/

mkdir -p dist/unlock/META-INF/com/google/android
cp META-INF/com/google/android/{update-binary,updater-script} dist/unlock/META-INF/com/google/android/

#mkdir -p dist/stock/kamakiri/bin
#cp bin/{boot.img,recovery.img} dist/stock/kamakiri/bin/
#cp return-to-stock.sh dist/stock/kamakiri/

mkdir -p dist/gptfix/kamakiri/bin
cp gpt-fix.sh dist/gptfix/kamakiri/
cp bin/gpt-mantis.bin dist/gptfix/kamakiri/bin/

mkdir -p dist/full
cp -r dist/unlock/* dist/full/
#cp -r dist/stock/* dist/full/
cp -r dist/gptfix/* dist/full/
