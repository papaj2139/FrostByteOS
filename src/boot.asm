section .multiboot
align 4
    dd 0x1BADB002              ; magic
    dd 0x03                    ; flags: align modules (bit0) | request mem info (bit1)
    dd -(0x1BADB002 + 0x03)    ; checksum

section .text
global start
extern kmain

start:
    push ebx
    push eax
    call kmain
    add esp, 8

    cli
    hlt
