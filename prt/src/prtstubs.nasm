;;; COPYRIGHT_NOTICE_1
; $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtstubs.nasm,v 1.8 2012/03/16 21:15:42 taanders Exp $

%ifndef __x86_64__

SECTION .data

junk1:	dd 0
junk2:  dd 0


SECTION .text

; =============================================================================
; Declare external functions as near ones for calls below.
extern prt_ExitThread
extern printf
extern exit
extern prtYieldUnmanaged
extern prtFatCutTo
extern prt_PrintIllegalCutMessage
extern prt_ValidateVsh
extern pcallOnSystemStack
extern prt_needsToYield
extern prt_validateTlsPointer
extern prtYieldUntil
extern prtToUnmanaged
extern prtToManaged
extern prt_GetTaskNonInline
extern registerBootstrapTaskCim
extern prtPillarCompilerUnwinder

%define REGISTER_SIZE 4

common prt_Globals 4
common prtMinFreeStackSpace 4

%else ; // __x86_64__

SECTION .data

;junk1:	dq 0
;junk2:  dq 0


SECTION .text

; =============================================================================
; Declare external functions as near ones for calls below.
extern prt_ExitThread
extern printf
extern exit
extern prtYieldUnmanaged
extern prtFatCutTo
extern prt_PrintIllegalCutMessage
extern prt_ValidateVsh
extern pcallOnSystemStack
extern prt_needsToYield
extern prt_validateTlsPointer
extern prtYieldUntil
extern prtToUnmanaged
extern prtToManaged
extern prt_GetTaskNonInline
extern registerBootstrapTaskCim
extern prtPillarCompilerUnwinder

%define REGISTER_SIZE 8

common prt_Globals 8
common prtMinFreeStackSpace 4

%endif ; // __x86_64__


%define MIN_UNMANAGED_STACK 500000


; ==============================================================================
; These constants are verified in the validateStubConstants function
%ifdef TLS0
%define PRT_TASK_USER_TLS 0                     ; mUserTls
%define PRT_TASK_LAST_VSE REGISTER_SIZE         ; mLastVse
%else
%define PRT_TASK_USER_TLS REGISTER_SIZE         ; mUserTls
%define PRT_TASK_LAST_VSE 0                     ; mLastVse
%endif
%define PRT_TASK_THREAD_HANDLE 2*REGISTER_SIZE  ; mThreadHandle
%define PRT_VSE_ENTRY_TYPE_CODE 0               ; entryTypeCode
%define PRT_VSE_NEXT_FRAME REGISTER_SIZE        ; nextFrame
%define PRT_VSE_TARGET_CONTINUATION 2*REGISTER_SIZE ; targetContinuation
%define CONTINUATION_EIP_OFFSET 0

%ifndef __x86_64__

; ==============================================================================
;  COMMON CODE SEQUENCES
; ==============================================================================

;TLS_REGISTER = "ebx"

; Check for a valid TLS_REGISTER setting.
%ifdef TLS_REGISTER
%if TLS_REGISTER == "ebx"
;ECHO Using ebx as TLS register.
%define tlsreg ebx
%define savereg1 esi
%define savereg2 edi
%macro restoreTlsRegister 0
        mov tlsreg, [ ebp + _savedEbx$ ]
%endmacro
%elif TLS_REGISTER == "edi"
;ECHO Using edi as TLS register.
%define tlsreg edi
%define savereg1 esi
%define savereg2 ebx
%macro restoreTlsRegister 0
        mov tlsreg, [ ebp + savedEdi$ ]
%endmacro
%else
;ECHO Error: TLS_REGISTER should be set to "ebx" or "edi", or left unset.
.ERR
%endif
%else
%define savereg1 esi
%define savereg2 edi
%macro restoreTlsRegister 0
%endmacro
%endif

; // Like getTlsIntoEax below but always refreshes tlsreg from the current thread's TLS.
%macro loadTlsRegister 0
    call dword prt_GetTaskNonInline
    mov  tlsreg, eax
%endmacro

; // Returns the current thread's TLS (its PrtTask pointer) in eax.  Leaves other registers unchanged.
%macro getTlsIntoEax 0
%ifdef TLS_REGISTER
    mov  eax, tlsreg
%else
    call dword prt_GetTaskNonInline
%endif
%endmacro

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

; // Copies a function's arguments to its frame. Also reserves space in the frame for the copied arguments.
; // Leaves eax and edx untouched.
; // Modifies ebx, ecx, esi, edi.
; // Upon exit, ebx will contain the number of bytes of arguments.
;%macro copyArgs 2 ; argStartOffsetFromEbp, argSizeOffsetFromEbp
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

 ; // Restore the original frame's ebp and esp values using edx, which points to a VSE embedded in the frame.
%macro continuationProlog 2 ; espOffsetFromEbp, contOffsetFromEbp
    lea esp, [edx - %2 + %1]
    lea ebp, [edx - %2]
%endmacro

; // Pushes a VSE.  Assumes Prt_Task pointer is in eax.  Leaves eax and edx intact.
%macro pushVse 2 ; VseOffsetFromEbp, VseType
    mov ecx, [eax+PRT_TASK_LAST_VSE]
    mov dword [ebp+%1+PRT_VSE_ENTRY_TYPE_CODE], %2
    mov dword [ebp+%1+PRT_VSE_NEXT_FRAME], ecx
    mov dword [ebp+%1+PRT_VSE_TARGET_CONTINUATION], 0
    lea ecx, [ebp+%1]
    mov [eax+PRT_TASK_LAST_VSE], ecx
%endmacro

; // Pops a VSE.  Assumes Prt_Task pointer is in eax.  Leaves eax and edx intact.
%macro popVse 2 ; VseOffsetFromEbp, VseType
%ifdef DEBUG_VSE_POPS
    push eax
    push edx
    push %2
    lea ecx, [ebp+%1]
    push ecx
    call dword prt_ValidateVsh
    add esp, 8
    pop edx
    pop eax
%endif ; // DEBUG_VSE_POPS
    mov ecx, [eax+PRT_TASK_LAST_VSE]
    mov ecx, [ecx+PRT_VSE_NEXT_FRAME]
    mov [eax+PRT_TASK_LAST_VSE], ecx
%endmacro

; ==============================================================================

global prtWatermarkPrototype
global prt_WatermarkPrototypeStart
global prt_WatermarkPostTopIndex1
global prt_WatermarkPostTopIndex2
global prt_WatermarkPostStubStackStart
global prt_WatermarkPostStubStart
global prt_WatermarkPostRealEipMove
global prt_WatermarkPrototypeEnd

prtWatermarkPrototype:
prt_WatermarkPrototypeStart:
    mov ecx, dword [junk1]                       ; // ecx = current top index
prt_WatermarkPostTopIndex1:
    inc ecx                                      ; // ecx = new top index
    mov dword [junk1], ecx                       ; // increment the stack's top
prt_WatermarkPostTopIndex2:
    lea ecx, [junk2+ecx*REGISTER_SIZE]           ; // ecx = address of the top free stub stack array entry
prt_WatermarkPostStubStackStart:
    mov dword [ecx], prt_WatermarkPrototypeStart ; // write the stub start value into the stub stack array
prt_WatermarkPostStubStart:
    mov ecx, exit
prt_WatermarkPostRealEipMove:
    jmp ecx                                      ; // any jump target will do...will be replaced in each copy of the stub
prt_WatermarkPrototypeEnd:

; ==============================================================================

; // void __pdecl prtYield(void);


global prtYield
global prt_YieldStart
global prt_YieldEnd

prtYield:
prt_YieldStart:
    ret
prt_YieldEnd:


; ==============================================================================

; // void __stdcall prtInvokeManagedFunc(PrtCodeAddress managedFunc, void *argStart, unsigned argSize);


; // Export the different locations within the prtInvokeManagedFunc function.
global prtInvokeManagedFunc          ; start of the function
global prtInvokeManagedIntRet          ; start of the function
global prt_InvokeManagedFuncStart       ; start of the function
global prt_InvokeManagedFuncUnwindContinuation
global prt_InvokeManagedFuncEnd         ; end of the function

; // Stack offsets of this function's aguments.
%define _managedFunc$ REGISTER_SIZE + REGISTER_SIZE
%define _argStart$ _managedFunc$ + REGISTER_SIZE
%define _argSize$ _argStart$ + REGISTER_SIZE

; // Stack frame layout:
;   ebp+0:  saved ebp
;   ebp-4:  saved ebx
;   ebp-8:  saved esi
;   ebp-12: saved edi
%define _savedEbx$ -REGISTER_SIZE
%define _savedEsi$ _savedEbx$ - REGISTER_SIZE
%define _savedEdi$ _savedEsi$ - REGISTER_SIZE
%define _contArgHigh32Bits$ _savedEdi$ - REGISTER_SIZE
%define _firstFrameLocal$ _savedEdi$ - REGISTER_SIZE ; intentionally same
%define _contArgLow32Bits$ _contArgHigh32Bits$ - REGISTER_SIZE
%define _contVsh$ _contArgLow32Bits$ - REGISTER_SIZE
%define _contEip$ _contVsh$ - REGISTER_SIZE
%define _contStart$ _contVsh$ - REGISTER_SIZE
%define _normalEsp$ _contVsh$ - REGISTER_SIZE

%define U2MFRAMESIZE (((_firstFrameLocal$) - (_normalEsp$)) + (REGISTER_SIZE))

prtInvokeManagedIntRet:
prtInvokeManagedFunc:
prt_InvokeManagedFuncStart:
    ; // A stack limit check is unnecessary because we must already be running
    ; // on a full-sized stack for unmanaged code.
    fullStubProlog    ; // basic ebp-based frame prolog that saves ebx, esi, edi
    sub esp , U2MFRAMESIZE
    copyArgs _argStart$ , _argSize$      ; // reserve space and copy arguments
%ifdef TLS_REGISTER
    loadTlsRegister
%endif

    call dword [ ebp + _managedFunc$ ]   ; // call the managed function. don't disturb the return registers after this

prt_InvokeManagedFuncAfterCall:
    add esp, U2MFRAMESIZE
    fullStubEpilog
    ret 12

prt_InvokeManagedFuncUnwindContinuation:
    continuationProlog _normalEsp$, _contStart$
    mov eax, dword [ ebp + _contArgLow32Bits$ ]
    mov edx, dword [ ebp + _contArgHigh32Bits$ ]
    jmp prt_InvokeManagedFuncAfterCall
prt_InvokeManagedFuncEnd:


; ==============================================================================

global prtInvokeUnmanagedFunc                ; start of the function
global prtInvokeUnmanagedIntRet              ; start of the function
global prt_InvokeUnmanagedFuncStart          ; start of the function
global prt_InvokeUnmanagedFuncPostCall       ;
global prt_InvokeUnmanagedFuncDestructor     ; VSE type identifier (code address)
global prt_InvokeUnmanagedFuncUnwindContinuation
global prt_InvokeUnmanagedFuncEnd            ; end of the function

; // Stack offsets of this function's aguments.
%define _unmanagedFunc$ REGISTER_SIZE + REGISTER_SIZE
%define _argStart$ _unmanagedFunc$ + REGISTER_SIZE
%define _argSize$ _argStart$ + REGISTER_SIZE
%define _callingConvention$ _argSize$ + REGISTER_SIZE

; // Keep these offsets up to date with the definition of struct Prt_M2uFrame.
; Stack frame layout:
;   ebp+0:  saved ebp
;   ebp-4:  saved ebx
;   ebp-8:  saved esi
;   ebp-12: saved edi
%define _savedEbx$ -REGISTER_SIZE
%define _savedEsi$ _savedEbx$ - REGISTER_SIZE
%define _savedEdi$ _savedEsi$ - REGISTER_SIZE
%define _contArgHigh32Bits$ _savedEdi$ - REGISTER_SIZE
%define _firstFrameLocal$ _savedEdi$ - REGISTER_SIZE
%define _contArgLow32Bits$ _contArgHigh32Bits$ - REGISTER_SIZE
%define _contVsh$ _contArgLow32Bits$ - REGISTER_SIZE
%define _contEip$ _contVsh$ - REGISTER_SIZE
%define _contStart$ _contVsh$ - REGISTER_SIZE
%define _realM2uUnwinder$ _contStart$ - REGISTER_SIZE
;; end of core VSE fields
%define _targetContinuation$ _realM2uUnwinder$ - REGISTER_SIZE
%define _nextVsePtr$ _targetContinuation$ - REGISTER_SIZE
%define _frameType$ _nextVsePtr$ - REGISTER_SIZE
%define _vsePtr$ _nextVsePtr$ - REGISTER_SIZE
%define _normalEsp$ _nextVsePtr$ - REGISTER_SIZE
;; start of VSE

%define M2UFRAMESIZE (((_firstFrameLocal$) - (_normalEsp$)) + (REGISTER_SIZE))

prtInvokeUnmanagedIntRet:
prtInvokeUnmanagedFunc:
prt_InvokeUnmanagedFuncStart:
    getTlsIntoEax                                   ; // leaves eax set to TLS, edx set to mcrtThread
    fullStubProlog                                  ; // basic ebp-based frame prolog that saves ebx, esi, edi
    sub  esp, M2UFRAMESIZE                          ; // reserve space for rest of the frame and local vars.
    mov dword [ ebp + _realM2uUnwinder$ ], prtPillarCompilerUnwinder
    pushVse _vsePtr$, prt_InvokeUnmanagedFuncDestructor

    push eax
    call dword prtToUnmanaged
    pop  eax

    ;mov dword [ebp+_callsite_id$], 0            ; // 0 in contArgLow32Bits means we're at the unmanaged call site

    copyArgs _argStart$ , _argSize$                  ; // reserve space and copy arguments, sets ebx = number of arg bytes
    call dword [ ebp + _unmanagedFunc$ ]
prt_InvokeUnmanagedFuncPostCall:
    mov ecx, [ ebp + _callingConvention$ ]               ; // ecx != 0 if target is a stdcall
    cmp ecx, 0
    jnz end_cc_check
    add esp, ebx
end_cc_check:

prt_InvokeUnmanagedFuncAfterCall:
    restoreTlsRegister
    mov savereg1, eax                                    ; // save possible return registers away
    mov savereg2, edx                                    ; // save possible return registers away
    ; // we use targetContinuation field here because we no longer need it
    ; // for fat cuts.  We use it to indicate we are at the prtYieldUnmanaged callsite
    ; // rather than the call to the unmanaged function.
    ; mov dword [ebp+_callsite_id$], 1
    ; call prtYieldUnmanaged

    getTlsIntoEax                                        ; // get current Prt_Task pointer

    push eax
    call dword prtToManaged
    pop  eax

    popVse _vsePtr$, prt_InvokeUnmanagedFuncDestructor

    mov eax, savereg1                                    ; // restore return registers
    mov edx, savereg2
    add esp, M2UFRAMESIZE                                ; // remove the rest of the frame and local vars

    fullStubEpilog
    ret 16

prt_InvokeUnmanagedFuncDestructor:
    continuationProlog _normalEsp$, _vsePtr$
    push dword [ ebp + _targetContinuation$ ]                  ; // recut
    call dword prtFatCutTo

prt_InvokeUnmanagedFuncUnwindContinuation:
    continuationProlog _normalEsp$, _contStart$
    mov eax, [ebp+_contArgLow32Bits$]
    mov edx, [ebp+_contArgHigh32Bits$]
    jmp prt_InvokeUnmanagedFuncAfterCall

prt_InvokeUnmanagedFuncEnd:

; ==============================================================================

global prt_PcallDestructor

prt_PcallDestructor:
    mov  esp, edx
%ifdef TLS_REGISTER
    loadTlsRegister
%endif
    push 0
    push 0
    push 0
    push prt_PrintIllegalCutMessage
    call dword prtInvokeUnmanagedFunc
    call dword prt_ExitThread

; ==============================================================================

%macro setNextConstant 2
    mov eax, [esp + %2 ]
    mov dword [eax], %1
%endmacro

global prt_getStubConstants                ; start of the function

prt_getStubConstants:
    setNextConstant PRT_TASK_LAST_VSE , REGISTER_SIZE*1
    setNextConstant PRT_TASK_USER_TLS , REGISTER_SIZE*2
    setNextConstant PRT_TASK_THREAD_HANDLE , REGISTER_SIZE*3
    setNextConstant PRT_VSE_ENTRY_TYPE_CODE , REGISTER_SIZE*4
    setNextConstant PRT_VSE_NEXT_FRAME , REGISTER_SIZE*5
    setNextConstant PRT_VSE_TARGET_CONTINUATION , REGISTER_SIZE*6
    setNextConstant CONTINUATION_EIP_OFFSET , REGISTER_SIZE*7
    ret

; ==============================================================================

global prt_getCurrentEsp

prt_getCurrentEsp:
    mov eax, esp
    ret

; ==============================================================================

global prtThinCutTo

%define _continuation$ 4

prtThinCutTo:
%ifdef TLS_REGISTER
    loadTlsRegister
%endif
    mov edx, [ esp + _continuation$ ]
    jmp dword [edx+CONTINUATION_EIP_OFFSET]

; ==============================================================================

global prtYieldUntilDestructor
prtYieldUntilDestructor:
    mov edx, [edx+8]                           ; // edx = target continuation
    push edx
    call dword prtFatCutTo

; ==============================================================================

global prt_bootstrapTaskAsm                    ; start of the function
global prt_bootstrapTaskAsmCall

; // Stack offsets of this function's aguments.
%define _cimCreated$ REGISTER_SIZE + REGISTER_SIZE
%define _funcToCall$ _cimCreated$ + REGISTER_SIZE
%define _argStart$ _funcToCall$ + REGISTER_SIZE
%define _argSize$ _argStart$ + REGISTER_SIZE

prt_bootstrapTaskAsm:
    fullStubProlog
prt_bootstrapTaskAsmStart:
    mov  esi, [ ebp + _cimCreated$ ]
    test esi, esi
    jne  cimAlreadyCreated
    mov  eax, prt_bootstrapTaskAsmEnd
;    lea  eax, prt_bootstrapTaskAsmEnd
    push eax
    mov  eax, prt_bootstrapTaskAsmStart
;    lea  eax, prt_bootstrapTaskAsmStart
    push eax
;    call [_registerBootstrapTaskCim]
    mov edx, registerBootstrapTaskCim
    call edx
;    call dword _registerBootstrapTaskCim
    add  esp, 8
cimAlreadyCreated:
    ;; // get all the needed vars into regs before we update esp in case this is
    ;; // not an ebp based frame.
    mov  edx, [ebp + _funcToCall$ ]

    copyArgs _argStart$ , _argSize$      ; // reserve space and copy arguments

%ifdef TLS_REGISTER
    push edx
    mov edx, prt_GetTaskNonInline
    call edx
    mov tlsreg, eax
;    loadTlsRegister
    pop edx
%endif
;    mov edx, edx
    call edx                              ; // invoke the function
;    call dword edx                              ; // invoke the function
prt_bootstrapTaskAsmCall:

    call dword prt_ExitThread                   ; // exit the thread
prt_bootstrapTaskAsmEnd:

; ==============================================================================


; // Stack offsets of this function's aguments.
%define _funcToCall$ REGISTER_SIZE + REGISTER_SIZE
%define _stackTop$ _funcToCall$ + REGISTER_SIZE

global prt_testStackSize                    ; start of the function
prt_testStackSize:
    fullStubProlog
%ifdef TLS_REGISTER
    loadTlsRegister
%endif

    mov  esi, esp
    mov  edx, [ebp + _funcToCall$]       ; // edx = the function to invoke
    mov  esp, [ebp + _stackTop$ ]        ; // transition to a new stack

    call edx                             ; // invoke the function

    mov  esp, esi
    fullStubEpilog
    ret

; ==============================================================================

global prt_pcallAsm                       ; start of the function

; // Stack offsets of this function's aguments.
%define _newEsp$ REGISTER_SIZE + REGISTER_SIZE

prt_pcallAsm:
    fullStubProlog
    mov esi, esp
    mov esp, [ebp + _newEsp$ ]
    call dword pcallOnSystemStack
    mov esp, esi
    fullStubEpilog
    ret

; ==============================================================================

; // void * __pdecl prt_getTlsRegister(void);
global prt_getTlsRegister
global prt_getTlsRegisterStart
global prt_getTlsRegisterEnd

prt_getTlsRegister:
prt_getTlsRegisterStart:
%ifdef TLS_REGISTER
    mov eax, tlsreg
%else
    mov eax, 0
%endif
    ret
prt_getTlsRegisterEnd:


; ==============================================================================
; =                      __x86_64__                                            =
; ==============================================================================


%else ; // __x86_64__

; // Like getTlsIntoEax below but always refreshes tlsreg from the current thread's TLS.
%macro loadTlsRegister 0
    call prt_GetTaskNonInline
    mov  tlsreg, rax
%endmacro

%define ARG_REG1 rdi
%define ARG_REG2 rsi
%define ARG_REG3 rdx
%define ARG_REG4 rcx
%define ARG_REG5 r8
%define ARG_REG6 r9

; // Copies a function's arguments to its frame. Also reserves space in the frame for the copied arguments.
; // Leaves rax and rdx untouched.
; // At completion, rsi = size of args in bytes, rcx/rdx/r8/r9 are the args to the next function, rdi is trashed.
; // startReg and sizeReg should not be rbx, rcx, rsi or rdi.
; %macro copyArgs %1 = startReg, %2 = sizeReg
;%macro copyArgs 2
; r11 = startReg, r12 = sizeReg
%macro copyArgs 0
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
    mov  rdi, qword [rsp+0]
    movq xmm0, rdi

    mov  rsi, qword [rsp+8]
    movq xmm1, rsi

    mov  rdx, qword [rsp+16]
    movq xmm2, rdx

    mov  rcx, qword [rsp+24]
    movq xmm3, rcx

    mov  r8, qword [rsp+32]
    movq xmm4, r8

    mov  r9, qword [rsp+40]
    movq xmm5, r9

    mov  r11, r13                               ;; // r11 = space subtracted from stack
    mov  r12, r13       ; 8, 16, 56, 64         ;; // r12 = space subtracted from stack
    mov  r15, r13       ; 8, 16, 56, 64         ;; // r15 = space subtracted from stack
    and  r13, rax       ; 0, 16, 48, 64         ;; // r13 = space subtracted on 16-byte align
    not  rax                                    ;; // invert mask to get the remainder
    and  r15, rax       ; 8,  0,  8,  0         ;; // r15 = 0 if stack space was 16-byte aligned, 8 otherwise
    sub  r13, 48        ; -,  -,  0, 16         ;; // see if all arguments can be passed in regs (6 regs * 8 bytes = 48)
    mov  r14, r13                               ;; // r14 = amount of stack space used greater than max reg args
    sar  r14, 63                                ;; // make r14 either all 'F' or all '0' depending on sign of r13
    not  r14            ; 0,  0,  1,  1         ;; // r14 = 0 if r13 is negative, all '1' otherwise
    and  r13, r14       ; 0,  0,  0, 16         ;; // r13 = 0 if registers enough for args, else amount of args on stack
    add  r13, r15       ; 8,  0,  8, 16         ;; // r13 = amount of stack to keep adjusted for alignment
    sub  r12, r13       ; 0, 16, 48, 48         ;; // r12 = amount to add to rsp to "pop" the args that go in regs
    add  rsp, r12                               ;; // pop

    sub  r11, r12                               ;; // r11 = amount we need to add to rsp after the call is complete
    mov  r12, r11                               ;; // r12 = save this amount in the callee-saved register r12
%endmacro

; // Restore the original frame's ebp and esp values using edx, which points to a VSE embedded in the frame.
; %macro continuationProlog %1 = espOffsetFromEbp, %2 = vseOffsetFromEbp
%macro continuationProlog 2
    lea rsp, [rdx - %2 + %1]
    lea rbp, [rdx - %2]
%endmacro

; // Returns the current thread's TLS (its PrtTask pointer) in eax.  Leaves other registers unchanged.
%macro getTlsIntoEax 0
%ifdef TLS_REGISTER
    mov  rax, tlsreg
%else
    call dword prt_GetTaskNonInline
%endif
%endmacro

;TLS_REGISTER = "rbx"

; Check for a valid TLS_REGISTER setting.
%ifdef TLS_REGISTER
%if TLS_REGISTER == "rbx"
;ECHO Using rbx as TLS register.
%define tlsreg   rbx
%define savereg1 rsi
%define savereg2 rdi
%macro restoreTlsRegister 0
    mov tlsreg, [ rbp + _savedRbx$ ]
%endmacro
%else
;ECHO Error: TLS_REGISTER should be set to "ebx" or left unset.
.ERR
%endif
%else
%define savereg1 rsi
%define savereg2 rdi
%macro restoreTlsRegister 0
%endmacro
%endif

; =============================================================================================

global prtWatermarkPrototype
global prt_WatermarkPrototypeStart
global prt_WatermarkPostTopIndex1
global prt_WatermarkPostStubStackStart
global prt_WatermarkPostStubStart
global prt_WatermarkPostRealEipMove
global prt_WatermarkPrototypeEnd

prtWatermarkPrototype:
prt_WatermarkPrototypeStart:
    mov  rdx, 01F2f3F4f5F6f7F8fh
prt_WatermarkPostTopIndex1:
    mov  ecx, dword [rdx]               ; // ecx = current top index
    inc  ecx                            ; // ecx = new top index
    mov  dword [rdx], ecx               ; // increment the stack's top

    mov  rdx, 01F2f3F4f5F6f7F8fh
prt_WatermarkPostStubStackStart:
    lea  rcx, [rdx+rcx*REGISTER_SIZE]   ; // ecx = address of the top free stub stack array entry

    mov  rdx, 01F2f3F4f5F6f7F8fh
prt_WatermarkPostStubStart:
    mov  qword [rcx], rdx               ; // write the stub start value into the stub stack array
    mov  rcx, 01F2f3F4f5F6f7F8fh
prt_WatermarkPostRealEipMove:
    jmp  rcx                            ; // any jump target will do...will be replaced in each copy of the stub
prt_WatermarkPrototypeEnd:

; ==============================================================================

; // void __pdecl prtYield(void);

global prtYield
global prt_YieldStart
global prt_YieldEnd

prtYield:
prt_YieldStart:
    ret
prt_YieldEnd:

; ==============================================================================

; // void __stdcall prtInvokeManagedFunc(PrtCodeAddress managedFunc, void *argStart, unsigned argSize);


; // Export the different locations within the prtInvokeManagedFunc function.
global prtInvokeManagedFunc             ; start of the function
global prtInvokeManagedIntRet           ; start of the function
global prt_InvokeManagedFuncStart       ; start of the function
global prt_InvokeManagedFuncUnwindContinuation
global prt_InvokeManagedFuncEnd         ; end of the function

; // Stack offsets of this function's aguments.
%define _managedFunc$ REGISTER_SIZE + REGISTER_SIZE
%define _argStart$ _managedFunc$ + REGISTER_SIZE
%define _argSize$ _argStart$ + REGISTER_SIZE

; // Stack frame layout:
;   ebp+0:  saved ebp
;   ebp-4:  saved ebx
;   ebp-8:  saved esi
;   ebp-12: saved edi
%define _savedEbx$ -REGISTER_SIZE
%define _savedEsi$ _savedEbx$ - REGISTER_SIZE
%define _savedEdi$ _savedEsi$ - REGISTER_SIZE
%define _contArgHigh32Bits$ _savedEdi$ - REGISTER_SIZE
%define _firstFrameLocal$ _savedEdi$ - REGISTER_SIZE ; intentionally same
%define _contArgLow32Bits$ _contArgHigh32Bits$ - REGISTER_SIZE
%define _contVsh$ _contArgLow32Bits$ - REGISTER_SIZE
%define _contEip$ _contVsh$ - REGISTER_SIZE
%define _contStart$ _contVsh$ - REGISTER_SIZE
%define _normalEsp$ _contVsh$ - REGISTER_SIZE

%define U2MFRAMESIZE (((_firstFrameLocal$) - (_normalEsp$)) + (REGISTER_SIZE))

prtInvokeManagedIntRet:
prtInvokeManagedFunc:
prt_InvokeManagedFuncStart:
    ; // WE NEVER USE THIS SO IT IS COMPLETELY ATROPHIED!!!!!!!!!!!!!!!!!!!!!!

    ;fullStubProlog                    ; // basic ebp-based frame prolog that saves ebx, esi, edi
    mov  rax, ARG_REG1
    sub  rsp, U2MFRAMESIZE
    copyArgs ; // reserve space and copy arguments
    ;copyArgs ARG_REG2 , ARG_REG3      ; // reserve space and copy arguments

%ifdef TLS_REGISTER
    loadTlsRegister
%endif

    call rax                          ; // call the managed function. don't disturb the return registers after this

prt_InvokeManagedFuncAfterCall:
    add  rsp, rsi
    add  rsp, U2MFRAMESIZE
    ;fullStubEpilog
    ret

prt_InvokeManagedFuncUnwindContinuation:
    continuationProlog _normalEsp$, _contStart$
    mov  rax, qword [ rbp + _contArgLow32Bits$ ]
    mov  rdx, qword [ rbp + _contArgHigh32Bits$ ]
    jmp  prt_InvokeManagedFuncAfterCall
prt_InvokeManagedFuncEnd:


; ==============================================================================

; // void __stdcall prtInvokeUnmanagedFunc(PrtCodeAddress unmanagedFunc, void *argStart, unsigned argSize, PrtCallingConvention callingConvention);

global prtInvokeUnmanagedFunc                ; start of the function
global prtInvokeUnmanagedIntRet              ; start of the function
global prt_InvokeUnmanagedFuncStart          ; start of the function
global prt_InvokeUnmanagedFuncPostCall       ;
global prt_InvokeUnmanagedFuncDestructor     ; VSE type identifier (code address)
global prt_InvokeUnmanagedFuncUnwindContinuation
global prt_InvokeUnmanagedFuncEnd            ; end of the function

; // Stack offsets of this function's aguments.
;%define _unmanagedFunc$ REGISTER_SIZE + REGISTER_SIZE
;%define _argStart$ _unmanagedFunc$ + REGISTER_SIZE
;%define _argSize$ _argStart$ + REGISTER_SIZE
;%define _callingConvention$ _argSize$ + REGISTER_SIZE

; // Keep these offsets up to date with the definition of struct Prt_M2uFrame.
; Stack frame layout:
;   ebp+0:  saved rbp
;   ebp-8:  saved rbx
;   ebp-16: saved r12
;   ebp-24: saved r13
;   ebp-32: saved r14
;   ebp-40: saved r15
%define _savedRbx$           -REGISTER_SIZE
%define _savedR12$           _savedRbx$ - REGISTER_SIZE
%define _savedR13$           _savedR12$ - REGISTER_SIZE
%define _savedR14$           _savedR13$ - REGISTER_SIZE
%define _savedR15$           _savedR14$ - REGISTER_SIZE
%define _contArg$            _savedR15$ - REGISTER_SIZE
%define _firstFrameLocal$    _savedR15$ - REGISTER_SIZE
%define _contVsh$            _contArg$  - REGISTER_SIZE
%define _contEip$            _contVsh$  - REGISTER_SIZE
%define _contStart$          _contVsh$  - REGISTER_SIZE
%define _realM2uUnwinder$    _contStart$- REGISTER_SIZE
;; end of core VSE fields
%define _targetContinuation$ _realM2uUnwinder$ - REGISTER_SIZE
%define _nextVsePtr$         _targetContinuation$ - REGISTER_SIZE
%define _frameType$          _nextVsePtr$ - REGISTER_SIZE
%define _vsePtr$             _nextVsePtr$ - REGISTER_SIZE
%define _normalEsp$          _nextVsePtr$ - REGISTER_SIZE
;; start of VSE

;%define M2UFRAMESIZE (((_firstFrameLocal$) - (_normalEsp$)) + (REGISTER_SIZE))
;%define ODD_ARG_MOD ((6*REGISTER_SIZE + M2UFRAMESIZE) % 16)
;%define EVEN_ARG_MOD ((5*REGISTER_SIZE + M2UFRAMESIZE) % 16)

prtInvokeUnmanagedIntRet:
prtInvokeUnmanagedFunc:
prt_InvokeUnmanagedFuncStart:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    sub  rsp, 7*REGISTER_SIZE                        ; // make space for M2U frame (6 registers) and align rsp

    mov  r14, ARG_REG3                               ; // save args in callee-saves
    mov  r13, ARG_REG2
    mov  r12, ARG_REG1

    getTlsIntoEax

    mov  qword [rbp + _realM2uUnwinder$], prtPillarCompilerUnwinder
;    pushVse _vsePtr$ , prt_InvokeUnmanagedFuncDestructor ; // doesn't modify input args

;    mov r10, %2     ; // VseType
    mov qword [rbp + _frameType$], prt_InvokeUnmanagedFuncDestructor
    mov r10, [rax+PRT_TASK_LAST_VSE]                 ; // get last VSE from Prt_Task into r10
    mov qword [rbp + _nextVsePtr$], r10              ; // link up this VSE with the previous one
    mov qword [rbp + _targetContinuation$], 0        ; // initialize targetContinuation to 0
    lea r10, [rbp + _vsePtr$]                        ; // get address of new VSE in r10
    mov [rax+PRT_TASK_LAST_VSE], r10                 ; // set last VSE in Prt_Task to the new VSE

    mov  ARG_REG1, rax                               ; // rcx = Tls sub  rsp, ODD_ARG_MOD
    call prtToUnmanaged

    mov  r10, r12
    mov  r11, r13
    mov  r12, r14

    copyArgs                                         ; // reserve space and copy arguments, rsi = size of args
    call r10
    add  rsp, r12

prt_InvokeUnmanagedFuncPostCall:
prt_InvokeUnmanagedFuncAfterCall:

    mov  r12, rax                                    ; // save result away in r12

    restoreTlsRegister

    getTlsIntoEax

    mov  ARG_REG1, rax
    call prtToManaged

    getTlsIntoEax

;    popVse _vsePtr$, prt_InvokeUnmanagedFuncDestructor
    mov  rcx, [rax+PRT_TASK_LAST_VSE]
    mov  rcx, [rcx+PRT_VSE_NEXT_FRAME]
    mov  [rax+PRT_TASK_LAST_VSE], rcx

    mov  rax, r12                                    ; // restore return value
    add  rsp, 7*REGISTER_SIZE                        ; // remove the rest of the frame and local vars

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

prt_InvokeUnmanagedFuncDestructor:
    continuationProlog _normalEsp$, _vsePtr$
    mov  ARG_REG1, [rbp+_targetContinuation$]        ; // recut
    sub  rsp, 32
    call prtFatCutTo

prt_InvokeUnmanagedFuncUnwindContinuation:
    continuationProlog _normalEsp$, _contStart$
    mov rax, [rbp+_contArgLow32Bits$]
    mov rdx, [rbp+_contArgHigh32Bits$]
    jmp prt_InvokeUnmanagedFuncAfterCall

prt_InvokeUnmanagedFuncEnd:

; =============================================================================================

global prt_PcallDestructor

prt_PcallDestructor:
    mov  rsp, rdx

    mov  rax, rsp
    and  rax, 0Fh
    sub  rsp, rax                           ; // align rsp

%ifdef TLS_REGISTER
    loadTlsRegister
%endif

    mov  ARG_REG4, 0
    mov  ARG_REG3, 0
    mov  ARG_REG2, 0
    mov  ARG_REG1, prt_PrintIllegalCutMessage
    call prtInvokeUnmanagedFunc
    call prt_ExitThread

; =============================================================================================

global prt_getStubConstants                ; start of the function

prt_getStubConstants:
    mov qword [rdi], PRT_TASK_LAST_VSE
    mov qword [rsi], PRT_TASK_USER_TLS
    mov qword [rdx], PRT_TASK_THREAD_HANDLE
    mov qword [rcx], PRT_VSE_ENTRY_TYPE_CODE
    mov qword [r8],  PRT_VSE_NEXT_FRAME
    mov qword [r9],  PRT_VSE_TARGET_CONTINUATION
    mov rax,  [rsp + REGISTER_SIZE]
    mov qword [rax], CONTINUATION_EIP_OFFSET
    ret

; =============================================================================================

global prt_getCurrentEsp

prt_getCurrentEsp:
    mov rax, rsp
    ret

; =============================================================================================

global prtThinCutTo

prtThinCutTo:
    mov rdx, ARG_REG1
    mov rax, [rdx+CONTINUATION_EIP_OFFSET]
    jmp rax

; =============================================================================================

global prt_bootstrapTaskAsm                    ; start of the function
global prt_bootstrapTaskAsmCall
global prt_bootstrapTaskAsmStart
global prt_bootstrapTaskAsmEnd

; // Stack offsets of this function's aguments.
%define _cimCreated$ REGISTER_SIZE + REGISTER_SIZE
%define _funcToCall$ _cimCreated$ + REGISTER_SIZE
%define _argStart$ _funcToCall$ + REGISTER_SIZE
%define _argSize$ _argStart$ + REGISTER_SIZE

prt_bootstrapTaskAsm:
prt_bootstrapTaskAsmStart:
    ; // This function never returns so don't bother saving the callee-save registers
    ; // even though we use them.
    push rcx  ; // argSize
    push rdx  ; // argStart
    push rsi  ; // function to call, by pushing 3 registers we align rsp

    test rdi, rdi
    jne  cimAlreadyCreated

    mov  rsi, prt_bootstrapTaskAsmEnd
    mov  rdi, prt_bootstrapTaskAsmStart
    call registerBootstrapTaskCim

cimAlreadyCreated:

%ifdef TLS_REGISTER
    loadTlsRegister
%endif

    pop  r10  ; //  function to call
    pop  r11  ; //  argStart
    pop  r12  ; //  argSize

    copyArgs  ; // r10, r11

    call r10
prt_bootstrapTaskAsmCall:
    add  rsp, r12
    call prt_ExitThread                   ;; // exit the thread
prt_bootstrapTaskAsmEnd:

; =============================================================================================

global prt_pcallAsm                       ; start of the function

prt_pcallAsm:
    push r12

    mov  r12, rsp
    mov  rsp, ARG_REG1

    mov  ARG_REG1, [rsp+0]
    mov  ARG_REG2, [rsp+1*REGISTER_SIZE]
    mov  ARG_REG3, [rsp+2*REGISTER_SIZE]
    mov  ARG_REG4, [rsp+3*REGISTER_SIZE]

    mov  rax, rsp
    and  rax, 0Fh
    sub  rsp, rax                         ; // align rsp

    call pcallOnSystemStack

    mov  rsp, r12

    pop  r12
    ret

; =============================================================================================

global prt_testStackSize                    ; start of the function

; // Stack offsets of this function's aguments.
%define _funcToCall$ REGISTER_SIZE + REGISTER_SIZE
%define _stackTop$ _funcToCall$ + REGISTER_SIZE

prt_testStackSize:
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r13
    push r15

    mov  r12, ARG_REG1
    mov  r13, ARG_REG2

%ifdef TLS_REGISTER
    loadTlsRegister
%endif

    mov  ARG_REG1, r12
    mov  ARG_REG2, r13

    mov  r12, rsp
    mov  rsp, ARG_REG2                    ;; // transition to a new stack

    sub  rsp, 40
    call ARG_REG1                         ;; // invoke the function
    add  rsp, 40

    mov  rsp, r12

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret

; =============================================================================================


; // void * __pdecl prt_getTlsRegister(void);
global prt_getTlsRegister
global prt_getTlsRegisterStart
global prt_getTlsRegisterEnd

prt_getTlsRegister:
prt_getTlsRegisterStart:
%ifdef TLS_REGISTER
    mov rax, tlsreg
%else
    mov rax, 0
%endif
    ret
prt_getTlsRegisterEnd:

; =============================================================================================

global prtYieldUntilDestructor
prtYieldUntilDestructor:
    mov  rcx, [rdx+8]                           ; // edx = target continuation
    push rcx
    call dword prtFatCutTo

; ==============================================================================


%endif ; //__x86_64__
