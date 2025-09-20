[BITS 32]
section .text

extern irq_dispatch
extern isr_exception_dispatch
extern isr_exception_dispatch_ext

;exception stubs (ISRs 0-31)
%macro ISR_NOERR 1
global isr%1
isr%1:
    pushad
    push dword 0           ;no error code
    push dword %1          ;vector number
    call isr_exception_dispatch
    add esp, 8             ;pop our args
    popad
    iretd
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    pushad
    push dword [esp + 32]  ;original CPU-pushed error code
    push dword %1          ;vector number
    call isr_exception_dispatch
    add esp, 8             ;pop our args
    popad
    add esp, 4             ;discard original error code
    iretd
%endmacro

;define exception vectors
ISR_NOERR 0   ;division by zero error
ISR_NOERR 1   ;debug
ISR_NOERR 2   ;non-maskable interrupt
ISR_NOERR 3   ;breakpoint
ISR_NOERR 4   ;overflow
ISR_NOERR 5   ;bound range exceeded
ISR_NOERR 6   ;invalidopcode
ISR_NOERR 7   ;device not available
ISR_ERR 8     ;double Fault (pushes error code)
ISR_NOERR 9   ;coprocessor segment overrun (reserved)
ISR_ERR 10    ;invalid TSS
ISR_ERR 11    ;segment not present
ISR_ERR 12    ;stack-segment fault
ISR_ERR 13    ;general protection fault
;page fault handler with extended context passing
global isr14
isr14:
    pushad
    ;after pushad, the CPU-pushed frame is located above the 32-byte pushad area
    ;layout at that point (from lower to higher addresses):
    ; [ ... pushad 32 bytes ... ] then: [ERR] [EIP] [CS] [EFLAGS] [USERESP?] [SS?]
    ;ERR is at [esp + 32]
    mov eax, [esp + 32]   ;errcode
    mov ecx, [esp + 36]   ;eip
    mov edx, [esp + 40]   ;cs
    mov ebx, [esp + 44]   ;eflags
    mov esi, [esp + 48]   ;useresp (only present on CPL change)
    mov edi, [esp + 52]   ;ss (only present on CPL change)

    ;push args for C handler (in reverse order): ss, useresp, eflags, cs, eip, errcode, vector
    push edi    ;ss
    push esi    ;useresp
    push ebx    ;eflags
    push edx    ;cs
    push ecx    ;eip
    push eax    ;errcode
    push dword 14 ;vector
    call isr_exception_dispatch_ext
    add esp, 28
    popad
    add esp, 4             ;discard original CPU-pushed error code
    iretd
ISR_NOERR 15  ;reserved
ISR_NOERR 16  ;x87 floating-point exception
ISR_ERR 17    ;alignment check
ISR_NOERR 18  ;machine check
ISR_NOERR 19  ;SIMD floating-point exception
ISR_NOERR 20  ;virtualization exception
ISR_NOERR 21  ;control protection exception
ISR_NOERR 22  ;reserved
ISR_NOERR 23  ;reserved
ISR_NOERR 24  ;reserved
ISR_NOERR 25  ;reserved
ISR_NOERR 26  ;reserved
ISR_NOERR 27  ;reserved
ISR_NOERR 28  ;hypervisor injection exception
ISR_NOERR 29  ;VMM communication exception
ISR_NOERR 30  ;security exception
ISR_NOERR 31  ;reserved

global irq0
irq0:
    pushad
    push dword 0
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq1
irq1:
    pushad
    push dword 1
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq2
irq2:
    pushad
    push dword 2
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq3
irq3:
    pushad
    push dword 3
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq4
irq4:
    pushad
    push dword 4
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq5
irq5:
    pushad
    push dword 5
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq6
irq6:
    pushad
    push dword 6
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq7
irq7:
    pushad
    push dword 7
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq8
irq8:
    pushad
    push dword 8
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq9
irq9:
    pushad
    push dword 9
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq10
irq10:
    pushad
    push dword 10
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq11
irq11:
    pushad
    push dword 11
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq12
irq12:
    pushad
    push dword 12
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq13
irq13:
    pushad
    push dword 13
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq14
irq14:
    pushad
    push dword 14
    call irq_dispatch
    add esp, 4
    popad
    iretd

global irq15
irq15:
    pushad
    push dword 15
    call irq_dispatch
    add esp, 4
    popad
    iretd
