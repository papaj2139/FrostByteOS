[BITS 32]
section .text

extern syscall_dispatch
extern syscall_capture_user_frame

;syscall handler entry point (interrupt 0x80)
global syscall_handler_asm
syscall_handler_asm:
    ;save registers
    push ebp
    mov ebp, esp
    push ebx
    push ecx
    push edx
    push esi
    push edi
    
    ;push syscall arguments in reverse order (C calling convention)
    ;arguments are passed in EAX=syscall_num EBX=arg1 ECX=arg2 EDX=arg3 ESI=arg4 EDI=arg5
    push edi    ;arg5
    push esi    ;arg4
    push edx    ;arg3
    push ecx    ;arg2
    push ebx    ;arg1
    push eax    ;syscall_num
    
    ;call the C syscall dispatcher
    call syscall_dispatch
    
    ;clean up arguments from stack
    add esp, 24  ;6 arguments * 4 bytes each
        
    ;restore registers
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop ebp
    
    ;return
    iretd
