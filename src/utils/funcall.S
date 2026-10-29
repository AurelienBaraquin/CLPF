    .globl call_raw_func
    .text

call_raw_func:
    # Input:
    # %rdi = function to call (void (*func)())
    # %rsi = array of uintptr_t arguments
    # %rdx = number of arguments

    # Free the argument registers
    mov %rdi, %rax      # RAX = function
    mov %rsi, %r10      # R10 = args[]
    mov %rdx, %r11      # R11 = argc

    cmp $1, %r11
    jl .call
    mov (%r10), %rdi    # arg1

    cmp $2, %r11
    jl .call
    mov 8(%r10), %rsi   # arg2

    cmp $3, %r11
    jl .call
    mov 16(%r10), %rdx  # arg3

    cmp $4, %r11
    jl .call
    mov 24(%r10), %rcx  # arg4

    cmp $5, %r11
    jl .call
    mov 32(%r10), %r8   # arg5

    cmp $6, %r11
    jl .call
    mov 40(%r10), %r9   # arg6

.call:
    # rsp ≡ 8 (mod 16) here; realign so the callee is entered with rsp ≡ 8 as the ABI requires
    sub $8, %rsp
    call *%rax
    add $8, %rsp
    ret

.section .note.GNU-stack,"",@progbits
