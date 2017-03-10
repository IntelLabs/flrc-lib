;;; Redistribution and use in source and binary forms, with or without modification, are permitted 
;;; provided that the following conditions are met:
;;; 1.   Redistributions of source code must retain the above copyright notice, this list of 
;;; conditions and the following disclaimer.
;;; 2.   Redistributions in binary form must reproduce the above copyright notice, this list of
;;; conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
;;; THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
;;; BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
;;; ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
;;; EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
;;; OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
;;; OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
;;; IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

IFNDEF __x86_64__
.586
.xmm
.model flat,c

.code

REGISTER_SIZE = 4

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

continuationProlog MACRO espOffsetFromEbp, contOffsetFromEbp
    lea esp, [edx - contOffsetFromEbp + espOffsetFromEbp]
    lea ebp, [edx - contOffsetFromEbp]
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

copyArgs MACRO argStartOffsetFromEbp, argSizeOffsetFromEbp
    mov ebx, DWORD PTR argSizeOffsetFromEbp         ;; // ebx = number of 4-byte params
    shl ebx, 2                                      ;; // ebx = size of params in bytes
    sub esp, ebx                                    ;; // reserve space for copies of the incoming arguments

    ;; // copy the arguments to the stack starting at esp.
    cld
    mov ecx, DWORD PTR argSizeOffsetFromEbp
    mov esi, DWORD PTR argStartOffsetFromEbp
    mov edi, esp
    ;; // rep movsd copies "ecx" dwords from [esi] to [edi].  cld means esi and edi are incremented after each copy.
    rep movsd
ENDM

; =========================================================================

EXTERN C prtInvokeUnmanagedFunc@16 : NEAR
EXTERN C prtPushVseForTask : NEAR
EXTERN C prtPopVseForTask : NEAR
EXTERN C prtGetTaskHandle : NEAR
EXTERN C prtYieldUnmanaged : NEAR
EXTERN C prtFatCutTo@4 : NEAR
EXTERN C gc_heap_slot_write_barrier_indirect : DWORD
EXTERN C gc_heap_slot_write_interior_indirect : DWORD
EXTERN C longjmp : NEAR
EXTERN C free : NEAR
; EXTERN C RaiseException : NEAR

IFDEF PROVIDE_P2C_WB
IFNDEF NO_INLINE_WRITE_BARRIER
EXTERN C g_tls_offset_bytes : DWORD
EXTERN C local_nursery_size : DWORD
ENDIF
ENDIF

; =========================================================================

IFDEF NO_P2C_TH
PUBLIC pillar2cInvokeUnmanagedFunc@20
ELSE
PUBLIC pillar2cInvokeUnmanagedFunc@24
ENDIF
PUBLIC pillar2cInvokeUnmanagedStart
PUBLIC pillar2cInvokeUnmanagedDestructor
IFDEF NO_P2C_TH
firstArg _prevPseudoFrame$
ELSE
firstArg _taskHandle$
nextArg _prevPseudoFrame$
ENDIF
nextArg _unmanagedFunc$
nextArg _argStart$
nextArg _argSize$
nextArg _callingConvention$
IFDEF NO_P2C_TH
pillar2cInvokeUnmanagedFunc@20 PROC EXPORT
ELSE
pillar2cInvokeUnmanagedFunc@24 PROC EXPORT
ENDIF
pillar2cInvokeUnmanagedStart::
    push ebx

IFDEF NO_P2C_TH
    call prtGetTaskHandle
	mov  ebx, eax

    ; // call Pillar runtime function
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    call prtInvokeUnmanagedFunc@16
ELSE
    ; // task handle into ebx
    mov  ebx, dword ptr [esp+8]

    ; // call Pillar runtime function
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    push dword ptr [esp+28]
    call prtInvokeUnmanagedFunc@16
ENDIF

    pop  ebx
IFDEF NO_P2C_TH
    ret  20
ELSE
    ret  24
ENDIF
pillar2cInvokeUnmanagedDestructor::
IFDEF NO_P2C_TH
pillar2cInvokeUnmanagedFunc@20 ENDP
ELSE
pillar2cInvokeUnmanagedFunc@24 ENDP
ENDIF

; =========================================================================

IFDEF NO_P2C_TH
PUBLIC pillar2cYield@4
ELSE
PUBLIC pillar2cYield@8
ENDIF
PUBLIC pillar2cYieldStart
PUBLIC pillar2cYieldDestructor
IFDEF NO_P2C_TH
firstArg _prevPseudoFrame$
pillar2cYield@4 PROC EXPORT
ELSE
firstArg _taskHandle$
nextArg _prevPseudoFrame$
pillar2cYield@8 PROC EXPORT
ENDIF
pillar2cYieldStart::
    push ebx

IFDEF NO_P2C_TH
    call prtGetTaskHandle
	mov  ebx, eax
ELSE
    ; // task handle into ebx
    mov  ebx, dword ptr [esp+8]
ENDIF

    ; // call Pillar runtime function
    push 0
    push 0
    push 0
    push prtYieldUnmanaged
    call prtInvokeUnmanagedFunc@16

    pop  ebx
IFDEF NO_P2C_TH
    ret  4
ELSE
    ret  8
ENDIF
pillar2cYieldDestructor::
IFDEF NO_P2C_TH
pillar2cYield@4 ENDP
ELSE
pillar2cYield@8 ENDP
ENDIF

; =========================================================================

PUBLIC pillar2c_pcall_target@12
PUBLIC pillar2c_pcall_target_start
PUBLIC pillar2c_pcall_target_end
firstArg _managedFunc$
nextArg  _argStart$
nextArg  _argSize$
pillar2c_pcall_target@12 PROC EXPORT
pillar2c_pcall_target_start::
    fullStubProlog

    copyArgs _argStart$[ebp], _argSize$[ebp]
	push _argStart$[ebp]
	call free                                ; // argStart should be a malloc'ed copy of the arguments
	add  esp, 4
    push 0                                   ; // a NULL to root the pseudo-frame stack
IFNDEF NO_P2C_TH
    call prtGetTaskHandle
    push eax                                 ; // task handle is the first arg to all managed methods
ENDIF
    call DWORD PTR _managedFunc$[ebp]        ; // managedFunc should remove all the args

    fullStubEpilog
    ret  12
pillar2c_pcall_target_end::
pillar2c_pcall_target@12 ENDP

; =========================================================================

PUBLIC _pillar2c_continuation_target
_pillar2c_continuation_target PROC EXPORT
    add edx,8
    push 1
    push edx
    call longjmp
_pillar2c_continuation_target ENDP

IFNDEF RaiseException
    RaiseException  PROTO   STDCALL  dwExceptionCode:DWORD, dwExceptionFlags:DWORD , nNumberOfArguments:DWORD, lpArguments:PTR DWORD
ENDIF

PUBLIC _pillar2c_RaiseException_continuation_target
_pillar2c_RaiseException_continuation_target PROC EXPORT
    push edx
    mov  edx, esp
;    push esp
;    push 1
;    push 0
;    push 1
;    call RaiseException
    INVOKE  RaiseException, 1, 0, 1, edx
_pillar2c_RaiseException_continuation_target ENDP

; =========================================================================

_TEXT ENDS

ELSE  ; // __x86_64__

; =========================================================================

REGISTER_SIZE = 8

; =========================================================================

_TEXT ENDS

ENDIF ; // __x86_64__

end
