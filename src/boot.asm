section .multiboot
align 4
    dd 0x1BADB002              ; magic
    dd 0x01                    ; flags: request memory info (i think)
    dd -(0x1BADB002 + 0x01)    ; checksum

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
