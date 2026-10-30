#include <memory/stack.h>
#include <memory/block_pool.h>

static block_pool_t stack_pool = BLOCK_POOL_INITIALIZER;

aco_share_stack_t *stack_acquire(size_t size) {
    aco_share_stack_t *stack = size == CLPF_DEFAULT_STACK_SIZE ? block_pool_take(&stack_pool) : NULL;
    if (!stack)
        return aco_share_stack_new(size);

    // As aco_share_stack_new leaves it: no owner, and the slot a returning coroutine pops.
    // The guard page keeps its protection, as the mapping is never changed.
    stack->owner = NULL;
    stack->align_validsz = 0;
    *(void **)stack->align_retptr = (void *)aco_funcp_protector_asm;
    return stack;
}

static void stack_destroy(void *stack) {
    aco_share_stack_destroy(stack);
}

void stack_release(aco_share_stack_t *stack, size_t size) {
    if (size != CLPF_DEFAULT_STACK_SIZE || !block_pool_put(&stack_pool, stack))
        stack_destroy(stack);
}

void stack_pool_drain(void) {
    block_pool_drain(&stack_pool, stack_destroy);
}
