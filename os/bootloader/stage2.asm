%define STAGE2_SEG          0x1000
%define LOADER_LOAD_BASE    0x10000
%define TEMP_KERNEL_PHYS    0x00020000
%define TEMP_MODULE_PHYS    0x00078000
%define MODULE_DEST_PHYS    0x04000000
%define MB2_INFO_PHYS       0x00009000
%define VBE_INFO_PHYS       0x00008000
%define E820_MAX_ENTRIES    32
%define SEL_LOADER_CODE     0x08
%define SEL_LOADER_DATA     0x10
%define SEL_FLAT_CODE       0x18
%define SEL_FLAT_DATA       0x20

BITS 16
ORG 0

start:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFE
    sti

    mov [boot_drive], dl

    call setup_text_mode
    call splash_screen
    call menu_loop
    call detect_memory_map
    call try_set_vbe_mode

    cmp byte [selected_profile], 1
    jne .not64
    mov si, kernel64_name
    mov dword [current_file_dest], TEMP_KERNEL_PHYS
    call load_root_file_to_lowmem
    jc boot_error
    mov [kernel_file_size], eax

    mov si, hello64_name
    mov dword [current_file_dest], TEMP_MODULE_PHYS
    call load_root_file_to_lowmem
    jc boot_error
    mov [module_file_size], eax
    jmp .ready

.not64:
    mov si, kernel32_name
    mov dword [current_file_dest], TEMP_KERNEL_PHYS
    call load_root_file_to_lowmem
    jc boot_error
    mov [kernel_file_size], eax
    mov dword [module_file_size], 0

.ready:
    call enable_a20
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp SEL_LOADER_CODE:(protected_mode_entry + LOADER_LOAD_BASE)

boot_error:
    mov si, error_missing_file
    call print_line
    jmp halt

setup_text_mode:
    mov ax, 0x0003
    int 0x10
    ret

clear_text:
    pusha
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov ax, 0x1F20
    mov cx, 80 * 25
    rep stosw
    popa
    ret

splash_screen:
    call clear_text
    mov si, splash_1
    mov dh, 4
    call draw_centered_line
    call wait_ticks_2
    mov si, splash_2
    mov dh, 6
    call draw_centered_line
    call wait_ticks_2
    mov si, splash_3
    mov dh, 8
    call draw_centered_line
    call wait_ticks_2
    ret

menu_loop:
    mov byte [selected_profile], 0
    mov byte [details_enabled], 0
.redraw:
    call draw_menu
    mov ah, 0x00
    int 0x1A
    mov [menu_deadline], dx
.wait:
    mov ah, 0x01
    int 0x16
    jnz .have_key
    mov ah, 0x00
    int 0x1A
    mov ax, dx
    sub ax, [menu_deadline]
    cmp ax, 91
    jb .wait
    ret
.have_key:
    mov ah, 0x00
    int 0x16
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp ah, 0x1C
    je .done
    cmp al, 'd'
    je .toggle_details
    cmp al, 'D'
    je .toggle_details
    cmp al, 9
    je .toggle_details
    jmp .redraw
.up:
    cmp byte [selected_profile], 0
    jne .dec
    mov byte [selected_profile], 3
    jmp .redraw
.dec:
    dec byte [selected_profile]
    jmp .redraw
.down:
    cmp byte [selected_profile], 3
    jne .inc
    mov byte [selected_profile], 0
    jmp .redraw
.inc:
    inc byte [selected_profile]
    jmp .redraw
.toggle_details:
    xor byte [details_enabled], 1
    jmp .redraw
.done:
    ret

wait_ticks_2:
    pusha
    mov ah, 0x00
    int 0x1A
    mov bx, dx
.loop:
    mov ah, 0x00
    int 0x1A
    mov ax, dx
    sub ax, bx
    cmp ax, 2
    jb .loop
    popa
    ret

draw_menu:
    call clear_text
    mov si, menu_title
    mov dh, 2
    call draw_centered_line
    mov si, menu_subtitle
    mov dh, 3
    call draw_centered_line

    mov cx, 4
    xor bx, bx
.next_item:
    mov si, menu_item0
    cmp bl, 0
    je .have_item
    mov si, menu_item1
    cmp bl, 1
    je .have_item
    mov si, menu_item2
    cmp bl, 2
    je .have_item
    mov si, menu_item3
.have_item:
    mov dh, 8
    add dh, bl
    mov dl, 16
    mov al, ' '
    mov ah, 0x1E
    cmp bl, [selected_profile]
    jne .plain
    mov al, '>'
    mov ah, 0x70
.plain:
    call put_char_at
    mov dl, 18
    mov ah, 0x1F
    cmp bl, [selected_profile]
    jne .item_draw
    mov ah, 0x70
.item_draw:
    call draw_string_at
    inc bl
    loop .next_item

    mov si, hint_line1
    mov dh, 18
    call draw_centered_line
    mov si, hint_line2
    mov dh, 19
    call draw_centered_line

    cmp byte [details_enabled], 0
    je .no_details
    mov si, detail_prefix
    mov dh, 21
    mov dl, 8
    mov ah, 0x1F
    call draw_string_at
    mov bl, [selected_profile]
    xor bh, bh
    shl bx, 1
    mov si, [profile_detail_table + bx]
    mov dl, 20
    call draw_string_at
.no_details:
    ret

draw_centered_line:
    pusha
    call string_length
    mov bl, 80
    sub bl, al
    shr bl, 1
    mov dl, bl
    mov ah, 0x1F
    call draw_string_at
    popa
    ret

draw_string_at:
    pusha
.next:
    lodsb
    test al, al
    jz .done
    call put_char_at
    inc dl
    jmp .next
.done:
    popa
    ret

put_char_at:
    pusha
    xor bx, bx
    mov bl, dh
    mov ax, bx
    mov bl, 80
    mul bl
    xor bx, bx
    mov bl, dl
    add ax, bx
    shl ax, 1
    mov di, ax
    mov bx, 0xB800
    mov es, bx
    stosb
    mov al, ah
    stosb
    popa
    ret

print_line:
    pusha
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x0F
    int 0x10
    jmp .next
.done:
    popa
    ret

string_length:
    push bx
    push si
    xor bx, bx
.loop:
    cmp byte [si + bx], 0
    je .done
    inc bx
    jmp .loop
.done:
    mov al, bl
    pop si
    pop bx
    ret

; -------------------------------
; BIOS / FAT12 loading
; -------------------------------
load_root_file_to_lowmem:
    push si
    call find_root_file
    pop si
    jc .fail

    mov ax, [found_first_cluster]
    mov dx, [current_file_dest + 2]
    mov bx, [current_file_dest]

.next_cluster:
    cmp ax, 0x0FF8
    jae .done
    push ax
    call cluster_to_lba
    call read_sector_to_far
    add dword [current_file_dest], 512
    pop ax
    call fat12_next_cluster
    mov ax, dx
    jmp .next_cluster
.done:
    mov eax, [found_file_size]
    clc
    ret
.fail:
    stc
    ret

find_root_file:
    pusha
    call calc_fs_layout
    mov ax, [root_dir_lba]
    mov cx, [root_dir_sectors]
.search_sector:
    push ax
    call read_sector_to_buffer
    pop ax
    mov di, sector_buffer
    mov bx, 16
.search_entry:
    mov dl, [di]
    cmp dl, 0x00
    je .not_found
    cmp dl, 0xE5
    je .next_entry
    cmp byte [di + 11], 0x0F
    je .next_entry

    push si
    push di
    mov cx, 11
    repe cmpsb
    pop di
    pop si
    je .found
.next_entry:
    add di, 32
    dec bx
    jnz .search_entry
    inc ax
    loop .search_sector
.not_found:
    popa
    stc
    ret
.found:
    mov dx, [di + 26]
    mov [found_first_cluster], dx
    mov eax, [di + 28]
    mov [found_file_size], eax
    popa
    clc
    ret

calc_fs_layout:
    push ax
    push bx
    push dx
    xor ax, ax
    mov es, ax
    mov ax, [es:0x7C0E]
    mov [reserved_sectors], ax
    mov al, [es:0x7C10]
    mov [fat_count], al
    mov ax, [es:0x7C11]
    mov [root_entries], ax
    mov ax, [es:0x7C16]
    mov [sectors_per_fat], ax

    mov ax, [root_entries]
    mov bx, 32
    mul bx
    add ax, 511
    mov bx, 512
    div bx
    mov [root_dir_sectors], ax

    mov ax, [sectors_per_fat]
    xor bx, bx
    mov bl, [fat_count]
    mul bx
    add ax, [reserved_sectors]
    mov [root_dir_lba], ax

    mov ax, [root_dir_lba]
    add ax, [root_dir_sectors]
    mov [first_data_lba], ax

    pop dx
    pop bx
    pop ax
    ret

cluster_to_lba:
    sub ax, 2
    add ax, [first_data_lba]
    ret

fat12_next_cluster:
    push ax
    push bx
    push cx
    push si

    mov bx, ax
    mov dx, ax
    shr dx, 1
    add bx, dx                   ; offset = cluster + cluster/2
    mov ax, bx
    shr ax, 9
    add ax, [reserved_sectors]
    mov [fat_sector_lba], ax
    and bx, 0x01FF
    mov [fat_entry_offset], bx

    mov ax, [fat_sector_lba]
    call read_sector_to_buffer
    inc ax
    mov di, sector_buffer + 512
    call read_sector_to_esdi

    mov bx, [fat_entry_offset]
    mov dx, [sector_buffer + bx]
    pop si
    pop cx
    pop bx
    pop ax

    test ax, 1
    jz .even
    shr dx, 4
    and dx, 0x0FFF
    ret
.even:
    and dx, 0x0FFF
    ret

read_sector_to_buffer:
    push ax
    mov ax, cs
    mov es, ax
    mov bx, sector_buffer
    pop ax
    jmp read_sector_common

read_sector_to_esdi:
    push ax
    mov bx, di
    pop ax
    jmp read_sector_common

read_sector_to_far:
    push ax
    push bx
    push dx
    mov ax, [current_file_dest + 2]
    mov es, ax
    mov bx, [current_file_dest]
    pop dx
    pop bx
    pop ax

read_sector_common:
    pusha
    mov di, 3
.retry:
    push ax
    xor dx, dx
    div word [bpb_sectors_per_track_const]
    mov cx, dx
    inc cl
    xor dx, dx
    div word [bpb_heads_const]
    mov dh, dl
    mov ch, al
    shl ah, 6
    or cl, ah
    mov dl, [boot_drive]
    mov ah, 0x02
    mov al, 0x01
    int 0x13
    jnc .ok
    xor ax, ax
    int 0x13
    dec di
    jnz .retry
    mov si, error_disk
    call print_line
    jmp halt
.ok:
    add sp, 2
    popa
    ret

; -------------------------------
; BIOS helpers
; -------------------------------
detect_memory_map:
    pusha
    mov dword [e820_count], 0
    xor ebx, ebx
    mov di, e820_buffer
.next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov ax, cs
    mov es, ax
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp dword [e820_count], E820_MAX_ENTRIES
    jae .done
    mov dword [es:di + 20], 0
    add di, 24
    inc dword [e820_count]
    test ebx, ebx
    jnz .next
.done:
    popa
    ret

try_set_vbe_mode:
    pusha
    mov byte [fb_valid], 0
    mov si, vbe_modes
.next_mode:
    lodsw
    test ax, ax
    jz .done
    mov [candidate_mode], ax
    mov ax, 0x4F01
    mov cx, [candidate_mode]
    mov dx, cs
    mov es, dx
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    jne .next_mode
    test word [vbe_mode_info + 0], 0x0080
    jz .next_mode
    mov ax, 0x4F02
    mov bx, [candidate_mode]
    or bx, 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    mov eax, [vbe_mode_info + 40]
    mov [fb_addr], eax
    mov ax, [vbe_mode_info + 16]
    mov [fb_pitch], ax
    mov ax, [vbe_mode_info + 18]
    mov [fb_width], ax
    mov ax, [vbe_mode_info + 20]
    mov [fb_height], ax
    mov al, [vbe_mode_info + 25]
    mov [fb_bpp], al
    mov byte [fb_valid], 1
    jmp .done
.done:
    popa
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

halt:
    cli
.hang:
    hlt
    jmp .hang

; -------------------------------
; 32-bit protected-mode loader
; -------------------------------
BITS 32
protected_mode_entry:
    mov ax, SEL_LOADER_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, LOADER_LOAD_BASE + 0xF000

    cmp byte [selected_profile], 1
    je .load64

    mov esi, TEMP_KERNEL_PHYS
    call load_elf32_image
    jmp .after_kernel

.load64:
    mov esi, TEMP_KERNEL_PHYS
    call load_elf64_image

.after_kernel:
    mov [kernel_max_end], eax

    cmp dword [module_file_size], 0
    je .no_module
    mov esi, TEMP_MODULE_PHYS
    mov edi, MODULE_DEST_PHYS
    mov ecx, [module_file_size]
    cld
    rep movsb
    mov dword [module_dest_addr], MODULE_DEST_PHYS
.no_module:

    call build_mb2_info

    mov ax, SEL_FLAT_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x0009F000

    mov eax, 0x36D76289
    mov ebx, MB2_INFO_PHYS
    jmp [kernel_entry_point]

load_elf32_image:
    pushad
    mov eax, [esi + 24]
    mov [kernel_entry_point], eax
    movzx ebx, word [esi + 44]
    movzx edx, word [esi + 42]
    mov eax, [esi + 28]
    add eax, esi
    mov [ph_table_ptr], eax
    mov [ph_count], edx
    mov [ph_size], ebx
    mov dword [kernel_loaded_end], 0

    xor ecx, ecx
.next32:
    cmp ecx, [ph_count]
    jae .done32
    mov ebx, [ph_table_ptr]
    mov edx, [ph_size]
    imul edx, ecx
    add ebx, edx
    cmp dword [ebx + 0], 1
    jne .skip32

    mov edi, [ebx + 12]
    mov eax, [ebx + 4]
    add eax, esi
    mov esi, eax
    mov ecx, [ebx + 16]
    cld
    rep movsb

    mov ecx, [ebx + 20]
    sub ecx, [ebx + 16]
    jbe .track32
    xor eax, eax
    rep stosb
.track32:
    mov eax, [ebx + 12]
    add eax, [ebx + 20]
    cmp eax, [kernel_loaded_end]
    jbe .skip32
    mov [kernel_loaded_end], eax
.skip32:
    inc ecx
    jmp .next32
.done32:
    mov eax, [kernel_loaded_end]
    popad
    ret

load_elf64_image:
    pushad
    mov eax, [esi + 24]
    mov [kernel_entry_point], eax
    movzx ebx, word [esi + 54]
    movzx edx, word [esi + 56]
    mov eax, [esi + 32]
    add eax, esi
    mov [ph_table_ptr], eax
    mov [ph_count], edx
    mov [ph_size], ebx
    mov dword [kernel_loaded_end], 0

    xor ecx, ecx
.next64:
    cmp ecx, [ph_count]
    jae .done64
    mov ebx, [ph_table_ptr]
    mov edx, [ph_size]
    imul edx, ecx
    add ebx, edx
    cmp dword [ebx + 0], 1
    jne .skip64

    mov edi, [ebx + 24]
    mov eax, [ebx + 8]
    add eax, esi
    mov esi, eax
    mov ecx, [ebx + 32]
    cld
    rep movsb

    mov ecx, [ebx + 40]
    sub ecx, [ebx + 32]
    jbe .track64
    xor eax, eax
    rep stosb
.track64:
    mov eax, [ebx + 24]
    add eax, [ebx + 40]
    cmp eax, [kernel_loaded_end]
    jbe .skip64
    mov [kernel_loaded_end], eax
.skip64:
    inc ecx
    jmp .next64
.done64:
    mov eax, [kernel_loaded_end]
    popad
    ret

build_mb2_info:
    mov edi, MB2_INFO_PHYS + 8

    ; cmdline tag
    movzx eax, byte [selected_profile]
    mov esi, [profile_cmdline_table + eax * 4]
    cmp byte [esi], 0
    je .skip_cmdline
    mov dword [edi + 0], 1
    push edi
    lea ebx, [edi + 8]
.copy_cmd:
    lodsb
    mov [ebx], al
    inc ebx
    test al, al
    jnz .copy_cmd
    mov eax, ebx
    sub eax, edi
    mov [edi + 4], eax
    add edi, eax
    add edi, 7
    and edi, 0xFFFFFFF8
    pop eax
.skip_cmdline:

    ; basic meminfo tag
    mov dword [edi + 0], 4
    mov dword [edi + 4], 16
    mov dword [edi + 8], 639
    mov eax, [available_mem_kb_above_1m]
    mov [edi + 12], eax
    add edi, 16

    ; mmap tag
    mov dword [edi + 0], 6
    mov eax, [e820_count]
    imul eax, 24
    add eax, 16
    mov [edi + 4], eax
    mov dword [edi + 8], 24
    mov dword [edi + 12], 0
    mov esi, e820_buffer + LOADER_LOAD_BASE
    lea ebx, [edi + 16]
    mov ecx, [e820_count]
.copy_map:
    test ecx, ecx
    jz .mmap_done
    push ecx
    mov ecx, 24
    rep movsb
    pop ecx
    dec ecx
    jmp .copy_map
.mmap_done:
    mov eax, [edi + 4]
    add edi, eax
    add edi, 7
    and edi, 0xFFFFFFF8

    cmp byte [fb_valid], 0
    je .skip_fb
    mov dword [edi + 0], 8
    mov dword [edi + 4], 32
    mov eax, [fb_addr]
    mov [edi + 8], eax
    mov dword [edi + 12], 0
    movzx eax, word [fb_pitch]
    mov [edi + 16], eax
    movzx eax, word [fb_width]
    mov [edi + 20], eax
    movzx eax, word [fb_height]
    mov [edi + 24], eax
    movzx eax, byte [fb_bpp]
    mov [edi + 28], al
    mov byte [edi + 29], 1
    mov byte [edi + 30], 0
    mov byte [edi + 31], 0
    add edi, 32
.skip_fb:

    cmp dword [module_file_size], 0
    je .skip_mod
    mov dword [edi + 0], 3
    mov eax, [module_dest_addr]
    mov [edi + 8], eax
    add eax, [module_file_size]
    mov [edi + 12], eax
    lea ebx, [edi + 16]
    mov esi, module_name_string
.copy_mod:
    lodsb
    mov [ebx], al
    inc ebx
    test al, al
    jnz .copy_mod
    mov eax, ebx
    sub eax, edi
    mov [edi + 4], eax
    add edi, eax
    add edi, 7
    and edi, 0xFFFFFFF8
.skip_mod:

    mov dword [edi + 0], 0
    mov dword [edi + 4], 8
    add edi, 8
    mov eax, edi
    sub eax, MB2_INFO_PHYS
    mov [MB2_INFO_PHYS], eax
    mov dword [MB2_INFO_PHYS + 4], 0
    ret

; -------------------------------
; Data
; -------------------------------
BITS 16
align 4
menu_title        db 'Chrysalis Custom Bootloader', 0
menu_subtitle     db 'BIOS boot v1 - no GRUB on ISO path', 0
menu_item0        db 'Chrysalis OS (Console)', 0
menu_item1        db 'Chrysalis OS (64-bit Prototype)', 0
menu_item2        db 'Chrysalis OS (Console, PIC Safe)', 0
menu_item3        db 'Chrysalis OS (Console, Debug)', 0
hint_line1        db 'Up/Down selects, Enter boots, D toggles details.', 0
hint_line2        db 'Timeout: 5 seconds. Solid fallback if VBE mode fails.', 0
detail_prefix     db 'Profile: ', 0
splash_1          db '   ____ _                _           _ _     ', 0
splash_2          db '  / ___| |__  _ __ _   _| |__   ___ | (_)___ ', 0
splash_3          db ' | |   | `_ \| `__| | | | `_ \ / _ \| | / __|', 0
error_missing_file db 'Required boot file missing.', 13, 10, 0
error_disk         db 'Disk read failed during FAT load.', 13, 10, 0
kernel32_name     db 'KERNEL  BIN'
kernel64_name     db 'KERNEL64BIN'
hello64_name      db 'HELLO64 ELF'
module_name_string db 'hello64.elf', 0
cmdline_empty     db 0
cmdline_pic       db 'apic=off', 0
cmdline_debug     db '--debug', 0
profile_detail0   db 'kernel.bin', 0
profile_detail1   db 'kernel64.bin + hello64.elf', 0
profile_detail2   db 'kernel.bin apic=off', 0
profile_detail3   db 'kernel.bin --debug', 0

profile_detail_table dw profile_detail0, profile_detail1, profile_detail2, profile_detail3
vbe_modes         dw 0x118, 0x117, 0x115, 0x114, 0

align 4
profile_cmdline_table dd cmdline_empty + LOADER_LOAD_BASE, cmdline_empty + LOADER_LOAD_BASE, cmdline_pic + LOADER_LOAD_BASE, cmdline_debug + LOADER_LOAD_BASE
menu_deadline     dw 0
selected_profile  db 0
details_enabled   db 0
boot_drive        db 0
fb_valid          db 0
fb_bpp            db 0
candidate_mode    dw 0
reserved_sectors  dw 0
root_entries      dw 0
sectors_per_fat   dw 0
root_dir_sectors  dw 0
root_dir_lba      dw 0
first_data_lba    dw 0
fat_sector_lba    dw 0
fat_entry_offset  dw 0
fat_count         db 0
bpb_sectors_per_track_const dw 18
bpb_heads_const   dw 2
align 4
current_file_dest dd 0
found_file_size   dd 0
module_file_size  dd 0
kernel_file_size  dd 0
module_dest_addr  dd 0
kernel_entry_point dd 0
kernel_loaded_end dd 0
kernel_max_end    dd 0
available_mem_kb_above_1m dd 0
ph_table_ptr      dd 0
ph_count          dd 0
ph_size           dd 0
found_first_cluster dw 0
fb_addr           dd 0
fb_pitch          dw 0
fb_width          dw 0
fb_height         dw 0
align 4
e820_count        dd 0
sector_buffer:
    times 1024 db 0
e820_buffer:
    times E820_MAX_ENTRIES * 24 db 0
vbe_mode_info:
    times 256 db 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00409A010000FFFF
    dq 0x004092010000FFFF
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start + LOADER_LOAD_BASE
