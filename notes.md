each command has :

const char* name;
void (*handler)(const char* args);


disk --usage — ajutor

disk --list — afișează model și număr de sectoare (folosește ata_identify + ata_decode_identify)

disk --init — scrie un MBR minimal (partition start = 2048). Folosește ata_set_allow_mbr_write(1) temporar.

disk --assign <letter> <lba> <count> — asignează litere (în memorie)

disk --format <letter> — quick-format (zeroize primele ~128 sectoare din partiție)

disk --test --write <lba> — scrie pattern 0..255 repeated în sector

disk --test --output <lba> — citește + hexdump un sector

disk --test --see-raw <lba> <count> — hexdump pentru mai multe sectoare



make build/user_prog.bin
xxd -i build/user_prog.bin > kernel/user/user_prog.bin.inc


nm build/mem/kmalloc_init.o | grep kmalloc_init || true
# sau, dacă ai pus în kmalloc.o:
nm build/mem/kmalloc.o | grep kmalloc_init || true


> mem help
> mem status
> mem slab
> mem buddy
> mem kmalloc 128


[EDI]
[ESI]
[EBP]
[ESP_saved]
[EBX]
[EDX]
[ECX]
[EAX]
[EFLAGS]
[RETURN_EIP]


swtpm socket --tpmstate dir=tpm \
             --ctrl type=unixio,path=swtpm.sock \
             --tpm2=false &

qemu-system-i386 \
  -chardev socket,id=chrtpm,path=swtpm.sock \
  -tpmdev emulator,id=tpm0,chardev=chrtpm \
  -device tpm-tis,tpmdev=tpm0 \
  -m 64

https://opensource.org/license/mit

| Segment       | Access |
| ------------- | ------ |
| Kernel code   | `0x9A` |
| Kernel data   | `0x92` |
| **User code** | `0xFA` |
| **User data** | `0xF2` |


0xEE = 1110 1110b
        ││││ └─ type = 32-bit interrupt gate
        │││└── DPL = 3 (user mode allowed)
        ││└── present


zero IDT
fallback pe toate intrările
IRQ 0 / 1
syscall 0x80 (DPL=3)
lidt

31        24 23    16 15   11 10    8 7      2 1 0
+------------+--------+--------+--------+--------+
| enable=1   | bus    | device | func   | offset |
+------------+--------+--------+--------+--------+

[pci] scanning PCI bus...
[pci] device found: 8086:1237
[pci] device found: 8086:7000
[pci] device found: 1234:1111
[pci] scan complete


grep -R "cmd_[a-zA-Z_]*(" kernel/cmds/*.h

grub-file --is-x86-multiboot2 iso/boot/kernel.bin

objdump -h iso/boot/kernel.bin | grep multiboot

convert wallpaper.jpg -resize 1024x768^ -gravity center -extent 1024x768 wallpaper.bmp

convert wallpaper.bmp \
  -resize 20x20! \
  -type Grayscale \
  -depth 8 \
  BMP3:wallpaper.bmp

convert wallpaper.bmp \
  -resize 160x100! \
  -depth 8 \
  BMP3:wallpaper.bmp


cd iso/boot/grub

for f in *.png; do
  echo "Fixing $f"
  convert "$f" \
    -alpha off \
    -strip \
    -colorspace RGB \
    -depth 8 \
    -interlace none \
    "$f"
done


isoinfo -l -i chrysalis-usb.iso

grub-file --is-x86-multiboot kernel.bin && echo MULTIBOOT_OK
grub-file --is-x86-multiboot2 kernel.bin && echo MULTIBOOT2_OK

stat -c %s chrysalis.img
truncate -s %512 chrysalis.img

VBoxManage convertfromraw chrysalis.img chrysalis.vdi --format VDI
