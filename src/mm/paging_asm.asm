[BITS 32]
section .text

global enable_paging
global flush_tlb

enable_paging:
    mov eax, [esp+4]    ;get page directory physical address
    mov cr3, eax        ;load page directory

    mov eax, cr0
    or eax, 0x80000000  ;set PG bit
    mov cr0, eax        ;enable paging
    ret

flush_tlb:
    mov eax, cr3
    mov cr3, eax        ;reload CR3 to flush TLB
    ret
