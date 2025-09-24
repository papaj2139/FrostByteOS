[BITS 32]
section .text

extern syscall_dispatch
extern syscall_capture_user_frame
extern syscall_mark_enter
extern syscall_mark_exit

;syscall handler entry point (interrupt 0x80)
global syscall_handler_asm
syscall_handler_asm:
    ;save registers
    push ebp
    mov ebp, esp
    ;preserve all GPRs across the capture call
    pushad
    ;mark that we're now executing in kernel for this process (safe GPRs preserved)
    call syscall_mark_enter
    ;capture the user return frame (EIP, CS, EFLAGS, USERESP, SS, EBP) and user GPRs
    ;CPU-saved frame relative to EBP (after push ebp): [EBP+4]=EIP [EBP+8]=CS [EBP+12]=EFLAGS [EBP+16]=USERESP [EBP+20]=SS
    ;pushad saved user GPRs at [EBP-4]=EAX [EBP-8]=ECX [EBP-12]=EDX [EBP-16]=EBX [EBP-28]=ESI [EBP-32]=EDI
    push dword [ebp - 32]   ;EDI
    push dword [ebp - 28]   ;ESI
    push dword [ebp - 12]   ;EDX
    push dword [ebp - 8]    ;ECX
    push dword [ebp - 16]   ;EBX
    push dword [ebp - 4]    ;EAX
    push dword [ebp + 0]    ;user EBP saved by our push ebp
    push dword [ebp + 20]   ;SS
    push dword [ebp + 16]   ;USERESP
    push dword [ebp + 12]   ;EFLAGS
    push dword [ebp + 8]    ;CS
    push dword [ebp + 4]    ;EIP
    call syscall_capture_user_frame
    add esp, 48
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

    ;clear in-kernel before returning to user preserve EAX (syscall return)
    push eax
    call syscall_mark_exit
    pop eax

    ;restore registers
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop ebp

    ;return to user
    iretd
