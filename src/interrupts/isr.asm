[BITS 32]
section .text

extern irq_dispatch

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
