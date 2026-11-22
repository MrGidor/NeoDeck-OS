[org 0x7c00]

; KERNEL_SECTORS is written by build.sh (kernel size rounded up to sectors).
%ifndef KERNEL_SECTORS
    %error "KERNEL_SECTORS not defined - build with build.sh"
%endif
%include "layout.inc"           ; KERNEL_BASE, BOOT_STAGE_ADDRESS

; Floppy geometry (1.44 MB image) for now. The kernel is read one sector at a time
SECTORS_PER_TRACK equ 18
HEADS             equ 2
STAGE_SEGMENT     equ BOOT_STAGE_ADDRESS >> 4

mov [BOOT_DISK], dl

xor ax, ax
mov es, ax
mov ds, ax
mov bp, 0x8000
mov sp, bp

mov ah, 0x0e
mov al, 'R'
xor bh, bh
int 0x10

call load_kernel

mov ah, 0x0e
mov al, 'L'
xor bh, bh
int 0x10

call enable_a20

CODE_SEG equ GDT_code - GDT_start
DATA_SEG equ GDT_data - GDT_start

cli
lgdt [GDT_descriptor]
mov eax, cr0
or eax, 1
mov cr0, eax
jmp CODE_SEG:start_protected_mode

; ---------------------------------------------------------------------------
; Read KERNEL_SECTORS sectors starting at LBA 1 into BOOT_STAGE_ADDRESS.
; ---------------------------------------------------------------------------
load_kernel:
    mov ax, STAGE_SEGMENT
    mov es, ax
    xor bx, bx
    mov byte [cur_sector], 2        ; sector numbers are 1-based; 1 = boot sector
    mov byte [cur_head], 0
    mov byte [cur_cyl], 0
    mov word [sectors_left], KERNEL_SECTORS
.next:
    mov si, 3                       ; retries
.retry:
    mov ah, 0x02
    mov al, 1
    mov ch, [cur_cyl]
    mov cl, [cur_sector]
    mov dh, [cur_head]
    mov dl, [BOOT_DISK]
    int 0x13
    jnc .ok
    xor ax, ax                      ; reset controller and retry
    mov dl, [BOOT_DISK]
    int 0x13
    dec si
    jnz .retry
    jmp disk_error
.ok:
    mov ax, es                      ; next 512 bytes = +0x20 paragraphs
    add ax, 0x20
    mov es, ax

    inc byte [cur_sector]
    cmp byte [cur_sector], SECTORS_PER_TRACK + 1
    jb .chs_done
    mov byte [cur_sector], 1
    inc byte [cur_head]
    cmp byte [cur_head], HEADS
    jb .chs_done
    mov byte [cur_head], 0
    inc byte [cur_cyl]
.chs_done:
    dec word [sectors_left]
    jnz .next
    ret

; ---------------------------------------------------------------------------
; A20 gate: needed to touch memory at 1 MB and above.
; ---------------------------------------------------------------------------
enable_a20:
    mov ax, 0x2401                  ; BIOS method (result ignored)
    int 0x15
    in al, 0x92                     ; fast A20
    cmp al, 0xFF                    ; 0xFF = port not present
    je .done
    or al, 2
    and al, 0xFE                    ; never set bit 0
    out 0x92, al
.done:
    ret

disk_error:
    mov si, DISK_ERROR_MSG
    call print_string
    jmp $

print_string:
    pusha
    mov ah, 0x0e
    xor bh, bh
.repeat:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .repeat
.done:
    popa
    ret

BOOT_DISK:     db 0
cur_sector:    db 0
cur_head:      db 0
cur_cyl:       db 0
sectors_left:  dw 0
DISK_ERROR_MSG db "Disk read error!", 0

GDT_start:
    GDT_null:
        dd 0x0
        dd 0x0

    GDT_code:
        dw 0xffff
        dw 0x0
        db 0x0
        db 0b10011010
        db 0b11001111
        db 0x0

    GDT_data:
        dw 0xffff
        dw 0x0
        db 0x0
        db 0b10010010
        db 0b11001111
        db 0x0
GDT_end:

GDT_descriptor:
    dw GDT_end - GDT_start - 1
    dd GDT_start

[bits 32]
start_protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov esp, 0x7000                 ; scratch stack below the boot sector

    ; Copy the staged kernel up to 1 MB.
    cld
    mov esi, BOOT_STAGE_ADDRESS
    mov edi, KERNEL_BASE
    mov ecx, (KERNEL_SECTORS * 512) / 4
    rep movsd

    mov eax, KERNEL_BASE
    jmp eax

times 510-($-$$) db 0
dw 0xaa55
