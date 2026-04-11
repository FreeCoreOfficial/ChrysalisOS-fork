BITS 16
ORG 0x7C00

jmp short start
nop

; FAT12 BPB (patched/consumed by boot image builder)
bpb_oem:               db 'CHRYSV1 '
bpb_bytes_per_sector:  dw 512
bpb_sectors_per_cluster: db 1
bpb_reserved_sectors:  dw 2
bpb_fat_count:         db 2
bpb_root_entries:      dw 224
bpb_total_sectors_16:  dw 2880
bpb_media:             db 0xF0
bpb_sectors_per_fat:   dw 9
bpb_sectors_per_track: dw 18
bpb_heads:             dw 2
bpb_hidden_sectors:    dd 0
bpb_total_sectors_32:  dd 0
bios_drive:            db 0
bpb_reserved1:         db 0
bpb_ext_boot_sig:      db 0x29
bpb_volume_id:         dd 0x43525956
bpb_volume_label:      db 'CHRYSALIS '
bpb_fs_type:           db 'FAT12   '

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, boot_msg
    call puts

    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov si, 1
    mov cx, [bpb_reserved_sectors]
    dec cx
    jz .jump_stage2

.load_stage2:
    push cx
    push si
    push bx
    mov ax, si
    call read_sector_lba
    pop bx
    add bx, 512
    pop si
    inc si
    pop cx
    loop .load_stage2

.jump_stage2:
    mov dl, [boot_drive]
    jmp 0x1000:0x0000

; AX = LBA, ES:BX = destination
read_sector_lba:
    pusha
    mov di, 3
.retry:
    push ax
    xor dx, dx
    div word [bpb_sectors_per_track]   ; AX = temp, DX = sector idx
    mov cx, dx
    inc cl                              ; sector number is 1-based

    xor dx, dx
    div word [bpb_heads]                ; AX = cylinder, DX = head
    mov dh, dl                          ; head
    mov ch, al                          ; cylinder low 8 bits
    shl ah, 6
    or cl, ah                           ; cylinder high 2 bits in CL[7:6]

    mov dl, [boot_drive]
    mov ah, 0x02
    mov al, 0x01
    int 0x13
    jnc .ok

    xor ax, ax
    int 0x13
    dec di
    jnz .retry

    mov si, disk_err
    call puts
    jmp halt

.ok:
    add sp, 2
    popa
    ret

puts:
    pusha
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x0F
    int 0x10
    jmp .next
.done:
    popa
    ret

halt:
    cli
.hang:
    hlt
    jmp .hang

boot_drive: db 0
boot_msg:   db 'Chrysalis bootloader v1', 13, 10, 0
disk_err:   db 'Disk read error', 13, 10, 0

times 510-($-$$) db 0
    dw 0xAA55
