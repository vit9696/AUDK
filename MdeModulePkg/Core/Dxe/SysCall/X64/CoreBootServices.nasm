;------------------------------------------------------------------------------
;
; Copyright (c) 2024, Mikhail Krichanov. All rights reserved.
; SPDX-License-Identifier: BSD-3-Clause
;
;------------------------------------------------------------------------------

#include <Register/Intel/ArchitecturalMsr.h>

extern ASM_PFX(CallBootService)
extern ASM_PFX(gCoreSysCallStackTop)
extern ASM_PFX(gRing3CallStackTop)
extern ASM_PFX(gRing3EntryPoint)

DEFAULT REL
SECTION .text

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; AllowSupervisorAccessToUserMemory (
;   VOID
;   );
;------------------------------------------------------------------------------
global ASM_PFX(AllowSupervisorAccessToUserMemory)
ASM_PFX(AllowSupervisorAccessToUserMemory):
    pushfq
    pop     r10
    or      r10, 0x40000 ; Set AC (bit 18)
    push    r10
    popfq
    ret

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; ForbidSupervisorAccessToUserMemory (
;   VOID
;   );
;------------------------------------------------------------------------------
global ASM_PFX(ForbidSupervisorAccessToUserMemory)
ASM_PFX(ForbidSupervisorAccessToUserMemory):
    pushfq
    pop     r10
    and     r10, ~0x40000 ; Clear AC (bit 18)
    push    r10
    popfq
    ret

;------------------------------------------------------------------------------
; EFI_STATUS
; EFIAPI
; CallInstallMultipleProtocolInterfaces (
;   IN EFI_HANDLE  *Handle,
;   IN VOID        **ArgList,
;   IN UINT32      ArgListSize,
;   IN VOID        *Function
;   );
;------------------------------------------------------------------------------
global ASM_PFX(CallInstallMultipleProtocolInterfaces)
ASM_PFX(CallInstallMultipleProtocolInterfaces):
    push    r12

    ; Save function input.
    mov     rax, rdx
    mov     r10, r8
    mov     r11, r9

    ; Prepare registers for call.
    mov     rdx, [rax]
    mov     r8, [rax + 8]
    mov     r9, [rax + 8*2]

    ; Prepare stack for call.
    lea     rax, [rax + r10 * 8]
    mov     r12, r10
copy:
    sub     rax, 8
    push qword [rax]
    sub     r10, 1
    jnz     copy
    push    rcx

    call    r11

    ; Step over Function arguments.
    pop     rcx
    lea     rsp, [rsp + r12 * 8]

    pop     r12

    ret

%macro SetRing3DataSegmentSelectors 0
    mov     rcx, MSR_IA32_STAR
    rdmsr
    shl     rdx, 0x20
    or      rax, rdx
    ; rax = ((RING3_CODE64_SEL - 16) << 16 | RING0_CODE64_SEL) << 32
    shr     rax, 48
    add     rax, 8

    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
%endmacro

;------------------------------------------------------------------------------
; EFI_STATUS
; EFIAPI
; CoreBootServices (
;   IN  UINT8  Type,
;   ...
;   );
;
;   (rcx) RIP of the next instruction saved by SYSCALL in SysCall().
;   (rdx) Argument 1 of the called function.
;   (r8)  Argument 2 of the called function.
;   (r9)  Argument 3 of the called function.
;   (r10) Type.
;   (r11) RFLAGS saved by SYSCALL in SysCall().
;
;   (On User Stack) Argument 4, 5, ...
;------------------------------------------------------------------------------
global ASM_PFX(CoreBootServices)
ASM_PFX(CoreBootServices):
    ; Switch from User to Core data segment selectors.
    mov     ax, ss
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

    ; Save User Stack pointers and switch to Core SysCall Stack.
    mov     rax, [ASM_PFX(gCoreSysCallStackTop)]
    sub     rax, 8
    mov     [rax], rsp
    mov     rsp, rax
    push    rbp
    ; Save return address for SYSRET.
    push    rcx
    ; Save User RFLAGS for SYSRET.
    push    r11
    ; Save User Arguments [1..3].
    push    r9
    push    r8
    push    rdx
    mov     rbp, rsp
    ; Reserve space on stack for 4 CallBootService arguments (NOOPT prerequisite).
    sub     rsp, 8*4

    ; Prepare CallBootService arguments.
    mov     rcx, r10
    mov     rdx, rbp
    mov     r8, [rbp + 8*6]

    sti
    call ASM_PFX(CallBootService)
    push    rax
    cli

    SetRing3DataSegmentSelectors

    pop     rax

    ; Step over Arguments [1..3] and NOOPT buffer.
    add     rsp, 8*7

    ; Prepare SYSRET arguments.
    pop     r11
    pop     rcx

    ; Switch to User Stack.
    pop     rbp
    pop     rsp

    ; SYSCALL saves RFLAGS into R11 and the RIP of the next instruction into RCX.
o64 sysret
    ; SYSRET copies the value in RCX into RIP and loads RFLAGS from R11.

;------------------------------------------------------------------------------
; EFI_STATUS
; EFIAPI
; CallRing3 (
;   IN RING3_CALL_DATA *Data
;   );
;
;   (rcx) Data
;------------------------------------------------------------------------------
global ASM_PFX(CallRing3)
ASM_PFX(CallRing3):
    pushfq
    pop     r11
    cli
    ; Save nonvolatile registers RBX, RBP, RDI, RSI, RSP, R12, R13, R14, and R15.
    push    rbx
    push    rbp
    push    rdi
    push    rsi
    push    r12
    push    r13
    push    r14
    push    r15

    ; Save Core Stack pointer.
    mov     [ASM_PFX(CoreRsp)], rsp

    ; Save input Arguments.
    mov     r8, [ASM_PFX(gRing3CallStackTop)]
    mov     r9, [ASM_PFX(gRing3EntryPoint)]
    mov     r10, rcx

    SetRing3DataSegmentSelectors

    ; Prepare SYSRET arguments.
    mov     rdx, r10
    mov     rcx, r9

    ; Switch to User Stack.
    mov     rsp, r8
    mov     rbp, rsp

    ; Pass control to user image
o64 sysret

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; ReturnToCore (
;   IN EFI_STATUS Status
;   );
;------------------------------------------------------------------------------
global ASM_PFX(ReturnToCore)
ASM_PFX(ReturnToCore):
    mov     rsp, [ASM_PFX(CoreRsp)]
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rsi
    pop     rdi
    pop     rbp
    pop     rbx

    mov     rax, rcx
    sti
    ret

SECTION .data
ASM_PFX(CoreRsp):
  resq 1
