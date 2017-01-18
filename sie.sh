#!/bin/bash
killall -9 qemu-system-arm
./arm-softmmu/qemu-system-arm -M versatilepb -d guest_errors