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

%ifndef __x86_64__

SECTION .text

extern unmanaged_add_entry
extern unmanaged_add_entry_interior
extern unmanaged_mark_phase
extern prtInvokeUnmanagedFunc
extern g_tls_offset_bytes 
extern local_nursery_size
extern add_gen_rs





global gc_heap_slot_write_ref_p
global gc_heap_slot_write_ref_p_nonconcurrent_section
global gc_heap_slot_write_ref_p_end
gc_heap_slot_write_ref_p:
    mov eax, dword [esp+8]             ; // p_slot into eax

    mov ecx, ebx                       ; // Pillar task * into ecx
%ifndef TLS0
    add ecx, 4                         ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov ecx, dword [ecx]               ; // TLS value (GC_Thread_Info*) into ecx
    add ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb done

    mov eax, dword [esp+12]            ; // value into eax
    and eax, 3                         ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp eax, 0                         ; // compare two lower bits against zero
    jnz done                           ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    lea edx, [esp+12]                  ; // pointer to new value in edx
    mov eax, dword [esp+8]             ; // p_slot into eax

    push eax
    push 0                             ; // non-interior so offset of 0
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4                             ; // 4 args
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 12
%endif ; CONCURRENT
gc_heap_slot_write_ref_p_nonconcurrent_section:

    mov edx, dword [esp+12]            ; // value into edx
    ; // return if value is outside the nursery
    sub edx, dword [ecx+8]
    cmp edx, dword [local_nursery_size]
    jae done

    lea edx, [esp+12]
    mov  eax, dword [esp+8]
    push eax                           ; // push the p_slot
    mov  eax, dword [esp+8]
    push eax                           ; // push the base
    push edx                           ; // push a pointer to the value
    push ecx                           ; // push the GC thread

    mov eax, esp
    push 0
    push 4
    push eax
    mov eax, unmanaged_add_entry
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

done:
    ; // write value into *p_slot
    mov eax, dword [esp+8]       ; // p_slot into eax
    mov edx, dword [esp+12]      ; // value into edx
    mov dword [eax], edx         ; // *p_slot = value

    ret 12
gc_heap_slot_write_ref_p_end:

; -----------------------------------------------------------------------

global gc_heap_slot_write_interior_ref_p
global gc_heap_slot_write_interior_ref_p_nonconcurrent_section
global gc_heap_slot_write_interior_ref_p_end
gc_heap_slot_write_interior_ref_p:
    mov eax, dword [esp+4]       ; // p_slot into eax

    mov ecx, ebx                     ; // Pillar task * into ecx
%ifndef TLS0
    add ecx, 4                       ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov ecx, dword [ecx]         ; // TLS value (GC_Thread_Info*) into ecx
    add ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb interior_done

%ifdef CONCURRENT
    mov eax, dword [esp+4]      ; // p_slot into eax
    lea edx, [esp+8]            ; // value into edx

    push eax
    mov eax, dword [esp+16]     ; // offset
    push eax
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 12
%endif ; CONCURRENT
gc_heap_slot_write_interior_ref_p_nonconcurrent_section:

    mov edx, dword [esp+8]      ; // value into edx
    ; // return if value is outside the nursery
    sub edx, dword [ecx+8]
    cmp edx, dword [local_nursery_size]
    jae interior_done

    lea edx, [esp+8]
        push dword [esp+12]
    push edx
    push ecx

        mov eax, esp
    push 0
    push 3
    push eax
    mov eax, unmanaged_add_entry_interior
    push eax
        call prtInvokeUnmanagedFunc
    add esp, 12
interior_done:
    ; // write value into *p_slot
    mov eax, dword [esp+4]       ; // p_slot into eax
    mov edx, dword [esp+8]       ; // value into edx
    mov dword [eax], edx         ; // *p_slot = value
    ret 12
gc_heap_slot_write_interior_ref_p_end:

; -----------------------------------------------------------------------

global gc_heap_slot_write_ref_p_prt
global gc_heap_slot_write_ref_p_prt_nonconcurrent_section:
global gc_heap_slot_write_ref_p_prt_end
gc_heap_slot_write_ref_p_prt:
    mov  eax, dword [esp+8]                ; // p_slot into eax

    mov  ecx, dword [esp+16]               ; // Pillar task * into ecx
%ifndef TLS0
    add  ecx, 4                            ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  ecx, dword [ecx]                  ; // TLS value (GC_Thread_Info*) into ecx
    add  ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb   prt_done

    mov  eax, dword [esp+12]               ; // value into eax
    and  eax, 3                            ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp  eax, 0                            ; // compare two lower bits against zero
    jnz  prt_done                              ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    lea  edx, [esp+12]                     ; // pointer to new value in edx
    mov  eax, dword [esp+8]                ; // p_slot into eax

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    push 0                                 ; // non-interior so offset of 0
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4                                 ; // 4 args
    push eax
    mov  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  20
%endif ; CONCURRENT
gc_heap_slot_write_ref_p_prt_nonconcurrent_section:

    mov edx, dword [esp+12]      ; // value into edx
        ; // return if value is outside the nursery
    sub edx, dword [ecx+8]
    cmp edx, dword [local_nursery_size]
    jae prt_done

    push ebx
    mov  ebx, dword [esp+20]
    lea  edx, [esp+16]
    mov  eax, dword [esp+12]
    push eax                          ; // push the p_slot
    mov  eax, dword [esp+12]
    push eax                          ; // push the base
    push edx                          ; // push a pointer to the value
    push ecx                          ; // push the GC thread

    mov eax, esp
    push 0
    push 4
    push eax
    mov  eax, unmanaged_add_entry
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

prt_done:
    ; // write value into *p_slot
    mov  eax, dword [esp+8]       ; // p_slot into eax
    mov  edx, dword [esp+12]      ; // value into edx
    mov  dword [eax], edx         ; // *p_slot = value

    ret  20
gc_heap_slot_write_ref_p_prt_end:

; -----------------------------------------------------------------------

global gc_heap_slot_write_interior_ref_p_prt
global gc_heap_slot_write_interior_ref_p_prt_nonconcurrent_section
global gc_heap_slot_write_interior_ref_p_prt_end
gc_heap_slot_write_interior_ref_p_prt:
    mov  eax, dword [esp+4]               ; // p_slot into eax

    mov  ecx, dword [esp+16]
%ifndef TLS0
    add  ecx, 4                           ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov ecx, dword [ecx]                  ; // TLS value (GC_Thread_Info*) into ecx
    add ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb prt_interior_done

%ifdef CONCURRENT
    mov eax, dword [esp+4]                ; // p_slot into eax
    lea edx, [esp+8]                      ; // value into edx

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    mov  eax, dword [esp+20]              ; // offset
    push eax
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4
    push eax
    mov  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  20
%endif ; CONCURRENT
gc_heap_slot_write_interior_ref_p_prt_nonconcurrent_section:

    mov  edx, dword [esp+8]      ; // value into edx
        ; // return if value is outside the nursery
    sub  edx, dword [ecx+8]
    cmp  edx, dword [local_nursery_size]
    jae  prt_interior_done

    push ebx
    mov  ebx, dword [esp+20]
    lea  edx, [esp+12]
    push dword [esp+16]
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 3
    push eax
    mov  eax, unmanaged_add_entry_interior
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 12
    pop  ebx

prt_interior_done:
    ; // write value into *p_slot
    mov  eax, dword [esp+4]       ; // p_slot into eax
    mov  edx, dword [esp+8]       ; // value into edx
    mov  dword [eax], edx         ; // *p_slot = value
    ret  20
gc_heap_slot_write_interior_ref_p_prt_end:











; -----------------------------------------------------------------------
; --------------------------- CAS versions ------------------------------
; -----------------------------------------------------------------------

global gc_cas_write_ref_p
global gc_cas_write_ref_p_nonconcurrent_section
global gc_cas_write_ref_p_end
gc_cas_write_ref_p:
    mov eax, dword [esp+8]             ; // p_slot into eax

    mov ecx, ebx                       ; // Pillar task * into ecx
%ifndef TLS0
    add ecx, 4                         ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov ecx, dword [ecx]               ; // TLS value (GC_Thread_Info*) into ecx
    add ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb cas_done

    mov eax, dword [esp+12]            ; // value into eax
    and eax, 3                         ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp eax, 0                         ; // compare two lower bits against zero
    jnz cas_done                           ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    lea edx, [esp+12]                  ; // pointer to new value in edx
    mov eax, dword [esp+8]             ; // p_slot into eax

    push eax
    push 0                             ; // non-interior so offset of 0
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4                             ; // 4 args
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 16
%endif ; CONCURRENT
gc_cas_write_ref_p_nonconcurrent_section:

    mov edx, dword [esp+12]            ; // value into edx
        ; // return if value is outside the nursery
    sub edx, dword [ecx+8]
    cmp edx, dword [local_nursery_size]
    jae cas_done

    lea edx, [esp+12]
    mov  eax, dword [esp+8]
    push eax                           ; // push the p_slot
    mov  eax, dword [esp+8]
    push eax                           ; // push the base
    push edx                           ; // push a pointer to the value
    push ecx                           ; // push the GC thread

    mov eax, esp
    push 0
    push 4
    push eax
    mov eax, unmanaged_add_entry
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

cas_done:
    ; // write value into *p_slot
    mov  ecx, dword [esp+8]       ; // p_slot into ecx
    mov  edx, dword [esp+12]      ; // value into edx
    mov  eax, dword [esp+16]      ; // comperand into eax
    lock cmpxchg [ecx], edx

    ret 16
gc_cas_write_ref_p_end:

; -----------------------------------------------------------------------

global gc_cas_write_interior_ref_p
global gc_cas_write_interior_ref_p_nonconcurrent_section
global gc_cas_write_interior_ref_p_end
gc_cas_write_interior_ref_p:
    mov eax, dword [esp+4]       ; // p_slot into eax

    mov ecx, ebx                     ; // Pillar task * into ecx
%ifndef TLS0
    add ecx, 4                       ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov ecx, dword [ecx]         ; // TLS value (GC_Thread_Info*) into ecx
    add ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb cas_interior_done

%ifdef CONCURRENT
    mov eax, dword [esp+4]      ; // p_slot into eax
    lea edx, [esp+8]            ; // value into edx

    push eax
    mov eax, dword [esp+16]     ; // offset
    push eax
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 16
%endif ; CONCURRENT
gc_cas_write_interior_ref_p_nonconcurrent_section:

    mov edx, dword [esp+8]      ; // value into edx
    ; // return if value is outside the nursery
    sub edx, dword [ecx+8]
    cmp edx, dword [local_nursery_size]
    jae cas_interior_done

    lea edx, [esp+8]
    push dword [esp+12]
    push edx
    push ecx

    mov eax, esp
    push 0
    push 3
    push eax
    mov eax, unmanaged_add_entry_interior
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 12
cas_interior_done:
    ; // write value into *p_slot
    mov  ecx, dword [esp+4]       ; // p_slot into ecx
    mov  edx, dword [esp+8]       ; // value into edx
    mov  eax, dword [esp+16]      ; // comperand into eax
    lock cmpxchg [ecx], edx

    ret 16
gc_cas_write_interior_ref_p_end:

; -----------------------------------------------------------------------

global gc_cas_write_ref_p_prt
global gc_cas_write_ref_p_prt_nonconcurrent_section
global gc_cas_write_ref_p_prt_end
gc_cas_write_ref_p_prt:
    mov  eax, dword [esp+8]                ; // p_slot into eax

    mov  ecx, dword [esp+16]               ; // Pillar task * into ecx
%ifndef TLS0
    add  ecx, 4                            ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  ecx, dword [ecx]                  ; // TLS value (GC_Thread_Info*) into ecx
    add  ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb   cas_prt_done

    mov  eax, dword [esp+12]               ; // value into eax
    and  eax, 3                            ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp  eax, 0                            ; // compare two lower bits against zero
    jnz  cas_prt_done                              ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    lea  edx, [esp+12]                     ; // pointer to new value in edx
    mov  eax, dword [esp+8]                ; // p_slot into eax

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    push 0                                 ; // non-interior so offset of 0
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4                                 ; // 4 args
    push eax
    mov  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  24
%endif ; CONCURRENT
gc_cas_write_ref_p_prt_nonconcurrent_section:

    mov edx, dword [esp+12]      ; // value into edx
        ; // return if value is outside the nursery
    sub edx, dword [ecx+8]
    cmp edx, dword [local_nursery_size]
    jae cas_prt_done

    push ebx
    mov  ebx, dword [esp+20]
    lea  edx, [esp+16]
    mov  eax, dword [esp+12]
    push eax                          ; // push the p_slot
    mov  eax, dword [esp+12]
    push eax                          ; // push the base
    push edx                          ; // push a pointer to the value
    push ecx                          ; // push the GC thread

    mov eax, esp
    push 0
    push 4
    push eax
    mov  eax, unmanaged_add_entry
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

cas_prt_done:
    ; // write value into *p_slot
    mov  ecx, dword [esp+8]       ; // p_slot into ecx
    mov  edx, dword [esp+12]      ; // value into edx
    mov  eax, dword [esp+16]      ; // comperand into eax
    lock cmpxchg [ecx], edx

    ret  24
gc_cas_write_ref_p_prt_end:

; -----------------------------------------------------------------------

global gc_cas_write_interior_ref_p_prt
global gc_cas_write_interior_ref_p_prt_nonconcurrent_section
global gc_cas_write_interior_ref_p_prt_end
gc_cas_write_interior_ref_p_prt:
    mov  eax, dword [esp+4]               ; // p_slot into eax

    mov  ecx, dword [esp+16]
%ifndef TLS0
    add  ecx, 4                           ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov ecx, dword [ecx]                  ; // TLS value (GC_Thread_Info*) into ecx
    add ecx, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub eax, dword [ecx+8]                ; // ecx+8 is the start field in local nursery
    cmp eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb cas_prt_interior_done

%ifdef CONCURRENT
    mov eax, dword [esp+4]                ; // p_slot into eax
    lea edx, [esp+8]                      ; // value into edx

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    mov  eax, dword [esp+20]              ; // offset
    push eax
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4
    push eax
    mov  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  24
%endif ; CONCURRENT
gc_cas_write_interior_ref_p_prt_nonconcurrent_section:

    mov  edx, dword [esp+8]      ; // value into edx
    ; // return if value is outside the nursery
    sub  edx, dword [ecx+8]
    cmp  edx, dword [local_nursery_size]
    jae  cas_prt_interior_done

    push ebx
    mov  ebx, dword [esp+20]
    lea  edx, [esp+12]
    push dword [esp+16]
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 3
    push eax
    mov  eax, unmanaged_add_entry_interior
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 12
    pop  ebx

cas_prt_interior_done:
    ; // write value into *p_slot
    mov  ecx, dword [esp+4]       ; // p_slot into ecx
    mov  edx, dword [esp+8]       ; // value into edx
    mov  eax, dword [esp+16]      ; // comperand into eax
    lock cmpxchg [ecx], edx

    ret  24
gc_cas_write_interior_ref_p_prt_end:



; -----------------------------------------------------------------------
; ------------ GENERATIONAL versions ------------------------------------
; -----------------------------------------------------------------------

global gc_heap_slot_gen_write_ref_p_prt
global gc_heap_slot_gen_write_ref_p_prt_end
gc_heap_slot_gen_write_ref_p_prt:
    ; esp+4  = base;
	; esp+8  = p_slot
	; esp+12 = value
	; esp+16 = taskHandle
	; esp+20 = prev_frame
	mov eax, dword [esp+8]           ; // p_slot into eax
	mov ecx, eax
	and ecx, 0FFff0000h
	mov ecx, [ecx]
	cmp ecx, 0
	jz  gc_heap_slot_gen_write_ref_p_prt_done

	mov  edx, dword [esp+12]          ; // value into eax
	mov  ecx, edx
	and  ecx, 0FFff0000h              ; // pointer to start of GC block
	mov  ecx, [ecx]                   ; // value of thread_owner field...0 is young gen, 1 is old
	cmp  ecx, 0
	jnz  gc_heap_slot_gen_write_ref_p_prt_done

	push eax                          ; // push slot
	push edx                          ; // push value
	mov  ecx, dword [esp+24]          ; // Pillar task * into ecx
	add  ecx, 4                       ; // Go to TLS field in PrtTask
	mov  ecx, dword [ecx]             ; // TLS value (GC_Thread_Info*) into ecx
    add  ecx, g_tls_offset_bytes
	push ecx
	call add_gen_rs
	add  esp, 12                      ; // bypass thread pointer and base args

gc_heap_slot_gen_write_ref_p_prt_done:
	; // write value into *p_slot
	mov  eax, dword [esp+8]           ; // p_slot into eax
	mov  edx, dword [esp+12]          ; // value into edx
	mov  dword [eax], edx             ; // *p_slot = value

	ret 20
gc_heap_slot_gen_write_ref_p_prt_end:

; -----------------------------------------------------------------------

global gc_heap_slot_gen_write_interior_ref_p_prt
global gc_heap_slot_gen_write_interior_ref_p_prt_end
gc_heap_slot_gen_write_interior_ref_p_prt:
    ; esp+4  = p_slot
	; esp+8  = value
	; esp+12 = offset
	; esp+16 = taskHandle
	; esp+20 = prev_frame
	mov eax, dword [esp+4]            ; // p_slot into eax
	mov ecx, eax
	and ecx, 0FFff0000h
	mov ecx, [ecx]
	cmp ecx, 0
	jz  gc_heap_slot_gen_write_interior_ref_p_prt_done

	mov  edx, dword [esp+8]           ; // value into eax
	mov  ecx, edx
	and  ecx, 0FFff0000h              ; // pointer to start of GC block
	mov  ecx, [ecx]                   ; // value of thread_owner field...0 is young gen, 1 is old
	cmp  ecx, 0
	jnz  gc_heap_slot_gen_write_interior_ref_p_prt_done

	push eax                          ; // push slot
	push edx                          ; // push value
	mov  ecx, dword [esp+24]          ; // Pillar task * into ecx
	add  ecx, 4                       ; // Go to TLS field in PrtTask
	mov  ecx, dword [ecx]             ; // TLS value (GC_Thread_Info*) into ecx
    add  ecx, g_tls_offset_bytes
	push ecx
	call add_gen_rs
	add  esp, 12                      ; // bypass thread pointer and base args

gc_heap_slot_gen_write_interior_ref_p_prt_done:
	; // write value into *p_slot
	mov  eax, dword [esp+8]           ; // p_slot into eax
	mov  edx, dword [esp+12]          ; // value into edx
	mov  dword [eax], edx             ; // *p_slot = value

	ret 20
gc_heap_slot_gen_write_interior_ref_p_prt_end:














; -----------------------------------------------------------------------

%else  ; // __x86_64__

%define REGISTER_SIZE 8

extern unmanaged_add_entry
extern unmanaged_add_entry_interior
extern unmanaged_mark_phase
extern prtInvokeUnmanagedFunc
extern get_m2u_vse_size
extern tgc_enter_unmanaged
extern tgc_reenter_managed
extern g_tls_offset_bytes
extern local_nursery_size





%define _base$ rdi
%define _slot$ rsi
%define _value$ rdx
%define _baseStack$  1*REGISTER_SIZE
%define _slotStack$  2*REGISTER_SIZE
%define _valueStack$ 3*REGISTER_SIZE

global gc_heap_slot_write_ref_p
global gc_heap_slot_write_ref_p_end
gc_heap_slot_write_ref_p:
    mov  rax, _slot$                   ; // p_slot into rax

    mov  r10, rbx                      ; // Pillar task * into rcx
%ifndef TLS0
    add  r10, REGISTER_SIZE            ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10,  qword [r10]             ; // TLS value (GC_Thread_Info*) into r10
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]  ; // r10+2*REGISTER_SIZE is the start field in local nursery
    cmp  eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb   done

    mov  rax, _value$                  ; // value into rax
    and  rax, 3                        ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp  rax, 0                        ; // compare two lower bits against zero
    jnz  done                          ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    lea edx, [esp+12]                  ; // pointer to new value in edx
    mov eax, dword [esp+8]             ; // p_slot into eax

    push eax
    push 0                             ; // non-interior so offset of 0
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4                             ; // 4 args
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 12
normal_mode:
%endif ; CONCURRENT

    mov  r11, _value$                  ; // value into edx
    ; // return if value is outside the nursery
    sub  r11,  qword [r10+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  done

    mov  qword [rsp+_baseStack$], _base$    ; // save away args to this function in their stack locations
    mov  qword [rsp+_slotStack$], _slot$
    mov  qword [rsp+_valueStack$], _value$

    jmp  0 ; // this code is totally broken!!!!!
    mov  rcx, r10                                ; // arg 1, GC thread
    lea  rdx, [rsp+_valueStack$]                 ; // arg 2, pointer to the value
    mov  r8,  _base$                             ; // arg 3, the base
    push r8
    push rdx
    push rcx

    mov  rcx, unmanaged_add_entry
    mov  rdx, rsp
    mov  r8,  3
    mov  r9,  0
    sub  rsp, 4*REGISTER_SIZE

    call prtInvokeUnmanagedFunc

    add  rsp, 7*REGISTER_SIZE

    mov  _slot$, qword [rsp+_slotStack$]
    mov  _value$,  qword [rsp+_valueStack$]

done:
    ; // write value into *p_slot
    mov  qword [_slot$], _value$      ; // *p_slot = value
    ret
gc_heap_slot_write_ref_p_end:

; -----------------------------------------------------------------------

%define _slot$ rdi
%define _value$ rsi
%define _offset$ rdx
%define _slotStack$   1*REGISTER_SIZE
%define _valueStack$  2*REGISTER_SIZE
%define _offsetStack$ 3*REGISTER_SIZE

global gc_heap_slot_write_interior_ref_p
global gc_heap_slot_write_interior_ref_p_end
gc_heap_slot_write_interior_ref_p:
    mov  rax, _slot$                  ; // p_slot into eax

    mov  r10, rbx                     ; // Pillar task * into ecx
%ifndef TLS0
    add  r10, REGISTER_SIZE           ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10, qword [r10]             ; // TLS value (GC_Thread_Info*) into ecx
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]  ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb   interior_done

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    mov eax, dword [esp+4]      ; // p_slot into eax
    lea edx, [esp+8]            ; // value into edx

    push eax
    mov eax, dword [esp+16]     ; // offset
    push eax
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 12
normal_mode:
%endif ; CONCURRENT

    mov  r11, _value$                  ; // value into edx
        ; // return if value is outside the nursery
    sub  r11, qword [r10+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  interior_done

    mov  qword [rsp+_slotStack$], _slot$    ; // save away args to this function in their stack locations
    mov  qword [rsp+_valueStack$], _value$
    mov  qword [rsp+_offsetStack$], _offset$

    mov  rcx, r10
    lea  rdx, [rsp+_valueStack$]
    mov  r8, _offset$
    push r8
    push rdx
    push rcx

    mov  rcx, unmanaged_add_entry_interior
    mov  rdx, rsp
    mov  r8,  3
    mov  r9,  0
    sub  rsp, 4*REGISTER_SIZE

    call prtInvokeUnmanagedFunc

    add  rsp, 7*REGISTER_SIZE

    mov  _slot$, qword [rsp+_slotStack$]
    mov  _value$, qword [rsp+_valueStack$]

interior_done:
    ; // write value into *p_slot
    mov  qword [_slot$], _value$
    ret
gc_heap_slot_write_interior_ref_p_end:

; -----------------------------------------------------------------------

%define _base$ rdi
%define _slot$ rsi
%define _value$ rdx
%define _task$ rcx
%define _prev$ r8
%define _baseStack$  1*REGISTER_SIZE
%define _slotStack$  2*REGISTER_SIZE
%define _valueStack$ 3*REGISTER_SIZE
%define _taskStack$  4*REGISTER_SIZE
%define _prevStack$  5*REGISTER_SIZE

global gc_heap_slot_write_ref_p_prt
global gc_heap_slot_write_ref_p_prt_end
gc_heap_slot_write_ref_p_prt:
    mov  rax, _slot$                             ; // p_slot into eax

    mov  r10, _task$                             ; // Pillar task * into ecx
%ifndef TLS0
    add  r10, REGISTER_SIZE                      ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10, qword [r10]                        ; // TLS value (GC_Thread_Info*) into ecx
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]        ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]         ; // go to done if p_slot is in the local nursery
    jb   prt_done

    mov  rax, _value$                            ; // value into eax
    and  rax, 3                                  ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp  rax, 0                                  ; // compare two lower bits against zero
    jnz  prt_done                                ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    lea  edx, [esp+12]                           ; // pointer to new value in edx
    mov  eax, dword [esp+8]                      ; // p_slot into eax

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    push 0                                       ; // non-interior so offset of 0
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4                                       ; // 4 args
    push eax
    lea  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  20
normal_mode:
%endif ; CONCURRENT

    mov  r11,  _value$                           ; // value into edx
        ; // return if value is outside the nursery
    sub  r11,  qword [r10+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  prt_done

    mov  r13, r10                               ; GC_Thread_Info *
    push rdi
    push rsi
    push rdx
    push rcx
    push r8

    call get_m2u_vse_size
    sub  rsp, rax
    mov  r12, rax

    mov  rdi, rsp
    mov  rsi, r8
    mov  rdx, rcx
    call tgc_enter_unmanaged

    mov  rdi, r13
    lea  rsi, [rsp+r12+16]
    mov  rdx, qword [rsp+r12+32]
    mov  rcx, qword [rsp+r12+24]
    call unmanaged_add_entry

    mov  rdi, qword [rsp+r12+8]
    call tgc_reenter_managed

    add  rsp, r12

    pop  r8
    pop  rcx
    pop  rdx
    pop  rsi
    pop  rdi
prt_done:
    ; // write value into *p_slot
    mov qword [_slot$], _value$      ; // *p_slot = value
    ret
gc_heap_slot_write_ref_p_prt_end:

; -----------------------------------------------------------------------

%define _slot$ rcx
%define _value$ rdx
%define _offset$ r8
%define _task$ r9
%define _slotStack$   1*REGISTER_SIZE
%define _valueStack$  2*REGISTER_SIZE
%define _offsetStack$ 3*REGISTER_SIZE
%define _taskStack$   4*REGISTER_SIZE
%define _prevStack$   5*REGISTER_SIZE

global gc_heap_slot_write_interior_ref_p_prt
global gc_heap_slot_write_interior_ref_p_prt_end
gc_heap_slot_write_interior_ref_p_prt:
    mov  rax, _slot$                           ; // p_slot into eax

    mov  r10, _task$
%ifndef TLS0
    add  r10, REGISTER_SIZE                    ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10, qword [r10]                      ; // TLS value (GC_Thread_Info*) into ecx
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]      ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]       ; // go to done if p_slot is in the local nursery
    jb   prt_interior_done

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    mov eax, dword [esp+4]                ; // p_slot into eax
    lea edx, [esp+8]                      ; // value into edx

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    mov  eax, dword [esp+20]              ; // offset
    push eax
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4
    push eax
    lea  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  20
normal_mode:
%endif ; CONCURRENT

    mov  r11, _value$                  ; // value into edx
        ; // return if value is outside the nursery
    sub  r11, qword [rcx+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  prt_interior_done

    mov  qword [rsp+_slotStack$], _slot$    ; // save away args to this function in their stack locations
    mov  qword [rsp+_valueStack$], _value$
    mov  qword [rsp+_offsetStack$], _offset$

    push rbx
    mov  rbx, _task$

    mov  rcx, r10
    lea  rdx, [rsp+_valueStack$]
    mov  r8,  _offset$
    push r8
    push rdx
    push rcx

    mov  rcx, unmanaged_add_entry_interior
    mov  rdx, rsp
    mov  r8,  3
    mov  r9,  0
    sub  rsp, 4*REGISTER_SIZE

    call prtInvokeUnmanagedFunc

    add  rsp, 7*REGISTER_SIZE

    mov  _slot$, qword [rsp+_slotStack$]
    mov  _value$, qword [rsp+_valueStack$]

    pop  rbx
prt_interior_done:
    ; // write value into *p_slot
    mov  qword [_slot$], _value$   ; // *p_slot = value
    ret
gc_heap_slot_write_interior_ref_p_prt_end:













%define _base$ rdi
%define _slot$ rsi
%define _value$ rdx
%define _cmp$ rcx
%define _baseStack$  1*REGISTER_SIZE
%define _slotStack$  2*REGISTER_SIZE
%define _valueStack$ 3*REGISTER_SIZE
%define _cmpStack$   4*REGISTER_SIZE

global gc_cas_write_ref_p
global gc_cas_write_ref_p_end
gc_cas_write_ref_p:
    mov  rax, _slot$                   ; // p_slot into rax

    mov  r10, rbx                      ; // Pillar task * into rcx
%ifndef TLS0
    add  r10, REGISTER_SIZE            ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10,  qword [r10]             ; // TLS value (GC_Thread_Info*) into r10
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]  ; // r10+2*REGISTER_SIZE is the start field in local nursery
    cmp  eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb   cas_done

    mov  rax, _value$                  ; // value into rax
    and  rax, 3                        ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp  rax, 0                        ; // compare two lower bits against zero
    jnz  cas_done                          ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    lea edx, [esp+12]                  ; // pointer to new value in edx
    mov eax, dword [esp+8]             ; // p_slot into eax

    push eax
    push 0                             ; // non-interior so offset of 0
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4                             ; // 4 args
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 12
normal_mode:
%endif ; CONCURRENT

    mov  r11, _value$                  ; // value into edx
    ; // return if value is outside the nursery
    sub  r11,  qword [r10+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  cas_done

    mov  qword [rsp+_baseStack$], _base$    ; // save away args to this function in their stack locations
    mov  qword [rsp+_slotStack$], _slot$
    mov  qword [rsp+_valueStack$], _value$

    jmp  0 ; // this code is totally broken!!!!!
    mov  rcx, r10                                ; // arg 1, GC thread
    lea  rdx, [rsp+_valueStack$]                 ; // arg 2, pointer to the value
    mov  r8,  _base$                             ; // arg 3, the base
    push r8
    push rdx
    push rcx

    mov  rcx, unmanaged_add_entry
    mov  rdx, rsp
    mov  r8,  3
    mov  r9,  0
    sub  rsp, 4*REGISTER_SIZE

    call prtInvokeUnmanagedFunc

    add  rsp, 7*REGISTER_SIZE

    mov  _slot$, qword [rsp+_slotStack$]
    mov  _value$,  qword [rsp+_valueStack$]

cas_done:
    ; // write value into *p_slot
    mov  rax, _cmp$
    lock cmpxchg [_slot$], _value$      ; // *p_slot = value
    ret
gc_cas_write_ref_p_end:

; -----------------------------------------------------------------------

%define _slot$ rdi
%define _value$ rsi
%define _offset$ rdx
%define _cmp$ rcx
%define _slotStack$   1*REGISTER_SIZE
%define _valueStack$  2*REGISTER_SIZE
%define _offsetStack$ 3*REGISTER_SIZE
%define _cmpStack$    4*REGISTER_SIZE

global gc_cas_write_interior_ref_p
global gc_cas_write_interior_ref_p_end
gc_cas_write_interior_ref_p:
    mov  rax, _slot$                  ; // p_slot into eax

    mov  r10, rbx                     ; // Pillar task * into ecx
%ifndef TLS0
    add  r10, REGISTER_SIZE           ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10, qword [r10]             ; // TLS value (GC_Thread_Info*) into ecx
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]  ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]   ; // go to done if p_slot is in the local nursery
    jb   cas_interior_done

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    mov eax, dword [esp+4]      ; // p_slot into eax
    lea edx, [esp+8]            ; // value into edx

    push eax
    mov eax, dword [esp+16]     ; // offset
    push eax
    push edx
    push ecx

    mov eax, esp
    push 0
    push 4
    push eax
    mov eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add esp, 16

    ret 12
normal_mode:
%endif ; CONCURRENT

    mov  r11, _value$                  ; // value into edx
        ; // return if value is outside the nursery
    sub  r11, qword [r10+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  cas_interior_done

    mov  qword [rsp+_slotStack$], _slot$    ; // save away args to this function in their stack locations
    mov  qword [rsp+_valueStack$], _value$
    mov  qword [rsp+_offsetStack$], _offset$

    mov  rcx, r10
    lea  rdx, [rsp+_valueStack$]
    mov  r8, _offset$
    push r8
    push rdx
    push rcx

    mov  rcx, unmanaged_add_entry_interior
    mov  rdx, rsp
    mov  r8,  3
    mov  r9,  0
    sub  rsp, 4*REGISTER_SIZE

    call prtInvokeUnmanagedFunc

    add  rsp, 7*REGISTER_SIZE

    mov  _slot$, qword [rsp+_slotStack$]
    mov  _value$, qword [rsp+_valueStack$]

cas_interior_done:
    ; // write value into *p_slot
    mov  rax, _cmp$
    lock cmpxchg [_slot$], _value$      ; // *p_slot = value
    ret
gc_cas_write_interior_ref_p_end:

; -----------------------------------------------------------------------

%define _base$ rdi
%define _slot$ rsi
%define _value$ rdx
%define _cmp$ rcx
%define _task$ r8
%define _prev$ r9
%define _baseStack$  1*REGISTER_SIZE
%define _slotStack$  2*REGISTER_SIZE
%define _valueStack$ 3*REGISTER_SIZE
%define _cmpStack$   4*REGISTER_SIZE
%define _taskStack$  5*REGISTER_SIZE
%define _prevStack$  6*REGISTER_SIZE

global gc_cas_write_ref_p_prt
global gc_cas_write_ref_p_prt_end
gc_cas_write_ref_p_prt:
    mov  rax, _slot$                             ; // p_slot into eax

    mov  r10, _task$                             ; // Pillar task * into ecx
%ifndef TLS0
    add  r10, REGISTER_SIZE                      ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10, qword [r10]                        ; // TLS value (GC_Thread_Info*) into ecx
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]        ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]         ; // go to done if p_slot is in the local nursery
    jb   cas_prt_done

    mov  rax, _value$                            ; // value into eax
    and  rax, 3                                  ; // check if this is normal pointer (two lower bits 0) or a tagged pointer
    cmp  rax, 0                                  ; // compare two lower bits against zero
    jnz  cas_prt_done                                ; // if one of them isn't zero it is a tagged rational and no barrier needed for these

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    lea  edx, [esp+12]                           ; // pointer to new value in edx
    mov  eax, dword [esp+8]                      ; // p_slot into eax

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    push 0                                       ; // non-interior so offset of 0
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4                                       ; // 4 args
    push eax
    lea  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  20
normal_mode:
%endif ; CONCURRENT

    mov  r11,  _value$                           ; // value into edx
        ; // return if value is outside the nursery
    sub  r11,  qword [r10+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  cas_prt_done

    mov  r13, r10                               ; GC_Thread_Info *
    push rdi
    push rsi
    push rdx
    push rcx
    push r8

    call get_m2u_vse_size
    sub  rsp, rax
    mov  r12, rax

    mov  rdi, rsp
    mov  rsi, r8
    mov  rdx, rcx
    call tgc_enter_unmanaged

    mov  rdi, r13
    lea  rsi, [rsp+r12+16]
    mov  rdx, qword [rsp+r12+32]
    mov  rcx, qword [rsp+r12+24]
    call unmanaged_add_entry

    mov  rdi, qword [rsp+r12+8]
    call tgc_reenter_managed

    add  rsp, r12

    pop  r8
    pop  rcx
    pop  rdx
    pop  rsi
    pop  rdi
cas_prt_done:
    ; // write value into *p_slot
    mov  rax, _cmp$
    lock cmpxchg [_slot$], _value$      ; // *p_slot = value
    ret
gc_cas_write_ref_p_prt_end:

; -----------------------------------------------------------------------

%define _slot$ rdi
%define _value$ rsi
%define _offset$ rdx
%define _cmp$ rcx
%define _task$ r8
%define _prev$ r9
%define _slotStack$   1*REGISTER_SIZE
%define _valueStack$  2*REGISTER_SIZE
%define _offsetStack$ 3*REGISTER_SIZE
%define _cmpStack$    4*REGISTER_SIZE
%define _taskStack$   5*REGISTER_SIZE
%define _prevStack$   6*REGISTER_SIZE

global gc_cas_write_interior_ref_p_prt
global gc_cas_write_interior_ref_p_prt_end
gc_cas_write_interior_ref_p_prt:
    mov  rax, _slot$                           ; // p_slot into eax

    mov  r10, _task$
%ifndef TLS0
    add  r10, REGISTER_SIZE                    ; // Go to TLS field in PrtTask
%endif ; TLS0
    mov  r10, qword [r10]                      ; // TLS value (GC_Thread_Info*) into ecx
    add  r10d, dword [g_tls_offset_bytes]

    ; // return if slot is inside the nursery
    sub  rax, qword [r10+2*REGISTER_SIZE]      ; // ecx+8 is the start field in local nursery
    cmp  eax, dword [local_nursery_size]       ; // go to done if p_slot is in the local nursery
    jb   cas_prt_interior_done

%ifdef CONCURRENT
    NOT IMPLEMENTED YET!!!
    mov eax, dword [esp+4]                ; // p_slot into eax
    lea edx, [esp+8]                      ; // value into edx

    push ebx
    mov  ebx, dword [esp+20]
    push eax
    mov  eax, dword [esp+20]              ; // offset
    push eax
    push edx
    push ecx

    mov  eax, esp
    push 0
    push 4
    push eax
    lea  eax, unmanaged_mark_phase
    push eax
    call prtInvokeUnmanagedFunc
    add  esp, 16
    pop  ebx

    ret  20
normal_mode:
%endif ; CONCURRENT

    mov  r11, _value$                  ; // value into edx
        ; // return if value is outside the nursery
    sub  r11, qword [rcx+2*REGISTER_SIZE]
    cmp  r11d, dword [local_nursery_size]
    jae  cas_prt_interior_done

    mov  qword [rsp+_slotStack$], _slot$    ; // save away args to this function in their stack locations
    mov  qword [rsp+_valueStack$], _value$
    mov  qword [rsp+_offsetStack$], _offset$

    push rbx
    mov  rbx, _task$

    mov  rcx, r10
    lea  rdx, [rsp+_valueStack$]
    mov  r8,  _offset$
    push r8
    push rdx
    push rcx

    mov  rcx, unmanaged_add_entry_interior
    mov  rdx, rsp
    mov  r8,  3
    mov  r9,  0
    sub  rsp, 4*REGISTER_SIZE

    call prtInvokeUnmanagedFunc

    add  rsp, 7*REGISTER_SIZE

    mov  _slot$, qword [rsp+_slotStack$]
    mov  _value$, qword [rsp+_valueStack$]

    pop  rbx
cas_prt_interior_done:
    ; // write value into *p_slot
    mov  rax, _cmp$
    lock cmpxchg [_slot$], _value$      ; // *p_slot = value
    ret
gc_cas_write_interior_ref_p_prt_end:





; -----------------------------------------------------------------------
; ------------ GENERATIONAL versions ------------------------------------
; -----------------------------------------------------------------------

global gc_heap_slot_gen_write_ref_p_prt
global gc_heap_slot_gen_write_ref_p_prt_end
gc_heap_slot_gen_write_ref_p_prt:
    jmp 0
%ifdef GEN64FIXED
    ; esp+4  = base;
	; esp+8  = p_slot
	; esp+12 = value
	; esp+16 = taskHandle
	; esp+20 = prev_frame
	mov eax, dword [esp+8]           ; // p_slot into eax
	mov ecx, eax
	and ecx, 0FFff0000h
	mov ecx, [ecx]
	cmp ecx, 0
	jz  gc_heap_slot_gen_write_ref_p_prt_done

	mov  edx, dword [esp+12]          ; // value into eax
	mov  ecx, edx
	and  ecx, 0FFff0000h              ; // pointer to start of GC block
	mov  ecx, [ecx]                   ; // value of thread_owner field...0 is young gen, 1 is old
	cmp  ecx, 0
	jnz  gc_heap_slot_gen_write_ref_p_prt_done

	push eax                          ; // push slot
	push edx                          ; // push value
	mov  ecx, dword [esp+24]          ; // Pillar task * into ecx
	add  ecx, 4                       ; // Go to TLS field in PrtTask
	mov  ecx, dword [ecx]             ; // TLS value (GC_Thread_Info*) into ecx
    add  ecx, g_tls_offset_bytes
	push ecx
	call add_gen_rs
	add  esp, 12                      ; // bypass thread pointer and base args

gc_heap_slot_gen_write_ref_p_prt_done:
	; // write value into *p_slot
	mov  eax, dword [esp+8]           ; // p_slot into eax
	mov  edx, dword [esp+12]          ; // value into edx
	mov  dword [eax], edx             ; // *p_slot = value

	ret 20
%endif
gc_heap_slot_gen_write_ref_p_prt_end:

; -----------------------------------------------------------------------

global gc_heap_slot_gen_write_interior_ref_p_prt
global gc_heap_slot_gen_write_interior_ref_p_prt_end
gc_heap_slot_gen_write_interior_ref_p_prt:
    jmp 0
%ifdef GEN64FIXED
    ; esp+4  = p_slot
	; esp+8  = value
	; esp+12 = offset
	; esp+16 = taskHandle
	; esp+20 = prev_frame
	mov eax, dword [esp+4]            ; // p_slot into eax
	mov ecx, eax
	and ecx, 0FFff0000h
	mov ecx, [ecx]
	cmp ecx, 0
	jz  gc_heap_slot_gen_write_interior_ref_p_prt_done

	mov  edx, dword [esp+8]           ; // value into eax
	mov  ecx, edx
	and  ecx, 0FFff0000h              ; // pointer to start of GC block
	mov  ecx, [ecx]                   ; // value of thread_owner field...0 is young gen, 1 is old
	cmp  ecx, 0
	jnz  gc_heap_slot_gen_write_interior_ref_p_prt_done

	push eax                          ; // push slot
	push edx                          ; // push value
	mov  ecx, dword [esp+24]          ; // Pillar task * into ecx
	add  ecx, 4                       ; // Go to TLS field in PrtTask
	mov  ecx, dword [ecx]             ; // TLS value (GC_Thread_Info*) into ecx
    add  ecx, g_tls_offset_bytes
	push ecx
	call add_gen_rs
	add  esp, 12                      ; // bypass thread pointer and base args

gc_heap_slot_gen_write_interior_ref_p_prt_done:
	; // write value into *p_slot
	mov  eax, dword [esp+8]           ; // p_slot into eax
	mov  edx, dword [esp+12]          ; // value into edx
	mov  dword [eax], edx             ; // *p_slot = value

	ret 20
%endif
gc_heap_slot_gen_write_interior_ref_p_prt_end:















%endif ; // __x86_64__
