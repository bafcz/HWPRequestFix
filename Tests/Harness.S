.text
.globl _runPayload
_runPayload:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    movq %rdi, %r12
    movq %rsi, %r13
    movq 0(%r12), %rax
    movq 8(%r12), %rbx
    movq 16(%r12), %rcx
    movq 24(%r12), %rdx
    movq 32(%r12), %rsi
    movq 40(%r12), %rdi
    movq 48(%r12), %r8
    movq 56(%r12), %r9
    movq 64(%r12), %r10
    movq 72(%r12), %r11
    pushq 80(%r12)
    popfq
    callq *%r13
    pushfq
    popq %r14
    movq %rax, 0(%r12)
    movq %rbx, 8(%r12)
    movq %rcx, 16(%r12)
    movq %rdx, 24(%r12)
    movq %rsi, 32(%r12)
    movq %rdi, 40(%r12)
    movq %r8, 48(%r12)
    movq %r9, 56(%r12)
    movq %r10, 64(%r12)
    movq %r11, 72(%r12)
    movq %r14, 80(%r12)
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq
