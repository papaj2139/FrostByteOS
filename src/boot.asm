section .multiboot
align 4
    dd 0x1BADB002              ; magic
    dd 0x01                    ; flags: request memory info (i think)
    dd -(0x1BADB002 + 0x01)    ; checksum

section .text
global start
extern kmain

start:
    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    push ebx
    push eax
    call kmain

    cli
    hlt
