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
    ;preserve all GPRs across the capture call
    pushad
    ;capture the user return frame (EIP, CS, EFLAGS, USERESP, SS)
    ;at entry (before push ebp): [ESP+0]=EIP, [ESP+4]=CS, [ESP+8]=EFLAGS, [ESP+12]=USERESP, [ESP+16]=SS
    ;after push ebp those are at [EBP+4..+20]
    push dword [ebp + 20]   ;SS
    push dword [ebp + 16]   ;USERESP
    push dword [ebp + 12]   ;EFLAGS
    push dword [ebp + 8]    ;CS
    push dword [ebp + 4]    ;EIP
    call syscall_capture_user_frame
    add esp, 20
    ;restore original user register values
    popad
    ;now save caller-saved registers for dispatcher call
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
    
    ;return to user
    iretd
