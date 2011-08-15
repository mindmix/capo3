#!/bin/bash
case $1 in
    busybox )
        qemu-system-x86_64 -m 512 -nographic -smp 8 -kernel ../linux-3.0/arch/x86/boot/bzImage -append "root=/dev/sda rw init=/sbin/init norandmaps console=ttyS0,115200 debug" -hda rootfs.ext3 -net nic,model=e1000 -net user
        ;;
    ubuntu )
        qemu-system-x86_64 -m 512 -smp 8 -hda ../disk_images/ubuntu-10.04-x86_64.qcow2 -nographic -kernel ../linux-3.0/arch/x86/boot/bzImage -append "root=/dev/sda1 rw init=/sbin/init norandmaps console=ttyS0,115200 debug" -net nic,model=e1000 -net user
        ;;
    noreplay )
        qemu-system-x86_64 -m 512 -hda ../disk_images/ubuntu-10.04-x86_64.qcow2 -nographic -smp 2 -net nic,model=e1000 -net user
        ;;
    * )
        echo "Usage: $0 busybox|ubuntu|noreplay"
        ;;
esac