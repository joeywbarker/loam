; LOAM boot sector -- original code, no external assets.
; BIOS -> real mode -> 32-bit protected -> 64-bit long mode -> kernel at 0x10000.
[org 0x7C00]
[bits 16]

KERNEL_SECTORS equ 64            ; 32 KiB kernel window, LBA 1..64

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    sti

    mov si, dap                  ; extended read: kernel -> 0x1000:0000
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc  disk_error

    in  al, 0x92                 ; fast A20
    or  al, 2
    and al, 0xFE
    out 0x92, al

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm32

disk_error:
    mov ax, 0x0E45               ; 'E'
    int 0x10
    hlt
    jmp $

align 4
dap:
    db 0x10, 0
    dw KERNEL_SECTORS
    dw 0x0000, 0x1000
    dq 1
boot_drive: db 0

align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF        ; 0x08 32-bit code
    dq 0x00CF92000000FFFF        ; 0x10 data
    dq 0x00AF9A000000FFFF        ; 0x18 64-bit code
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

[bits 32]
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov edi, 0x1000              ; zero three page-table pages
    xor eax, eax
    mov ecx, 0xC00
    rep stosd
    mov dword [0x1000], 0x2003   ; PML4[0] -> PDPT
    mov dword [0x2000], 0x3003   ; PDPT[0] -> PD
    mov dword [0x3000], 0x83     ; PD[0]   = 2 MiB identity page

    mov eax, cr4
    or  eax, 0x20                ; PAE
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax
    mov ecx, 0xC0000080          ; EFER
    rdmsr
    or  eax, 0x100               ; LME
    wrmsr
    mov eax, cr0
    or  eax, 0x80000000          ; PG
    mov cr0, eax
    jmp 0x18:lm64

[bits 64]
lm64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x8FFF8
    mov rax, 0x10000
    jmp rax

times 510 - ($ - $$) db 0
dw 0xAA55
