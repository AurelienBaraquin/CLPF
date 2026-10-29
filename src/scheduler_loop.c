#include <scheduler.h>
#include <flags_internal.h>
#include <utils/ox.h>
#include <stdio.h>
#include <clpf_internal.h>
#include <assert.h>
#include <unistd.h>

void co_entrypoint() {
    Process* proc = (Process*)aco_get_arg();

    if (!proc)
        return;

    funcall_call(&proc->taskcall);

    if (proc->state == PROCESS_EXITING)
        aco_exit();
    aco_yield();
}

void handle_signals(Process* process) {
    if (!process)
        return;

    MPSCQNode *node = signal_drain(process);
    while (node) {
        Signal *signal = (Signal *)node->data;
        if (!signal)
            break;
        if (signal->type == SIGNAL_DOWN) {
            send(NULL, process->pid, signal, sizeof(Signal), MAKE_STR_TAG("scheduler::event::signal"));
        }
        MPSCQNode *next_node = node->next;
        safe_heap_free(&process->heap, signal);
        free(node);
        node = next_node;
    }
}

process_state execute_process(Scheduler *scheduler, Process* process) {
    if (!process)
        return PROCESS_EXITING;

    if (!process->acoro && process->state == PROCESS_INIT) {
        process->share_stack = aco_share_stack_new(STACK_SIZE);
        process->acoro = aco_create(scheduler->scheduler_acoro, process->share_stack, 0, &co_entrypoint, process);
        aco_assertptr(process->acoro);
    }

    if (process->acoro->is_end == 1) {
        process->state = PROCESS_EXITING;
        return process->state;
    }

    process->state = PROCESS_RUNNING;
    assert(process->acoro);
    assert(process->acoro->is_end == 0);
    aco_resume(process->acoro);

    return process->state;
}

void request_process_exit(Process* process) {
    CoreMessage* msg = malloc(sizeof(CoreMessage));
    if (!msg)
        return;
    
    msg->op = CORE_MESSAGE_TYPE_PROCESS_EXIT;
    msg->sender = process->pid;
    msg->reply_to = -1;
    msg->data.process_exit.pid = process->pid;

    mpscq_push(&clpf.message_queue, msg);
}

void scheduler_loop(Scheduler* scheduler) {
    if (!scheduler)
        return;

    scheduler->system_thread_id = pthread_self();
    aco_thread_init(NULL);
    scheduler->scheduler_acoro = aco_create(NULL, NULL, 0, NULL, NULL);

    while (1) {
        if (UNLIKELY(exit_in_progress)) {
            break;
        }
        // Process scheduling logic goes here

        // 0. Pull queues
        // 1. Update reduction counters
        // 2. Check timers
        // 3. If needed check balance
        // 4. If needed migrate processes and ports
        // 5. Do auxiliary scheduler work
        // 6. If needed check IO and update time
        // 7. While needed pick a port task to execute
        
        // 8. Pick a process to execute
        Process* process = pick_process(scheduler);
        
        if (!process) {
            usleep(SCHEDULER_IDLE_SLEEP_US);
            continue;
        }

        // 9. Handle signals of the process (exemple : EXIT, DOWN, MONITOR, UNLINK ect...)
        handle_signals(process);

        // 10. Execute the process
        process->state = execute_process(scheduler, process);

        // 11. Handle the process state
        switch (process->state) {
            case PROCESS_READY:
            case PROCESS_RUNNING:
                process->state = PROCESS_READY;
                scheduler_push(scheduler, process);
                break;
            case PROCESS_WAITING:
                scheduler_push(scheduler, process);
                break;
            case PROCESS_EXITING:
                atomic_fetch_sub(&scheduler->process_count, 1);
                request_process_exit(process);
                break;
            default:
                break;
        }
    }
}
