[BITS 32]
section .text

;load GDT and reload segment registers
global gdt_flush
gdt_flush:
    mov eax, [esp+4]  ;get GDT pointer from parameter
    lgdt [eax]        ;load GDT
    
    mov ax, 0x10      ;kernel data segment offset
    mov ds, ax        ;reload data segment
    mov es, ax        ;reload extra segment
    mov fs, ax        ;reload F segment
    mov gs, ax        ;reload G segment
    mov ss, ax        ;reload stack segment
    
    ;reload code segment by far jump
    jmp 0x08:.flush 
.flush:
    ret
