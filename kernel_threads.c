#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
//Mutex mx = MUTEX_INIT;

void start_thread() {
//    PTCB* ptcb = (PTCB*) ThreadSelf();
    PTCB *ptcb = CURTHREAD->ptcb_node.ptcb;
    assert(ptcb != NULL);
    ThreadExit(ptcb->task(ptcb->argl, ptcb->args));
}

/**
  @brief Create a new thread in the current process.
  */
Tid_t CreateThread(Task task, int argl, void *args) {
    Mutex_Lock(&kernel_mutex);
    CURPROC->threads_counter++;
    assert(CURPROC == CURTHREAD->owner_pcb);
    PTCB *ptcb = (PTCB *) malloc(sizeof(PTCB));
    ptcb->isDetached = false;
    ptcb->task = task;
    /* Copy the arguments to new storage, owned by the new process */
    ptcb->argl = argl;
    if (args != NULL) {
        ptcb->args = xmalloc(argl);
        memcpy(ptcb->args, args, argl);
    } else { ptcb->args = NULL; }
    /*
      Create and wake up the thread for the main function. This must be the last thing
      we do, because once we wakeup the new thread it may run! so we need to have finished
      the initialization of the PCB.
     */
    rlnode *ptcb_node = rlnode_init(&ptcb->node, ptcb);
    rlist_push_front(&CURPROC->PTCB_list, ptcb_node);
    assert(ptcb != NULL);
    if (task != NULL) {
        ptcb->thread = spawn_thread(CURPROC, start_thread, *ptcb_node);
        wakeup(ptcb->thread);
    }
    Mutex_Unlock(&kernel_mutex);
    return (Tid_t) ptcb;
}

PTCB *FindPTCB(Tid_t tid) {
    int length = rlist_len(&CURPROC->PTCB_list);
    assert(length != 0);
    PTCB *ptcb = NULL;
    for (int i = 0; i < length; i++) {
        rlnode *tmp = rlist_pop_front(&CURPROC->PTCB_list);
        assert(tmp->ptcb != NULL);
        if (tmp->ptcb->thread == (TCB *) tid) {
            ptcb = tmp->ptcb;
        }
        rlist_push_back(&CURPROC->PTCB_list, &tmp->ptcb->node);
    }
    return ptcb;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t ThreadSelf() {
    Mutex_Lock(&kernel_mutex);
//    PTCB *ptcb = FindPTCB((Tid_t) CURTHREAD);
    int length = rlist_len(&CURPROC->PTCB_list);
    PTCB *ptcb = NULL;
    for (int i = 0; i < length; i++) {
        rlnode *tmp = rlist_pop_front(&CURPROC->PTCB_list);
        assert(tmp->ptcb != NULL);
        if (tmp->ptcb->thread == CURTHREAD) {
            ptcb = tmp->ptcb;
        }
        rlist_push_back(&CURPROC->PTCB_list, &tmp->ptcb->node);
    }
    Mutex_Unlock(&kernel_mutex);
    return (Tid_t) ptcb;
}

/**
  @brief Join the given thread.
  */
int ThreadJoin(Tid_t tid, int *exitval) {
    Mutex_Lock(&kernel_mutex);
    PTCB *ptcb = FindPTCB(tid);//------------prosoxi epane3etasi-------------------
    int returnVal = 0;
    if (ptcb == NULL || tid == (Tid_t) CURTHREAD || ptcb->isDetached) { returnVal = -1; }
    else {
    }
    Mutex_Unlock(&kernel_mutex);
    return returnVal;
}

/**
  @brief Detach the given thread.
  */
int ThreadDetach(Tid_t tid) {
    return -1;
}

/**
  @brief Terminate the current thread.
  */
void ThreadExit(int exitval) {
    CURPROC->threads_counter--;
    if (CURPROC->threads_counter == 0) { Cond_Broadcast(&CURPROC->PTCB_list.next->ptcb->condVar); }
}

/**
  @brief Awaken the thread, if it is sleeping.

  This call will set the interrupt flag of the
  thread.

  */
int ThreadInterrupt(Tid_t tid) {
    return -1;
}

/**
  @brief Return the interrupt flag of the
  current thread.
  */
int ThreadIsInterrupted() {
    return 0;
}

/**
  @brief Clear the interrupt flag of the
  current thread.
  */
void ThreadClearInterrupt() {
}