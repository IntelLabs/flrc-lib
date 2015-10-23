;;; COPYRIGHT_NOTICE_1

; $Header: /nfs/sc/proj/ctg/psl002/CVS/pillar_pthread/src/base/prtstubs.asm,v 1.10 2013/02/15 21:20:15 taanders Exp $

IFNDEF __X86_64__
.586
.xmm
.model flat, c

dseg segment para public 'data'
junk1 dword ?
junk2 dword ?
dseg ends

.code

; =============================================================================================
; Declare external functions as near ones for calls below.
EXTERN C prt_ExitThread : NEAR  
EXTERN C printf : NEAR
EXTERN C exit : NEAR
EXTERN C prtYieldUnmanaged : NEAR
EXTERN C prtFatCutTo@4 : NEAR
EXTERN C prt_PrintIllegalCutMessage : NEAR
EXTERN C prt_ValidateVsh : NEAR
EXTERN C registerBootstrapTaskCim : NEAR
EXTERN C pcallOnSystemStack : NEAR
EXTERN C prt_needsToYield : NEAR
EXTERN C prt_validateTlsPointer : NEAR
EXTERN C prtYieldUntil : NEAR
EXTERN C prtToUnmanaged : NEAR
EXTERN C prtToManaged : NEAR
EXTERN C prt_GetTaskNonInline : NEAR
EXTERN C prtPillarCompilerUnwinder : NEAR

REGISTER_SIZE = 4

EXTERN C prt_Globals : DWORD
EXTERN C prtMinFreeStackSpace : DWORD

ELSE ; // __X86_64__

EXTERN prt_ExitThread : NEAR  
EXTERN printf : NEAR
EXTERN exit : NEAR
EXTERN prtYieldUnmanaged : NEAR
EXTERN prtFatCutTo : NEAR
EXTERN prt_PrintIllegalCutMessage : NEAR
EXTERN prt_ValidateVsh : NEAR
EXTERN registerBootstrapTaskCim : NEAR
EXTERN pcallOnSystemStack : NEAR
EXTERN prt_needsToYield : NEAR
EXTERN prt_StdFSpawnOld : NEAR

REGISTER_SIZE = 8

EXTERN prt_Globals : DWORD

ENDIF ; // __X86_64__


MIN_UNMANAGED_STACK   = 500000
;MIN_UNMANAGED_STACK   = 16384


; // Macros that work for both IA32 and __X86_64__
firstArg MACRO Names:vararg
    _argNum$ = REGISTER_SIZE
    nextArg Names
ENDM

nextArg MACRO Names:vararg
    _argNum$ = _argNum$ + REGISTER_SIZE
    for StackSlot, <Names>
        &StackSlot& = _argNum$
    endm
ENDM

; // Starts off the stack offset computations for the first entry in the frame.
firstStackOffsetSized MACRO Size, Names:vararg
    _cur$ = 0
    nextStackOffsetSized Size, Names
ENDM

; // Decrements the current frame variable offset (_cur$) and assigns the new offset to each variable name in "Names".
nextStackOffsetSized MACRO Size, Names:vararg
    _cur$ = _cur$ - Size
    for StackSlot, <Names>
        &StackSlot& = _cur$
    endm
ENDM

; // Will use a default size of 4 for the stack slot.
firstStackOffset MACRO Names:vararg
    firstStackOffsetSized 4, Names
ENDM

; // Will use a default size of 4 for the stack slot.
nextStackOffset MACRO Names:vararg
    nextStackOffsetSized 4, Names
ENDM


IFNDEF __X86_64__

; =============================================================================================
; These constants are verified in the validateStubConstants function

IFDEF TLS0
PRT_TASK_USER_TLS = 0                     ; mUserTls
PRT_TASK_LAST_VSE = 4                     ; mLastVse
ELSE
PRT_TASK_USER_TLS = 4                     ; mUserTls
PRT_TASK_LAST_VSE = 0                     ; mLastVse
ENDIF
PRT_TASK_THREAD_HANDLE = 8                ; mThreadHandle
PRT_VSE_ENTRY_TYPE_CODE = 0               ; entryTypeCode
PRT_VSE_NEXT_FRAME = 4                    ; nextFrame
PRT_VSE_TARGET_CONTINUATION = 8           ; targetContinuation
CONTINUATION_EIP_OFFSET = 0

; =============================================================================================
;  COMMON CODE SEQUENCES
; =============================================================================================

;TLS_REGISTER = "ebx"

; Check for a valid TLS_REGISTER setting.
IFDEF TLS_REGISTER
IF TLS_REGISTER EQ "ebx"
ECHO Using ebx as TLS register.
tlsreg TEXTEQU <ebx>
savereg1 TEXTEQU <esi>
savereg2 TEXTEQU <edi>
restoreTlsRegister MACRO
        mov tlsreg, _savedEbx$[ebp]
ENDM
ELSEIF TLS_REGISTER EQ "edi"
ECHO Using edi as TLS register.
tlsreg TEXTEQU <edi>
savereg1 TEXTEQU <esi>
savereg2 TEXTEQU <ebx>
restoreTlsRegister MACRO
        mov tlsreg, _savedEdi$[ebp]
ENDM
ELSE
ECHO Error: TLS_REGISTER should be set to "ebx" or "edi", or left unset.
.ERR
ENDIF
ELSE
savereg1 TEXTEQU <esi>
savereg2 TEXTEQU <edi>
restoreTlsRegister MACRO
ENDM
ENDIF

; // Like getTlsIntoEax below but always refreshes tlsreg from the current thread's TLS.
; // Both tlsreg (ebx) and eax come back as the TLS value.
loadTlsRegister MACRO
    call prt_GetTaskNonInline
    mov tlsreg, eax
ENDM

; // Returns the current thread's TLS (its PrtTask pointer) in eax.  Leaves other registers unchanged.
getTlsIntoEax MACRO
IFDEF TLS_REGISTER
    mov eax, tlsreg
ELSE
    call mcrtThreadGet              ;; // get the current thread's McrtThread* into eax
    push eax                        ;; // push thread param on stack for next func
    call mcrtThreadGetTLSForThread  ;; // get the current thread's PrtTask pointer (its TLS) into eax
    pop  edx
    ; add  esp, 4                     ;; // pop args to previous func
ENDIF
ENDM

; // A stub prologue that sets up an ebp-based frame and saves ebx, esi, edi.
fullStubProlog MACRO
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi
ENDM

; // Inverse of fullStubProlog
fullStubEpilog MACRO
    pop edi
    pop esi
    pop ebx
    pop ebp
ENDM

; // Copies a function's arguments to its frame. Also reserves space in the frame for the copied arguments.
; // Leaves eax and edx untouched.
; // Modifies ebx, ecx, esi, edi.
; // Upon exit, ebx will contain the number of bytes of arguments.
copyArgs MACRO argStartOffsetFromEbp, argSizeOffsetFromEbp
    mov ebx, DWORD PTR argSizeOffsetFromEbp[ebp]    ;; // ebx = number of 4-byte params
    shl ebx, 2                                      ;; // ebx = size of params in bytes
    sub esp, ebx                                    ;; // reserve space for copies of the incoming arguments

    ;; // copy the arguments to the stack starting at esp.
    cld
    mov ecx, DWORD PTR argSizeOffsetFromEbp[ebp]
    mov esi, DWORD PTR argStartOffsetFromEbp[ebp]
    mov edi, esp
    ;; // rep movsd copies "ecx" dwords from [esi] to [edi].  cld means esi and edi are incremented after each copy.
    rep movsd
ENDM

 ; // Restore the original frame's ebp and esp values using edx, which points to a VSE embedded in the frame.
continuationProlog MACRO espOffsetFromEbp, contOffsetFromEbp
    lea esp, [edx - contOffsetFromEbp + espOffsetFromEbp]
    lea ebp, [edx - contOffsetFromEbp]
ENDM

; // Pushes a VSE.  Assumes Prt_Task pointer is in eax.  Leaves eax and edx intact.
pushVse MACRO VseOffsetFromEbp, VseType
    mov ecx, [eax+PRT_TASK_LAST_VSE]
    mov DWORD PTR [ebp+VseOffsetFromEbp+PRT_VSE_ENTRY_TYPE_CODE], VseType
    mov DWORD PTR [ebp+VseOffsetFromEbp+PRT_VSE_NEXT_FRAME], ecx
    mov DWORD PTR [ebp+VseOffsetFromEbp+PRT_VSE_TARGET_CONTINUATION], 0
    lea ecx, [ebp+VseOffsetFromEbp]
    mov [eax+PRT_TASK_LAST_VSE], ecx
ENDM

; // Pops a VSE.  Assumes Prt_Task pointer is in eax.  Leaves eax and edx intact.
popVse MACRO VseOffsetFromEbp, VseType
IFDEF DEBUG_VSE_POPS
    push eax
    push edx
    push VseType
    lea ecx, [ebp+VseOffsetFromEbp]
    push ecx
    call prt_ValidateVsh
    add esp, 8
    pop edx
    pop eax
ENDIF ; // DEBUG_VSE_POPS
    mov ecx, [eax+PRT_TASK_LAST_VSE]
    mov ecx, [ecx+PRT_VSE_NEXT_FRAME]
    mov [eax+PRT_TASK_LAST_VSE], ecx
ENDM

; =============================================================================================

_TEXT SEGMENT



; =============================================================================================



PUBLIC prtWatermarkPrototype@0
PUBLIC prt_WatermarkPrototypeStart
PUBLIC prt_WatermarkPostTopIndex1
PUBLIC prt_WatermarkPostTopIndex2
PUBLIC prt_WatermarkPostStubStackStart
PUBLIC prt_WatermarkPostStubStart
PUBLIC prt_WatermarkPostRealEipMove
PUBLIC prt_WatermarkPrototypeEnd

prtWatermarkPrototype@0 PROC EXPORT
prt_WatermarkPrototypeStart::
    mov ecx, dword ptr [junk1]  ; // ecx = current top index
prt_WatermarkPostTopIndex1::
	inc ecx                                   ; // ecx = new top index
    mov dword ptr [junk1], ecx  ; // increment the stack's top...replace DEADBEEF with the address of this task's stub stack top
prt_WatermarkPostTopIndex2::
	lea ecx, junk2[ecx*4]   ; // ecx = address of the top free stub stack array entry
prt_WatermarkPostStubStackStart:: 
	mov dword ptr [ecx], prt_WatermarkPrototypeStart ; // write the stub start value into the stub stack array
prt_WatermarkPostStubStart::
    mov ecx, exit
prt_WatermarkPostRealEipMove::
	jmp ecx                                   ; // any jump target will do...will be replaced in each copy of the stub
prt_WatermarkPrototypeEnd::
prtWatermarkPrototype@0 ENDP

; =============================================================================================

; // void __pdecl prtYield(void);


PUBLIC prtYield@0
PUBLIC prt_YieldStart
PUBLIC prt_YieldEnd

prtYield@0 PROC EXPORT
prt_YieldStart::
    ret
prt_YieldEnd::
prtYield@0 ENDP



; =============================================================================================

; // void __stdcall prtInvokeManagedFunc(PrtCodeAddress managedFunc, void *argStart, unsigned argSize);


; // Export the different locations within the prtInvokeManagedFunc function.
PUBLIC prtInvokeManagedFunc@12          ; start of the function
PUBLIC prtInvokeManagedIntRet@12          ; start of the function
PUBLIC prt_InvokeManagedFuncStart       ; start of the function
PUBLIC prt_InvokeManagedFuncUnwindContinuation
PUBLIC prt_InvokeManagedFuncEnd         ; end of the function

; // Stack offsets of this function's aguments.
firstArg _managedFunc$
nextArg  _argStart$
nextArg  _argSize$

; // Stack frame layout:
;   ebp+0:  saved ebp
;   ebp-4:  saved ebx
;   ebp-8:  saved esi
;   ebp-12: saved edi
firstStackOffset _savedEbx$
nextStackOffset _savedEsi$
nextStackOffset _savedEdi$
nextStackOffset _contArgHigh32Bits$, _firstFrameLocal$
nextStackOffset _contArgLow32Bits$
nextStackOffset _contVsh$
nextStackOffset _contEip$, _contStart$, _normalEsp$

U2MFRAMESIZE = _firstFrameLocal$ - _normalEsp$ + REGISTER_SIZE

prtInvokeManagedIntRet@12 PROC EXPORT
prtInvokeManagedIntRet@12 ENDP
prtInvokeManagedFunc@12 PROC EXPORT
prt_InvokeManagedFuncStart::
    ; // A stack limit check is unnecessary because we must already be running
    ; // on a full-sized stack for unmanaged code.
    fullStubProlog                      ; // basic ebp-based frame prolog that saves ebx, esi, edi
    sub esp, U2MFRAMESIZE
    copyArgs _argStart$, _argSize$      ; // reserve space and copy arguments
IFDEF TLS_REGISTER
    loadTlsRegister
ENDIF
    
    getTlsIntoEax                       ; // leaves eax set to TLS
    push eax
    call prtToManaged
    pop  eax

    call DWORD PTR _managedFunc$[ebp]   ; // call the managed function. don't disturb the return registers after this

prt_InvokeManagedFuncAfterCall:
    mov  esi, eax                       ; // save away the return value

    getTlsIntoEax                       ; // leaves eax set to TLS
    push eax
    call prtToUnmanaged
    pop  eax

    mov  eax, esi                       ; // restore return value

    add esp, U2MFRAMESIZE
    fullStubEpilog
    ret 12
    
prt_InvokeManagedFuncUnwindContinuation::
    continuationProlog _normalEsp$, _contStart$
    mov eax, [ebp+_contArgLow32Bits$]
    mov edx, [ebp+_contArgHigh32Bits$]
    jmp prt_InvokeManagedFuncAfterCall
prt_InvokeManagedFuncEnd::
prtInvokeManagedFunc@12 ENDP


; =============================================================================================

; // void __stdcall prtInvokeUnmanagedFunc(PrtCodeAddress unmanagedFunc, void *argStart, unsigned argSize, PrtCallingConvention callingConvention);

PUBLIC prtInvokeUnmanagedFunc@16             ; start of the function
PUBLIC prtInvokeUnmanagedIntRet@16           ; start of the function
PUBLIC prt_InvokeUnmanagedFuncStart          ; start of the function
PUBLIC prt_InvokeUnmanagedFuncPostCall       ; 
PUBLIC prt_InvokeUnmanagedFuncDestructor     ; VSE type identifier (code address)
PUBLIC prt_InvokeUnmanagedFuncUnwindContinuation
PUBLIC prt_InvokeUnmanagedFuncEnd            ; end of the function

; // Stack offsets of this function's aguments.
firstArg _unmanagedFunc$
nextArg  _argStart$
nextArg  _argSize$
nextArg  _callingConvention$

; // Keep these offsets up to date with the definition of struct Prt_M2uFrame.
; Stack frame layout:
;   ebp+0:  saved ebp
;   ebp-4:  saved ebx
;   ebp-8:  saved esi
;   ebp-12: saved edi
firstStackOffset _savedEbx$
nextStackOffset _savedEsi$
nextStackOffset _savedEdi$
nextStackOffset _contArgHigh32Bits$, _firstFrameLocal$
nextStackOffset _contArgLow32Bits$
nextStackOffset _contVsh$
nextStackOffset _contEip$, _contStart$
nextStackOffset _realM2uUnwinder$
;nextStackOffset _callsite_id$
;; end of core VSE fields
nextStackOffset _targetContinuation$
nextStackOffset _nextVsePtr$
nextStackOffset _frameType$, _vsePtr$, _normalEsp$
;; start of VSE

M2UFRAMESIZE = _firstFrameLocal$ - _normalEsp$ + REGISTER_SIZE

prtInvokeUnmanagedIntRet@16::
prtInvokeUnmanagedFunc@16 PROC EXPORT
prt_InvokeUnmanagedFuncStart::
        getTlsIntoEax                                   ; // leaves eax set to TLS, edx set to mcrtThread
        fullStubProlog                                  ; // basic ebp-based frame prolog that saves ebx, esi, edi
        sub  esp, M2UFRAMESIZE                          ; // reserve space for rest of the frame and local vars.
        lea  esi, prtPillarCompilerUnwinder
        mov  dword ptr _realM2uUnwinder$[ebp], esi
        pushVse _vsePtr$, prt_InvokeUnmanagedFuncDestructor
        
        push eax
        call prtToUnmanaged
        pop  eax

        copyArgs _argStart$, _argSize$                  ; // reserve space and copy arguments, sets ebx = number of arg bytes
        call DWORD PTR _unmanagedFunc$[ebp]
prt_InvokeUnmanagedFuncPostCall::
        mov ecx, _callingConvention$[ebp]               ; // ecx != 0 if target is a stdcall
        .IF (ecx == 0)                                  ; // if target was cdecl instead of stdcall, then remove arguments from stack
            add esp, ebx    
        .ENDIF

prt_InvokeUnmanagedFuncAfterCall:
        restoreTlsRegister
        mov savereg1, eax                               ; // save possible return registers away
        mov savereg2, edx                               ; // save possible return registers away
        ; // we use targetContinuation field here because we no longer need it
        ; // for fat cuts.  We use it to indicate we are at the prtYieldUnmanaged callsite
        ; // rather than the call to the unmanaged function.
        ; mov DWORD PTR [ebp+_callsite_id$], 1
        ; call prtYieldUnmanaged

        getTlsIntoEax                                   ; // get current Prt_Task pointer
        
        push eax
        call prtToManaged
        pop  eax
        
        popVse _vsePtr$, prt_InvokeUnmanagedFuncDestructor
    
        mov eax, savereg1                               ; // restore return registers
        mov edx, savereg2
        add esp, M2UFRAMESIZE                           ; // remove the rest of the frame and local vars
    
        fullStubEpilog
        ret 16
    
prt_InvokeUnmanagedFuncDestructor::
    continuationProlog _normalEsp$, _vsePtr$
    push [ebp+_targetContinuation$]                     ; // recut
    call prtFatCutTo@4

prt_InvokeUnmanagedFuncUnwindContinuation::
    continuationProlog _normalEsp$, _contStart$
    mov eax, [ebp+_contArgLow32Bits$]
    mov edx, [ebp+_contArgHigh32Bits$]
    jmp prt_InvokeUnmanagedFuncAfterCall
    
prt_InvokeUnmanagedFuncEnd::
prtInvokeUnmanagedFunc@16 ENDP

; =============================================================================================

PUBLIC prt_PcallDestructor

prt_PcallDestructor PROC EXPORT
    mov  esp, edx
IFDEF TLS_REGISTER
    loadTlsRegister
ENDIF
    push 0
    push 0
    push 0
    push prt_PrintIllegalCutMessage
    call prtInvokeUnmanagedFunc@16
    call prt_ExitThread
prt_PcallDestructor ENDP

; =============================================================================================

setNextConstant MACRO Value
    _cur$ = _cur$ + REGISTER_SIZE
    mov eax, _cur$[esp]
    mov DWORD PTR [eax], Value
ENDM

PUBLIC prt_getStubConstants                ; start of the function
    
_cur$ = 0

prt_getStubConstants PROC EXPORT
    setNextConstant PRT_TASK_LAST_VSE
    setNextConstant PRT_TASK_USER_TLS
    setNextConstant PRT_TASK_THREAD_HANDLE
    setNextConstant PRT_VSE_ENTRY_TYPE_CODE
    setNextConstant PRT_VSE_NEXT_FRAME
    setNextConstant PRT_VSE_TARGET_CONTINUATION
    setNextConstant CONTINUATION_EIP_OFFSET
    ret
prt_getStubConstants ENDP

; =============================================================================================

PUBLIC prt_getCurrentEsp

prt_getCurrentEsp PROC EXPORT
    mov eax, esp
    ret
prt_getCurrentEsp ENDP

; =============================================================================================

PUBLIC prtThinCutTo

_continuation$ = 4

prtThinCutTo PROC EXPORT
IFDEF TLS_REGISTER
    loadTlsRegister
ENDIF
    mov edx, _continuation$[esp]
    jmp DWORD PTR [edx+CONTINUATION_EIP_OFFSET]
prtThinCutTo ENDP

; =============================================================================================

PUBLIC prtYieldUntilDestructor
prtYieldUntilDestructor PROC EXPORT
    mov edx, [edx+8]                           ; // edx = target continuation
    push edx
    call prtFatCutTo@4
prtYieldUntilDestructor ENDP

; =============================================================================================

PUBLIC prt_bootstrapTaskAsm                    ; start of the function
PUBLIC prt_bootstrapTaskAsmCall

; // Stack offsets of this function's aguments.
firstArg _cimCreated$
nextArg  _funcToCall$
nextArg _argStart$
nextArg _argSize$

prt_bootstrapTaskAsm PROC EXPORT
    fullStubProlog
prt_bootstrapTaskAsmStart:
    mov  esi, DWORD PTR _cimCreated$[ebp]
    test esi, esi
    jne  cimAlreadyCreated
    lea  eax, prt_bootstrapTaskAsmEnd
    push eax
    lea  eax, prt_bootstrapTaskAsmStart
    push eax
    call registerBootstrapTaskCim
    add  esp, 8
cimAlreadyCreated:
    ;; // get all the needed vars into regs before we update esp in case this is
    ;; // not an ebp based frame.
    mov  edx, _funcToCall$[ebp]           ;; // edx = the function to invoke

    copyArgs _argStart$, _argSize$      ; // reserve space and copy arguments

IFDEF TLS_REGISTER
    push edx
    loadTlsRegister
    pop edx
ENDIF        
    call edx                              ;; // invoke the function
prt_bootstrapTaskAsmCall::

    call prt_ExitThread                   ;; // exit the thread
prt_bootstrapTaskAsmEnd:
prt_bootstrapTaskAsm ENDP

; =============================================================================================

PUBLIC prt_testStackSize                    ; start of the function

; // Stack offsets of this function's aguments.
firstArg _funcToCall$
nextArg  _stackTop$

prt_testStackSize PROC EXPORT
    fullStubProlog
IFDEF TLS_REGISTER
    loadTlsRegister
ENDIF

    mov  esi, esp
    mov  edx, _funcToCall$[ebp]           ;; // edx = the function to invoke
    mov  esp, _stackTop$[ebp]             ;; // transition to a new stack
        
    call edx                              ;; // invoke the function

    mov  esp, esi
    fullStubEpilog
    ret
prt_testStackSize ENDP

; =============================================================================================

PUBLIC prt_pcallAsm                       ; start of the function

; // Stack offsets of this function's aguments.
firstArg _newEsp$

prt_pcallAsm PROC EXPORT
    fullStubProlog
    mov esi, esp
    mov esp, _newEsp$[ebp]
    call pcallOnSystemStack
    mov esp, esi
    fullStubEpilog
    ret
prt_pcallAsm ENDP

; =============================================================================================

; // void * __pdecl prt_getTlsRegister(void);
PUBLIC prt_getTlsRegister@0
PUBLIC prt_getTlsRegisterStart
PUBLIC prt_getTlsRegisterEnd

prt_getTlsRegister@0 PROC EXPORT
prt_getTlsRegisterStart::
IFDEF TLS_REGISTER
    mov eax, tlsreg
ELSE
    mov eax, 0
ENDIF
    ret
prt_getTlsRegisterEnd::
prt_getTlsRegister@0 ENDP



; =============================================================================================
; =============================================================================================
_TEXT   ENDS
; =============================================================================================
; =============================================================================================


ELSE ; // __X86_64__


; =============================================================================================
; These constants are verified in the validateStubConstants function
PRT_TASK_LAST_VSE = 8
PRT_TASK_CUR_STACK_LIMIT = 8
PRT_TASK_USER_TLS = 0
PRT_TASK_THREAD_HANDLE = 32
PRT_TASK_CUR_STACKLET_REF_COUNT_PTR = 24
PRT_VSE_ENTRY_TYPE_CODE = 0
PRT_VSE_NEXT_FRAME = 8
PRT_VSE_TARGET_CONTINUATION = 16
PRT_TASK_STARTING_STACK = 32
CONTINUATION_EIP_OFFSET = 0
PRT_THREAD_DONOTRUN = 118                 ; offset in McrtThread of the 16-bit doNotRun flag

ARG_REG1 EQU rcx
ARG_REG2 EQU rdx
ARG_REG3 EQU r8
ARG_REG4 EQU r9

; // Modifies esp to start using the stack originally given to the current task.  
; // Modifies eax and esp, other regs unchanged.
goToSystemStack MACRO
    getTlsIntoEax
    ;; // Go to the original stack stored at a known offset from the start of the Pillar task.
    mov rsp, [rax+PRT_TASK_STARTING_STACK]    
    and  rsp, 0FFffFFffFFffFFf0h
ENDM

fullStubProlog MACRO
    push rbp
    mov  rbp, rsp
    push rbx
    push rsi
    push rdi
ENDM

; // Inverse of fullStubProlog
fullStubEpilog MACRO
    pop rdi
    pop rsi
    pop rbx
    pop rbp
ENDM

; // Copies a function's arguments to its frame. Also reserves space in the frame for the copied arguments.
; // Leaves rax and rdx untouched.
; // At completion, rbx = size of args in bytes, rcx/rdx/r8/r9 are the args to the next function, rsi/rdi are trashed.
; // startReg and sizeReg should not be rbx, rcx, rsi or rdi.
copyArgs MACRO startReg, sizeReg
    mov rbx, sizeReg                            ;; // ebx = number of 8-byte params
    shl rbx, 3                                  ;; // ebx = size of params in bytes
    add rbx, 31
    mov rcx, 0FFffFFffFFe0h
    and rbx, rcx
    mov rcx, sizeReg
    sub rcx, 1
    sar rcx, 63
    and rcx, 32
    add rbx, rcx
    sub rsp, rbx                                ;; // make room to copy args that are on the stack
    

    ;; // copy the stack arguments to the next stack locationstack starting at esp.
    cld
    mov rcx, sizeReg                            ;; // arg size
    mov rsi, startReg                           ;; // arg start
    mov rdi, rsp                                ;; // place to copy
    ;; // rep movsd copies "rcx" dwords from [rsi] to [rdi].  cld means rsi and rdi are incremented after each copy.
    rep movsq

;    cmp rbx, 8
;    jl copyDone        
        ; // MOVE ARGS INTO REGISTERS?  BUT WE DON'T HAVE TYPE INFORMATION!!!
        mov ARG_REG1,  QWORD PTR [rsp+0]
        movd xmm0, ARG_REG1
;    cmp rbx, 16
;    jl copyDone        
        mov ARG_REG2,  QWORD PTR [rsp+8]
        movd xmm1, ARG_REG2
;    cmp rbx, 24
;    jl copyDone        
        mov ARG_REG3,   QWORD PTR [rsp+16]
        movd xmm2, ARG_REG3
;    cmp rbx, 32
;    jl copyDone        
        mov ARG_REG4,   QWORD PTR [rsp+24]
        movd xmm3, ARG_REG4
;copyDone::
ENDM

; // Pushes a VSE.  Assumes Prt_Task pointer is in eax.  Leaves eax and edx intact.
pushVse MACRO VseOffsetFromEbp, VseType
	mov r10, VseType
    mov QWORD PTR [rbp+VseOffsetFromEbp+PRT_VSE_ENTRY_TYPE_CODE], r10
    mov r10, [rax+PRT_TASK_LAST_VSE]
    mov QWORD PTR [rbp+VseOffsetFromEbp+PRT_VSE_NEXT_FRAME], r10
    mov QWORD PTR [rbp+VseOffsetFromEbp+PRT_VSE_TARGET_CONTINUATION], 0
    lea r10, [rbp+VseOffsetFromEbp]
    mov [rax+PRT_TASK_LAST_VSE], r10
ENDM

; // Pops a VSE.  Assumes Prt_Task pointer is in eax.  Leaves eax and edx intact.
popVse MACRO VseOffsetFromEbp, VseType
IFDEF DEBUG_VSE_POPS
    push rax
    push rdx

    mov  ARG_REG2, VseType
    lea  ARG_REG1, [rbp+VseOffsetFromEbp]
    
    sub  rsp, 32
    call prt_ValidateVsh
    add  rsp, 32

    pop  rdx
    pop  rax
ENDIF ; // DEBUG_VSE_POPS
    mov  rcx, [rax+PRT_TASK_LAST_VSE]
    mov  rcx, [rcx+PRT_VSE_NEXT_FRAME]
    mov  [rax+PRT_TASK_LAST_VSE], rcx
ENDM

 ; // Restore the original frame's ebp and esp values using edx, which points to a VSE embedded in the frame.
continuationProlog MACRO espOffsetFromEbp, vseOffsetFromEbp
    lea rsp, [rdx - vseOffsetFromEbp + espOffsetFromEbp]
    lea rbp, [rdx - vseOffsetFromEbp]
ENDM

saveArgRegs MACRO
    push ARG_REG1
    push ARG_REG2
    push ARG_REG3
    push ARG_REG4
ENDM

restoreArgRegs MACRO
    pop ARG_REG4
    pop ARG_REG3
    pop ARG_REG2
    pop ARG_REG1
ENDM

; // Returns the current thread's TLS (its PrtTask pointer) in rax.  
; // Leaves argument registers unchanged.
; // r13 out = mcrt thread handle
getTlsIntoEax MACRO
    saveArgRegs

    sub  rsp, 32
    call mcrtThreadGet                   ;; // get the current thread's McrtThread* into eax
    mov  ARG_REG1, rax                   ;; // prepare for call to mcrtThreadGetTLSForThread
    push rax                             ;; // save thread handle to the stack
    sub  rsp, 8                          ;; // reserve space for home location of ARG_REG1
    call mcrtThreadGetTLSForThread       ;; // get the current thread's PrtTask pointer (its TLS) into eax
    add  rsp, 8                          ;; // remove space for home location of ARG_REG1
    pop  r13
    add  rsp, 32

    restoreArgRegs
ENDM

getThreadHandleIntoRax MACRO
    sub  rsp, 32
    call mcrtThreadGet                   ;; // get the current thread's McrtThread* into rax
    add  rsp, 32
ENDM

saveFourRegArgsToHomeAfterJump MACRO
    mov [rsp+0],  ARG_REG1
    mov [rsp+8],  ARG_REG2
    mov [rsp+16], ARG_REG3
    mov [rsp+24], ARG_REG4
ENDM

restoreFourRegArgsFromHomeAfterJump MACRO
    mov ARG_REG1, [rsp+0]
    mov ARG_REG2, [rsp+8]
    mov ARG_REG3, [rsp+16]
    mov ARG_REG4, [rsp+24]
ENDM

saveFourRegArgsToHomeBeforeProlog MACRO
    mov [rsp+8],  ARG_REG1
    mov [rsp+16], ARG_REG2
    mov [rsp+24], ARG_REG3
    mov [rsp+32], ARG_REG4
ENDM

saveFourRegArgsToHomeAfterProlog MACRO
    mov [rbp+16], ARG_REG1
    mov [rbp+24], ARG_REG2
    mov [rbp+32], ARG_REG3
    mov [rbp+40], ARG_REG4
ENDM

restoreFourRegArgsFromHomeAfterProlog MACRO
    mov ARG_REG1, [rbp+16]
    mov ARG_REG2, [rbp+24]
    mov ARG_REG3, [rbp+32]
    mov ARG_REG4, [rbp+40]
ENDM

_TEXT SEGMENT

; =============================================================================================

PUBLIC prtInvokeManagedFunc             ; start of the function
PUBLIC prt_InvokeManagedFuncStart       ; start of the function
PUBLIC prt_InvokeManagedFuncUnwindContinuation
PUBLIC prt_InvokeManagedFuncEnd         ; end of the function

; // Stack offsets of this function's aguments.
firstArg _managedFunc$
nextArg  _argStart$
nextArg  _argSize$

firstStackOffsetSized 8, _savedEbx$
nextStackOffsetSized 8, _savedEsi$
nextStackOffsetSized 8, _savedEdi$
nextStackOffsetSized 8, _createAlignment$, _firstFrameLocal$
nextStackOffsetSized 8, _contArgHigh32Bits$
nextStackOffsetSized 8, _contArgLow32Bits$
nextStackOffsetSized 8, _contVsh$
nextStackOffsetSized 8, _contEip$, _contStart$, _normalEsp$

U2MFRAMESIZE = _firstFrameLocal$ - _normalEsp$ + REGISTER_SIZE

prtInvokeManagedFunc PROC EXPORT
prt_InvokeManagedFuncStart::
    ; // A stack limit check is unnecessary because we must already be running
    ; // on a full-sized stack for unmanaged code.
    fullStubProlog                      ; // won't modify input arg registers
    sub  rsp, U2MFRAMESIZE
    mov  rax, ARG_REG1
    copyArgs ARG_REG2, ARG_REG3         ; // reserve space and copy arguments
    call rax                            ; // call the managed function. don't disturb the return registers after this
prt_InvokeManagedFuncAfterCall::
    add  rsp, rbx                       ; // remove args since this is fastcall
    add  rsp, U2MFRAMESIZE
    fullStubEpilog
    ret
prt_InvokeManagedFuncUnwindContinuation::
    continuationProlog _normalEsp$, _contStart$
    mov rax, [rbp+_contArgLow32Bits$]
    mov rdx, [rbp+_contArgHigh32Bits$]
    jmp prt_InvokeManagedFuncAfterCall
prt_InvokeManagedFuncEnd::
prtInvokeManagedFunc ENDP

; =============================================================================================

; // void __stdcall prtInvokeUnmanagedFunc(PrtCodeAddress unmanagedFunc, void *argStart, unsigned argSize, PrtCallingConvention callingConvention);

PUBLIC prtInvokeUnmanagedFunc                ; start of the function
PUBLIC prt_InvokeUnmanagedFuncStart          ; start of the function
PUBLIC prt_InvokeUnmanagedFuncEnd            ; end of the function
PUBLIC prt_InvokeUnmanagedFuncDestructor     ; VSE type identifier (code address)
PUBLIC prt_InvokeUnmanagedFuncUnwindContinuation

; // Stack offsets of this function's aguments.
firstArg _unmanagedFunc$
nextArg  _argStart$
nextArg  _argSize$
nextArg  _callingConvention$

; // Keep these offsets up to date with the definition of struct Prt_M2uFrame.
; Stack frame layout:
;   ebp+0:  saved ebp
;   ebp-4:  saved ebx
;   ebp-8:  saved esi
;   ebp-12: saved edi
firstStackOffsetSized 8, _savedEbx$
nextStackOffsetSized  8, _savedEsi$
nextStackOffsetSized  8, _savedEdi$
nextStackOffsetSized  8, _contArgHigh32Bits$, _firstFrameLocal$
nextStackOffsetSized  8, _contArgLow32Bits$
nextStackOffsetSized  8, _contVsh$
nextStackOffsetSized  8, _contEip$, _contStart$
;; end of core VSE fields
nextStackOffsetSized  8, _targetContinuation$
nextStackOffsetSized  8, _nextVsePtr$
nextStackOffsetSized  8, _frameType$, _vsePtr$, _normalEsp$
;; start of VSE

M2UFRAMESIZE = _firstFrameLocal$ - _normalEsp$ + REGISTER_SIZE

prtInvokeUnmanagedFunc PROC EXPORT
prt_InvokeUnmanagedFuncStart::
        ; // We have enough space and don't need a new stack.
        fullStubProlog                                  ; // basic ebp-based frame prolog that saves ebx, esi, edi
        mov _callingConvention$[rbp], ARG_REG4
        mov _argSize$[rbp], ARG_REG3
        mov _argStart$[rbp], ARG_REG2
        mov _unmanagedFunc$[rbp], ARG_REG1
        
        sub rsp, M2UFRAMESIZE                           ; // reserve space for rest of the frame and local vars.
        pushVse _vsePtr$, prt_InvokeUnmanagedFuncDestructor ; // doesn't modify input args

        mov  rax, ARG_REG1
        copyArgs ARG_REG2, ARG_REG3                     ; // reserve space and copy arguments, sets ebx = number of arg bytes
        call rax
prt_InvokeUnmanagedFuncAfterCall::
        
        mov rcx, QWORD PTR _callingConvention$[rbp]
        cmp rcx, 0
        jne TARGET_WAS_STDCALL
        add rsp, rbx                                    ; // if target was cdecl instead of stdcall, then remove arguments from stack
TARGET_WAS_STDCALL:
    
        mov rdi, rax                                    ; // save possible return registers away
        
;        call prtYieldUnmanaged

        getTlsIntoEax                                   ; // get current Prt_Task pointer
        popVse _vsePtr$, prt_InvokeUnmanagedFuncDestructor
    
        mov rax, rdi                                    ; // restore return registers
        add rsp, M2UFRAMESIZE                           ; // remove the rest of the frame and local vars
    
        fullStubEpilog
        ret
    
prt_InvokeUnmanagedFuncDestructor::
    continuationProlog _normalEsp$, _vsePtr$
    mov  ARG_REG1, [rbp+_targetContinuation$]                ; // recut
    sub  rsp, 32
    call prtFatCutTo
    
prt_InvokeUnmanagedFuncUnwindContinuation::
    continuationProlog _normalEsp$, _contStart$
    mov rax, [rbp+_contArgLow32Bits$]
    mov rdx, [rbp+_contArgHigh32Bits$]
    jmp prt_InvokeUnmanagedFuncAfterCall
    
prt_InvokeUnmanagedFuncEnd::
prtInvokeUnmanagedFunc ENDP

; =============================================================================================

PUBLIC prt_PcallDestructor

prt_PcallDestructor PROC EXPORT
    mov  rsp, rdx

    mov  ARG_REG4, 0
    mov  ARG_REG3, 0
    mov  ARG_REG2, 0
    mov  ARG_REG1, prt_PrintIllegalCutMessage
    sub  rsp, 32
    call prtInvokeUnmanagedFunc
    add  rsp, 32
    goToSystemStack
    call prt_ExitThread
prt_PcallDestructor ENDP

; =============================================================================================

setNextConstant MACRO Value
    _cur$ = _cur$ + REGISTER_SIZE
    mov rax, _cur$[rsp]
    mov QWORD PTR [rax], Value
ENDM

PUBLIC prt_getStubConstants                ; start of the function

_cur$ = 0

prt_getStubConstants PROC EXPORT
    saveFourRegArgsToHomeBeforeProlog
    setNextConstant PRT_TASK_LAST_VSE
    setNextConstant PRT_TASK_CUR_STACK_LIMIT
    setNextConstant PRT_TASK_USER_TLS
    setNextConstant PRT_VSE_ENTRY_TYPE_CODE
    setNextConstant PRT_VSE_NEXT_FRAME
    setNextConstant PRT_VSE_TARGET_CONTINUATION
    setNextConstant PRT_TASK_STARTING_STACK
    setNextConstant PRT_TASK_CUR_STACKLET_REF_COUNT_PTR
    setNextConstant CONTINUATION_EIP_OFFSET
    setNextConstant PRT_TASK_THREAD_HANDLE
    ret

prt_getStubConstants ENDP

; =============================================================================================
; =============================================================================================

PUBLIC prt_getCurrentEsp

prt_getCurrentEsp PROC EXPORT
    mov rax, rsp
    ret
prt_getCurrentEsp ENDP

; =============================================================================================

PUBLIC prtThinCutTo

_continuation$ = 8

prtThinCutTo PROC EXPORT
    mov rdx, ARG_REG1
    mov rax, [rdx+CONTINUATION_EIP_OFFSET]
    jmp rax
prtThinCutTo ENDP

; =============================================================================================

PUBLIC prt_bootstrapTaskAsm                    ; start of the function
PUBLIC prt_bootstrapTaskAsmCall

; // Stack offsets of this function's aguments.
firstArg _cimCreated$
nextArg  _funcToCall$
nextArg  _stackTop$

prt_bootstrapTaskAsm PROC EXPORT
prt_bootstrapTaskAsmStart:
    fullStubProlog
    sub  rsp, 24
    ; // Save args to stack.
    mov  _cimCreated$[rbp], ARG_REG1
    mov  _funcToCall$[rbp], ARG_REG2
    mov  _stackTop$[rbp],   ARG_REG3

    test ARG_REG1, ARG_REG1
    jne  cimAlreadyCreated
    lea  ARG_REG2, prt_bootstrapTaskAsmEnd
    lea  ARG_REG1, prt_bootstrapTaskAsmStart
    sub  rsp, 16
    call registerBootstrapTaskCim
    add  rsp, 16
cimAlreadyCreated:
    mov  rsp, _stackTop$[rbp]             ;; // transition to a new stack
    mov  rax, _funcToCall$[rbp]

    ; // copy stack args into regs as expected
    mov ARG_REG1,  QWORD PTR [rsp+0]
    movd xmm0, ARG_REG1
    mov ARG_REG2,  QWORD PTR [rsp+8]
    movd xmm1, ARG_REG2
    mov ARG_REG3,   QWORD PTR [rsp+16]
    movd xmm2, ARG_REG3
    mov ARG_REG4,   QWORD PTR [rsp+24]
    movd xmm3, ARG_REG4
    
    sub  rsp, 32
    call rax                              ;; // invoke the function
    add  rsp, 32
prt_bootstrapTaskAsmCall::

    goToSystemStack                       ;; // go to the original stack
    call prt_ExitThread                   ;; // exit the thread
prt_bootstrapTaskAsmEnd:
prt_bootstrapTaskAsm ENDP

; =============================================================================================

PUBLIC prt_pcallAsm                       ; start of the function

; // Stack offsets of this function's aguments.
firstArg _newEsp$

prt_pcallAsm PROC EXPORT
    fullStubProlog
    sub  rsp, 8                           ; // keep rsp 16-byte aligned
    mov  rbx, rsp
    mov  rsp, ARG_REG1
    mov  ARG_REG1, [rsp+0]
    mov  ARG_REG2, [rsp+8]
    mov  ARG_REG3, [rsp+16]
    mov  ARG_REG4, [rsp+24]
    call pcallOnSystemStack
    mov  rsp, rbx
    add  rsp, 8
    fullStubEpilog
    ret
prt_pcallAsm ENDP

; =============================================================================================

PUBLIC prtYield
PUBLIC prt_YieldStart
PUBLIC prt_YieldInvokeUnmanaged
PUBLIC prt_YieldEnd

prtYield PROC EXPORT
prt_YieldStart::
    ret
prt_YieldEnd::
prtYield ENDP

; =============================================================================================

PUBLIC prt_testStackSize                    ; start of the function

; // Stack offsets of this function's aguments.
firstArg _funcToCall$
nextArg  _stackTop$

prt_testStackSize PROC EXPORT
    fullStubProlog

    mov  rbx, rsp
    mov  rsp, ARG_REG2                    ;; // transition to a new stack

    sub  rsp, 40        
    call ARG_REG1                         ;; // invoke the function
    add  rsp, 40

    mov  rsp, rbx
    fullStubEpilog
    ret
prt_testStackSize ENDP


; =============================================================================================


; // void * __pdecl prt_getTlsRegister(void);
PUBLIC prt_getTlsRegister
PUBLIC prt_getTlsRegisterStart
PUBLIC prt_getTlsRegisterEnd

prt_getTlsRegister PROC EXPORT
prt_getTlsRegisterStart::
IFDEF TLS_REGISTER
    mov rax, tlsreg
ELSE
    mov rax, 0
ENDIF
    ret
prt_getTlsRegisterEnd::
prt_getTlsRegister ENDP

; =============================================================================================

_TEXT   ENDS

ENDIF ; // __X86_64__

end
