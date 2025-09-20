section .text

global context_switch_asm
global switch_to_user_mode
global enter_user_with_pd

context_switch_asm:
    push ebp
    mov ebp, esp
    
    ;get parameters
    mov esi, [ebp + 8]  ;old_context
    mov edi, [ebp + 12] ;new_context
    
    ;save current context to old_context (if provided)
    test esi, esi
    jz .restore_new     ;skip save if old_context is NULL
    
    ;push all GPRs to capture their original values
    pushad                  ;pushes EAX, ECX, EDX, EBX, ESP(before), EBP, ESI, EDI
    
    ;ater pushad layout at [esp+offset]:
    ;0: EAX, 4: ECX, 8: EDX, 12: EBX, 16: original ESP, 20: EBP, 24: ESI, 28: EDI
    ;store into cpu_context_t in order: eax, ebx, ecx, edx, esi, edi, esp, ebp
    mov eax, [esp + 0]      ;EAX
    mov [esi + 0], eax
    mov eax, [esp + 12]     ;EBX
    mov [esi + 4], eax
    mov eax, [esp + 4]      ;ECX
    mov [esi + 8], eax
    mov eax, [esp + 8]      ;EDX
    mov [esi + 12], eax
    mov eax, [esp + 24]     ;ESI
    mov [esi + 16], eax
    mov eax, [esp + 28]     ;EDI
    mov [esi + 20], eax
    mov eax, [esp + 16]     ;original ESP
    mov [esi + 24], eax
    mov eax, [esp + 20]     ;EBP
    mov [esi + 28], eax
    
    ;save eip (return address)
    mov ecx, [ebp + 4]      ;get return address from stack
    mov [esi + 32], ecx     ;save as eip
    
    ;save eflags
    pushfd
    pop ecx
    mov [esi + 36], ecx
    
    ;save segment registers (16-bit values)
    xor ecx, ecx
    mov cx, cs
    mov [esi + 40], ecx
    xor ecx, ecx
    mov cx, ds
    mov [esi + 44], ecx
    xor ecx, ecx
    mov cx, es
    mov [esi + 48], ecx
    xor ecx, ecx
    mov cx, fs
    mov [esi + 52], ecx
    xor ecx, ecx
    mov cx, gs
    mov [esi + 56], ecx
    xor ecx, ecx
    mov cx, ss
    mov [esi + 60], ecx

    ;unwind pushad
    add esp, 32

    ;build a stable fake frame for kernel resume on the old (current) stack
    ;layout: [ESP]: saved EBP (original caller EBP), [ESP+4]: return EIP (saved above)
    mov eax, [esi + 32]     ;saved return EIP (caller of context_switch_asm)
    mov ecx, [ebp]          ;original caller EBP to restore
    sub esp, 8              ;allocate space for fake [EBP][RET]
    mov [esp], ecx          ;saved EBP
    mov [esp + 4], eax      ;return address
    mov [esi + 28], esp     ;context->ebp points to start of fake frame

.restore_new:
    ;restore context from new_context
    test edi, edi
    jz .done            ;skip restore if new_context is NULL

    ;restore data segment registers first (from context)
    mov ecx, [edi + 44]  ;ds
    mov ds, cx
    mov ecx, [edi + 48]  ;es
    mov es, cx
    mov ecx, [edi + 52]  ;fs
    mov fs, cx
    mov ecx, [edi + 56]  ;gs
    mov gs, cx

    ;determine target privilege level from CS selector in context
    mov ecx, [edi + 40]   ;cs
    test ecx, 3
    jnz .to_user          ;if RPL!=0, switch to user with iret

    ;kernel target (RPL=0): restore stack/regs then return via saved frame
    ;use EDX as a stable base pointer to the context to avoid clobbering EDI early
    mov edx, edi
    cli                     ;avoid IRQs during stack pivot
    ;do not modify SS kernel SS is constant
    ;for a proper function return ESP must point to saved EBP (not the ESP snapshot)
    mov esp, [edx + 28]   ;esp = saved EBP

    ;restore general purpose registers (read from EDX base)
    mov eax, [edx + 0]
    mov ebx, [edx + 4]
    mov ecx, [edx + 8]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    ;do not write EBP here it will be restored from the saved stack frame

    ;restore EFLAGS (ensure IF=1 so timer continues)
    mov eax, [edx + 36]     
    or eax, 0x200
    push eax
    popfd

    ;restore EDX last and return using the saved frame (pop ebp; ret)
    mov edx, [edx + 12]
    pop ebp
    ret

.to_user:
    ;disable interrupts while building the iret frame
    cli
    ;build an iret frame for a ring transition to user mode on the CURRENT (kernel) stack
    ;order expected by iret with CPL change: EIP, CS, EFLAGS, ESP, SS (push in reverse)
    push dword [edi + 60]      ;SS (user data)
    push dword [edi + 24]      ;ESP (user stack)
    push dword [edi + 36]      ;EFLAGS
    push dword [edi + 40]      ;CS (user code)
    push dword [edi + 32]      ;EIP (user entry)

    ;use EDX as a stable base pointer to context while restoring GPRs
    mov edx, edi

    ;restore minimal register (EBP) before iret
    mov ebp, [edx + 28]

    ;ensure user data segments are loaded before dropping to CPL=3
    mov ax, 0x23            ;user data selector 
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;restore general purpose registers from saved user context
    mov eax, [edx + 0]
    mov ebx, [edx + 4]
    mov ecx, [edx + 8]
    mov esi, [edx + 16]
    mov edi, [edx + 20]
    mov edx, [edx + 12]     ;restore EDX last (base no longer needed)

    ;perform the privilege-level switch
    iretd

.done:
    pop ebp
    ret

;atomically switch to a given page directory and enter user mode at (EIP, ESP)
;args (cdecl): [esp+4]=dir_phys, [esp+8]=user_eip, [esp+12]=user_esp
enter_user_with_pd:
    cli
    mov eax, [esp + 4]      ;dir_phys
    mov cr3, eax            ;switch to target page directory

    mov ecx, [esp + 8]      ;user EIP
    mov edx, [esp + 12]     ;user ESP

    ;load user data segments before iret
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;build iret frame: SS, ESP, EFLAGS, CS, EIP
    push dword 0x23         ;SS (user data)
    push edx                ;ESP (user stack)
    pushfd                  ;EFLAGS
    pop eax
    and eax, ~0x200         ;IF=0 in user mode for first entry
    push eax
    push dword 0x1B         ;CS (user code)
    push ecx                ;EIP (user entry)
    iretd

;switches from kernel mode to user mode
switch_to_user_mode:
    cli                     ;disable interrupts during switch

    mov ecx, [esp + 4]      ;get EIP parameter into ECX
    mov edx, [esp + 8]      ;get ESP parameter into EDX

    ;setup user data segments before iret (use AX only)
    mov ax, 0x23            ;user data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;build iret frame: SS, ESP, EFLAGS, CS, EIP
    push dword 0x23         ;SS (user data)
    push edx                ;ESP (user stack)
    pushfd                  ;EFLAGS
    pop eax
    or eax, 0x200           ;IF=1 in user mode (restore stable behavior)
    push eax
    push dword 0x1B         ;CS (user code)
    push ecx                ;EIP (user entry)
    iretd

;helper function to save current context (used by scheduler)
global save_current_context
save_current_context:
    push ebp
    mov ebp, esp
    
    mov eax, [ebp + 8]      ;get context pointer
    
    ;save all registers
    mov [eax + 0],  eax
    mov [eax + 4],  ebx
    mov [eax + 8],  ecx
    mov [eax + 12], edx
    mov [eax + 16], esi
    mov [eax + 20], edi
    mov [eax + 24], esp
    mov [eax + 28], ebp
    
    ;save return address as eip
    mov ecx, [ebp + 4]
    mov [eax + 32], ecx
    
    ;save eflags
    pushfd
    pop ecx
    mov [eax + 36], ecx
    
    ;save segments
    mov cx, cs
    mov [eax + 40], cx
    mov cx, ds
    mov [eax + 44], cx
    mov cx, es
    mov [eax + 48], cx
    mov cx, fs
    mov [eax + 52], cx
    mov cx, gs
    mov [eax + 56], cx
    mov cx, ss
    mov [eax + 60], cx
    
    pop ebp
    ret
