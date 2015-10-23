;;; COPYRIGHT_NOTICE_1

%ifndef __X86_64__

SECTION .text

%define REGISTER_SIZE 4

; // A stub prologue that sets up an ebp-based frame and saves ebx, esi, edi.
%macro fullStubProlog 0
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi
%endmacro

; // Inverse of fullStubProlog
%macro fullStubEpilog 0
    pop edi
    pop esi
    pop ebx
    pop ebp
%endmacro

%macro copyArgs 2 
    mov ebx, [ ebp + %2 ]
    shl ebx, 2               
    sub esp, ebx            
    cld
    mov ecx, dword [ ebp + %2 ]
    mov esi, dword [ ebp + %1 ]
    mov edi, esp
    rep movsd
%endmacro

; =========================================================================

extern prtInvokeUnmanagedFunc
extern prtGetTaskHandle
extern prtYieldUnmanaged
extern longjmp
extern free

common gc_heap_slot_write_barrier_indirect 4
common gc_heap_slot_write_interior_indirect 4

%ifndef NO_INLINE_WRITE_BARRIER
common g_tls_offset_bytes 4
common local_nursery_size 4
%endif

; =========================================================================

global pillar2cInvokeUnmanagedFunc
global pillar2cInvokeUnmanagedStart
global pillar2cInvokeUnmanagedDestructor

;global pillar2cInvokeUnmanagedFunc_0
;global pillar2cInvokeUnmanagedFunc_1
;global pillar2cInvokeUnmanagedFunc_2
;global pillar2cInvokeUnmanagedFunc_3
;global pillar2cInvokeUnmanagedFunc_4
;global pillar2cInvokeUnmanagedFunc_5
;global pillar2cInvokeUnmanagedFunc_6
;global pillar2cInvokeUnmanagedFunc_7
;global pillar2cInvokeUnmanagedFunc_8
;global pillar2cInvokeUnmanagedFunc_9
;global pillar2cInvokeUnmanagedFunc_10
;global pillar2cInvokeUnmanagedFunc_11

%define _taskHandle$ REGISTER_SIZE + REGISTER_SIZE
%define _prevPseudoFrame$ _taskHandle$ + REGISTER_SIZE
%define _unmanagedFunc$ _prevPseudoFrame$ + REGISTER_SIZE
%define _argStart$ _unmanagedFunc$ + REGISTER_SIZE
%define _argSize$ _argStart$ + REGISTER_SIZE
%define _callingConvention$ _argSize$ + REGISTER_SIZE

;pillar2cInvokeUnmanagedFunc_0:
;pillar2cInvokeUnmanagedFunc_1:
;pillar2cInvokeUnmanagedFunc_2:
;pillar2cInvokeUnmanagedFunc_3:
;pillar2cInvokeUnmanagedFunc_4:
;pillar2cInvokeUnmanagedFunc_5:
;pillar2cInvokeUnmanagedFunc_6:
;pillar2cInvokeUnmanagedFunc_7:
;pillar2cInvokeUnmanagedFunc_8:
;pillar2cInvokeUnmanagedFunc_9:
;pillar2cInvokeUnmanagedFunc_10:
;pillar2cInvokeUnmanagedFunc_11:
pillar2cInvokeUnmanagedFunc:
pillar2cInvokeUnmanagedStart:
    push ebx

    ; // task handle into ebx
    mov  ebx, dword [esp+8]

    ; // call Pillar runtime function
    push dword [esp+28]
    push dword [esp+28]
    push dword [esp+28]
    push dword [esp+28]
    call prtInvokeUnmanagedFunc

    pop  ebx
    ret  24
pillar2cInvokeUnmanagedDestructor:

; =========================================================================

global pillar2cYield
global pillar2cYieldStart
global pillar2cYieldDestructor

%define _taskHandle$ REGISTER_SIZE + REGISTER_SIZE
%define _prevPseudoFrame$ _taskHandle$ + REGISTER_SIZE

pillar2cYield:
pillar2cYieldStart:
    push ebx

    ; // task handle into ebx
    mov  ebx, dword [esp+8]

    ; // call Pillar runtime function
    push 0
    push 0
    push 0
    push prtYieldUnmanaged
    call prtInvokeUnmanagedFunc

    pop  ebx
    ret  8
pillar2cYieldDestructor:

; =========================================================================

global pillar2c_pcall_target
global pillar2c_pcall_target_start
global pillar2c_pcall_target_end

%define _managedFunc$ REGISTER_SIZE + REGISTER_SIZE
%define _argStart$ _managedFunc$ + REGISTER_SIZE
%define _argSize$ _argStart$ + REGISTER_SIZE

pillar2c_pcall_target:
pillar2c_pcall_target_start:
    fullStubProlog
    
    copyArgs _argStart$ , _argSize$
	push dword [ebp + _argStart$]
	call free
	add  esp, 4
    push 0                                   ; // a NULL to root the pseudo-frame stack
    call prtGetTaskHandle
    push eax                      
	           ; // task handle is the first arg to all managed methods
    call dword [ebp + _managedFunc$]         ; // managedFunc should remove all the args
    
    fullStubEpilog
    ret  12
pillar2c_pcall_target_end:

; =========================================================================

global _pillar2c_continuation_target

_pillar2c_continuation_target:
    add  edx, 8
    push 1
    push edx
    call longjmp

; =========================================================================

%else ; // __X86_64__

; =========================================================================

%define REGISTER_SIZE 8

; =========================================================================

extern prtInvokeUnmanagedFunc
extern prtGetTaskHandle
extern prtYieldUnmanaged
extern longjmp
extern free

common gc_heap_slot_write_barrier_indirect 4
common gc_heap_slot_write_interior_indirect 4

%ifndef NO_INLINE_WRITE_BARRIER
common g_tls_offset_bytes 4
common local_nursery_size 4
%endif

; =========================================================================

global pillar2cInvokeUnmanagedFunc
global pillar2cInvokeUnmanagedStart
global pillar2cInvokeUnmanagedDestructor

global pillar2cInvokeUnmanagedFunc_0
global pillar2cInvokeUnmanagedFunc_1
global pillar2cInvokeUnmanagedFunc_2
global pillar2cInvokeUnmanagedFunc_3
global pillar2cInvokeUnmanagedFunc_4
global pillar2cInvokeUnmanagedFunc_5
global pillar2cInvokeUnmanagedFunc_6
global pillar2cInvokeUnmanagedFunc_7
global pillar2cInvokeUnmanagedFunc_8
global pillar2cInvokeUnmanagedFunc_9
global pillar2cInvokeUnmanagedFunc_10
global pillar2cInvokeUnmanagedFunc_11

%define _taskHandle$ rdi
%define _prevPseudoFrame$ rsi
%define _unmanagedFunc$ rdx
%define _argStart$ rcx
%define _argSize$  r8
%define _callingConvention$ r9

pillar2cInvokeUnmanagedFunc_0:
pillar2cInvokeUnmanagedFunc_1:
pillar2cInvokeUnmanagedFunc_2:
pillar2cInvokeUnmanagedFunc_3:
pillar2cInvokeUnmanagedFunc_4:
pillar2cInvokeUnmanagedFunc_5:
pillar2cInvokeUnmanagedFunc_6:
pillar2cInvokeUnmanagedFunc_7:
pillar2cInvokeUnmanagedFunc_8:
pillar2cInvokeUnmanagedFunc_9:
pillar2cInvokeUnmanagedFunc_10:
pillar2cInvokeUnmanagedFunc_11:
pillar2cInvokeUnmanagedFunc:
pillar2cInvokeUnmanagedStart:
    push rbx
    push _prevPseudoFrame$
    sub  rsp, 8

    ; // task handle into rbx
    mov  rbx, _taskHandle$

    mov  rdi, _unmanagedFunc$
    mov  rsi, _argStart$
    mov  rdx, _argSize$
    mov  rcx, _callingConvention$
    call prtInvokeUnmanagedFunc

    add  rsp, 16
    pop  rbx
    ret
pillar2cInvokeUnmanagedDestructor:

; =========================================================================

global pillar2cYield
global pillar2cYieldStart
global pillar2cYieldDestructor

%define _taskHandleStack$ REGISTER_SIZE + REGISTER_SIZE
%define _prevPseudoFrameStack$ _taskHandleStack$ + REGISTER_SIZE
%define _taskHandle$ rcx
%define _prevPseudoFrame$ rdx

pillar2cYield:
pillar2cYieldStart:
    push rbx
    push _prevPseudoFrame$
    sub  rsp, 8

    ; // task handle into rbx
    mov  rbx, _taskHandle$

    mov  rdi, prtYieldUnmanaged
    mov  rsi, 0
    mov  rdx, 0
    mov  rcx, 0

    call prtInvokeUnmanagedFunc

    add  rsp, 16
    pop  rbx
    ret
pillar2cYieldDestructor:

; =========================================================================

global pillar2c_pcall_target
global pillar2c_pcall_target_start
global pillar2c_pcall_target_end

%define _managedFunc$ rdi
%define _argStart$ rsi
%define _argSize$ rdx

pillar2c_pcall_target:
pillar2c_pcall_target_start:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov  r12, _managedFunc$                  ; // save function to call in r15
    mov  r13, _argStart$
    mov  r14, _argSize$

    call prtGetTaskHandle
    mov  rbx, rax

    mov  r10, r12
    mov  r11, r13
    mov  r12, r14

    mov  r13, r12                               ;; // r13 = number of 8-byte params
    shl  r13, 3                                 ;; // r13 = size of params in bytes 
    mov  r14, rsp                               ;; // r14 = current stack pointer
    sub  r14, r13                               ;; // r14 = minimum required stack space
    mov  rax, 0FFffFFffFFffFFf0h                ;; // and then and'ing by 16 for alignment
    and  r14, rax                               ;; // and then and'ing by 16 for alignment

    mov  r13, rsp                               ;; // calculate how much space we actually need with alignment
    sub  r13, r14                               ;; // r13 = how much extra stack space we need

    sub  rsp, r13                               ;; // adjust rsp by that amount

    ;; // copy the stack arguments to the next stack location starting at esp.
    cld
    mov  rcx, r12                               ;; // arg size
    mov  rsi, r11                               ;; // arg start
    mov  rdi, rsp                               ;; // place to copy
    ;; // rep movsd copies "rcx" dwords from [rsi] to [rdi].  cld means rsi and rdi are incremented after each copy.
    rep  movsq

    ; // MOVE ARGS INTO REGISTERS?  BUT WE DON'T HAVE TYPE INFORMATION!!!
    mov  rdx, qword [rsp+0]
    movq xmm2, rdx

    mov  rcx, qword [rsp+8]
    movq xmm3, rcx

    mov  r8, qword [rsp+16]
    movq xmm4, r8

    mov  r9, qword [rsp+24]
    movq xmm5, r9

    mov  r11, r13                               ;; // r11 = space subtracted from stack  
    mov  r12, r13       ; 8, 16, 56, 64         ;; // r12 = space subtracted from stack
    mov  r15, r13       ; 8, 16, 56, 64         ;; // r15 = space subtracted from stack
    and  r13, rax       ; 0, 16, 48, 64         ;; // r13 = space subtracted on 16-byte align
    not  rax                                    ;; // invert mask to get the remainder
    and  r15, rax       ; 8,  0,  8,  0         ;; // r15 = 0 if stack space was 16-byte aligned, 8 otherwise
    sub  r13, 32        ; -,  -,  0, 16         ;; // see if all arguments can be passed in regs (4 regs * 8 bytes = 32)
    mov  r14, r13                               ;; // r14 = amount of stack space used greater than max reg args
    sar  r14, 63                                ;; // make r14 either all 'F' or all '0' depending on sign of r13
    not  r14            ; 0,  0,  1,  1         ;; // r14 = 0 if r13 is negative, all '1' otherwise
    and  r13, r14       ; 0,  0,  0, 16         ;; // r13 = 0 if registers enough for args, else amount of args on stack
    add  r13, r15       ; 8,  0,  8, 16         ;; // r13 = amount of stack to keep adjusted for alignment
    sub  r12, r13       ; 0, 16, 48, 48         ;; // r12 = amount to add to rsp to "pop" the args that go in regs
    add  rsp, r12                               ;; // pop

    sub  r11, r12                               ;; // r11 = amount we need to add to rsp after the call is complete
    mov  r12, r11                               ;; // r12 = save this amount in the callee-saved register r12

    mov  rsi, 0                                 ; // previous pseudo frame is NULL
    mov  rdi, rbx                               ; // task handle is first param
    call r10                                    ; // managedFunc should remove all the args

    add  rsp, r12

    pop  r15    
    pop  r14
    pop  r13    
    pop  r12    
    pop  rbx    
    pop  rbp    
    ret
pillar2c_pcall_target_end:

; =========================================================================

global _pillar2c_continuation_target

_pillar2c_continuation_target:
    mov  r10, rdx                               ; // the continuation is passed in rdx
    add  r10, 2*REGISTER_SIZE                   ; // r10 now points to the jmp_buf (3rd part) of the continuation
    mov  rax, 0FFffFFffFFffFFf0h                ; // align the stack
    and  rsp, rax                               ; // align the stack
    mov  rsi, 1                                 ; // value (2nd arg) to longjmp
    mov  rdi, r10                               ; // jmp_buf (1st arg) to longjmp
    call longjmp                                ; // call longjmp

; =========================================================================

global _pillar2c_get_next_rip_addr

_pillar2c_get_next_rip_addr:
    mov  rax, rsp
    ret

%endif ; // __X86_64__
