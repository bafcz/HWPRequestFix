.text
.p2align 4
.globl _hwpGeneralStart, _hwpGeneralGate, _hwpGeneralEnd
_hwpGeneralStart:
    pushfq
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %r12
    andq $-16, %rsp
    .byte 0x49, 0xbb
_hwpGeneralGate:
    .quad 0
    movzbl (%r11), %esi
    testl %esi, %esi
    je .LgeneralRestore
#ifdef HWP_PAYLOAD_TEST
    xorl %edx, %edx
    cmpl $6, %esi
    jne 1f
    movl $39, %edx
1:
#else
    xorl %edx, %edx
    cmpl $3, %esi
    jne 1f
    movl $0x771, %ecx
    rdmsr
    movl %eax, %edx
1:
#endif
    movq %r12, %rdi
    callq *8(%r11)
.LgeneralRestore:
    movq %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    popfq
_hwpGeneralEnd:

.p2align 4
.globl _hwpCoreStart, _hwpCoreGate, _hwpCoreEnd
_hwpCoreStart:
    pushfq
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %r12
    andq $-16, %rsp
    .byte 0x49, 0xba
_hwpCoreGate:
    .quad 0
    movzbl (%r10), %esi
    testl %esi, %esi
    je .LcoreRestore
#ifdef HWP_PAYLOAD_TEST
    xorl %edx, %edx
    cmpl $5, %esi
    jne 1f
    movl $39, %edx
1:
#else
    movl $0x771, %ecx
    rdmsr
    movl %eax, %edx
#endif
    movq %r12, %rdi
    callq *16(%r10)
.LcoreRestore:
    movq %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    popfq
_hwpCoreEnd:
