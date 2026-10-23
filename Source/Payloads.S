.text
.p2align 4
.globl _hwpGeneralStart, _hwpGeneralGate, _hwpGeneralEnd
_hwpGeneralStart:
    pushfq
    pushq %rax
    pushq %rcx
    .byte 0x48, 0xb8
_hwpGeneralGate:
    .quad 0
    cmpb $1, (%rax)
    je 1f
    cmpb $2, (%rax)
    je 2f
    cmpb $3, (%rax)
    je .LgeneralMode3
#ifdef HWP_PAYLOAD_TEST
    // Model RDMSR's ECX clobber in the forced E-core test path.
    movl $0x771, %ecx
    // Test-only forced E-core path; production uses IA32_HWP_CAPABILITIES.
    cmpb $6, (%rax)
    jne 3f
    cmpl $54, %edx
    jne 3f
    jmp .LgeneralE
#endif
    jmp 3f
.LgeneralMode3:
    cmpl $54, %edx
    jne 3f
#ifndef HWP_PAYLOAD_TEST
    // Read the capability of the logical CPU handling this request.  The
    // E-cores advertise highest=39; P-cores advertise 67/69.
    movl $0x771, %ecx
    rdmsr
#else
    // Model RDMSR's ECX clobber and a P-core capability without privileged access.
    movl $0x771, %ecx
    movl $67, %eax
#endif
    andl $0xff, %eax
    cmpl $39, %eax
#ifndef HWP_PAYLOAD_TEST
    je .LgeneralE
#endif

    movl $69, %edx
    // Convert a 100 MHz ratio to the Raptor Lake P-core HWP scale.
    movl %ebx, %eax
    shrl $16, %eax
    cmpl $54, %eax
    ja 3f
    imull $325, %eax
    addl $254, %eax
    shrl $8, %eax
    shll $16, %eax
    andl $0xff00ffff, %ebx
    orl %eax, %ebx
    jmp 3f
.LgeneralE:
    movl $39, %edx
    movl %ebx, %eax
    shrl $16, %eax
    cmpl $54, %eax
    ja 3f
    cmpl $39, %eax
    jbe .LgeneralEStore
    movl $39, %eax
.LgeneralEStore:
    shll $16, %eax
    andl $0xff00ffff, %ebx
    orl %eax, %ebx
    jmp 3f
2:
    cmpl $54, %edx
    jne 3f
    movl $69, %edx
    // Zero requests hardware-autonomous selection, not a zero clock.
    cmpl $0x360000, %ebx
    ja 3f
    xorl %ebx, %ebx
    jmp 3f
1:
    cmpl $54, %edx
    jne 3f
    movl $69, %edx
    // This aligned site is immediately after the original desired-field shift.
    cmpl $0x360000, %ebx
    jne 3f
    movl $0x450000, %ebx
3:
    popq %rcx
    popq %rax
    popfq
_hwpGeneralEnd:

.p2align 4
.globl _hwpCoreStart, _hwpCoreGate, _hwpCoreEnd
_hwpCoreStart:
    pushfq
    // RDMSR clobbers EAX/EDX; its selector uses ECX. Preserve scratch state.
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %r8
    .byte 0x48, 0xba
_hwpCoreGate:
    .quad 0
    movzbl (%rdx), %r8d
    cmpl $1, %r8d
    je .LcoreHaveMode
    cmpl $2, %r8d
    je .LcoreHaveMode
    cmpl $3, %r8d
#ifdef HWP_PAYLOAD_TEST
    je .LcoreHaveMode
    cmpl $4, %r8d
    je .LcoreHaveMode
    cmpl $5, %r8d
#endif
    jne .LcoreDone
.LcoreHaveMode:
#ifdef HWP_PAYLOAD_TEST
    cmpl $4, %r8d
    je .LcorePDispatch
    cmpl $5, %r8d
    je .LcoreE
#endif
#ifdef HWP_PAYLOAD_TEST
    // The native test cannot execute RDMSR from user space, so it uses
    // explicit P/E dispatch values instead.
#else
    // Read the capability of the logical CPU executing this request.  On
    // this machine E-cores advertise highest=39 while P-cores advertise
    // 67/69.  This is more reliable here than CPUID.1A because it observes
    // the same per-CPU HWP state that the request write is about to use.
    movl $0x771, %ecx
    rdmsr
    andl $0xff, %eax
    cmpl $39, %eax
    je .LcoreE
#endif

.LcorePDispatch:
    // The saved RAX is at 32(%rsp): flags, RAX, RBX, RCX, RDX, R8.
    movq 32(%rsp), %rax
    cmpl $1, %r8d
    je .LcoreP1
    cmpl $2, %r8d
    je .LcoreP2
    // Mode 3: translate each P-core desired value exactly once.
    cmpq $0x4500, %rax
    jne .LcoreDone
    movl %r9d, %edx
    shrl $16, %edx
    cmpl $54, %edx
    ja .LcoreDone
    // Same exact Linux RPL conversion as the general request hook.
    imull $325, %edx
    addl $254, %edx
    shrl $8, %edx
    shll $16, %edx
    andl $0xff00ffff, %r9d
    orl %edx, %r9d
    jmp .LcoreDone
.LcoreP2:
    cmpq $0x4500, %rax
    jne .LcoreDone
    movl %r9d, %edx
    shrl $16, %edx
    cmpl $54, %edx
    ja .LcoreDone
    andl $0xff00ffff, %r9d
    jmp .LcoreDone
.LcoreP1:
    // RAX already contains the maximum performance field shifted by 8.
    cmpq $0x4500, %rax
    jne .LcoreDone
    // Desired is shifted by 16; the low minimum field must be preserved.
    movl %r9d, %edx
    shrl $16, %edx
    cmpl $54, %edx
    jne .LcoreDone
    addl $0x0f0000, %r9d
    jmp .LcoreDone

.LcoreE:
    // The package hook intentionally leaves MAX=69 for the package. This
    // per-core hook reduces it to the E-core capability before the WRMSR.
    movq 32(%rsp), %rax
    movl %eax, %edx
    shrl $8, %edx
    andl $0xff, %edx
    cmpl $39, %edx
    jbe .LcoreEMaxOK
    andl $0xffff00ff, %eax
    orl $0x2700, %eax
.LcoreEMaxOK:
    // popq %rax at the common epilogue restores the saved slot, so keep the
    // E-core-adjusted maximum there rather than losing it on return.
    movq %rax, 32(%rsp)
    // E-core requests use native 100 MHz units, not the P-core HWP scale.
    cmpl $2, %r8d
    jne .LcoreECapDesired
    movl %r9d, %edx
    shrl $16, %edx
    andl $0xff, %edx
    cmpl $54, %edx
    ja .LcoreECapDesired
    andl $0xff00ffff, %r9d
    jmp .LcoreDone
.LcoreECapDesired:
    movl %r9d, %edx
    shrl $16, %edx
    andl $0xff, %edx
    cmpl $39, %edx
    jbe .LcoreDone
    andl $0xff00ffff, %r9d
    orl $0x270000, %r9d
.LcoreDone:
    popq %r8
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    popfq
_hwpCoreEnd:
