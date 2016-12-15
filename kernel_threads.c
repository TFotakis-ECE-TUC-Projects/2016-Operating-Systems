#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
/*Start the current thread created by the spawn function*/
void start_thread() {
	PTCB *ptcb = (PTCB *) ThreadSelf_withMutex();
	int argl = ptcb->argl;
	void *args = ptcb->args;
	Task call = ptcb->task;
	int exitval = call(argl, args);
	assert(ptcb != NULL);
	ThreadExit(exitval);
}
/**
  @brief Create a new thread in the current process.
  */
Tid_t CreateThread(Task task, int argl, void *args) {
	Mutex_Lock(&kernel_mutex);
	CURPROC->threads_counter++;
	assert(CURPROC == CURTHREAD->owner_pcb);
	PTCB *ptcb = (PTCB *) malloc(sizeof(PTCB));
	ptcb->refcount = 0;
	ptcb->isExited = 0;
	ptcb->isDetached = 0;
	ptcb->task = task;
	/* Copy the arguments to new storage, owned by the new process */
	ptcb->argl = argl;
	ptcb->condVar = COND_INIT;
	if (args != NULL) {
		ptcb->args = args;
	} else { ptcb->args = NULL; }
	/*
	  Create and wake up the thread for the main function. This must be the last thing
	  we do, because once we wakeup the new thread it may run! so we need to have finished
	  the initialization of the PCB.
	 */
	rlnode *ptcb_node = rlnode_init(&ptcb->node, ptcb);
	rlist_push_back(&CURPROC->PTCB_list, ptcb_node);
	assert(ptcb != NULL);
	if (task != NULL) {
		ptcb->thread = spawn_thread(CURPROC, start_thread);
		wakeup(ptcb->thread);
	}
	Mutex_Unlock(&kernel_mutex);
	return (Tid_t) ptcb->thread;
}
/**
 	@brief Return the Tid of the tid Thread's PTCB.
*/
PTCB *FindPTCB(Tid_t tid) {
	int length = rlist_len(&CURPROC->PTCB_list);
	assert(length != 0);
	PTCB *ptcb = NULL;
	for (int i = 0; i < length; i++) {
		rlnode *tmp = rlist_pop_front(&CURPROC->PTCB_list);
		assert(tmp != NULL);
		assert(tmp->ptcb != NULL);
		if (tmp->ptcb->thread == (TCB *) tid) {
			ptcb = tmp->ptcb;
		}
		rlist_push_back(&CURPROC->PTCB_list, &tmp->ptcb->node);
	}
	return ptcb;
}
/**
  @brief Return the Tid of the current thread's PTCB.
 */
Tid_t ThreadSelf() {
	PTCB *ptcb = FindPTCB((Tid_t) CURTHREAD);
	return (Tid_t) ptcb;
}
/**
 	@brief Call the ThreadSelf using Mutexes.
 */
Tid_t ThreadSelf_withMutex() {
	Mutex_Lock(&kernel_mutex);
	Tid_t tid = ThreadSelf();
	Mutex_Unlock(&kernel_mutex);
	return tid;
}
/**
  @brief Join the given thread.
  */
int ThreadJoin(Tid_t tid, int *exitval) {
	Mutex_Lock(&kernel_mutex);
	PTCB *ptcb = FindPTCB(tid);
	int returnVal = 0;
	if (ptcb == NULL || tid == (Tid_t) CURTHREAD || ptcb->isDetached) { returnVal = -1; }
	else {
		ptcb->refcount++;
		while (!ptcb->isExited && !ptcb->isDetached) {
			Cond_Wait(&kernel_mutex, &ptcb->condVar);
		}
		if (ptcb->isDetached) {
			returnVal = -1;
		} else {
			if (exitval) {
				*exitval = ptcb->exitval;
			}
			if (ptcb->refcount == 1) {
				rlist_remove(&ptcb->node);
				free(ptcb);
			}
		}
	}
	Mutex_Unlock(&kernel_mutex);
	return returnVal;
}
/**
  @brief Detach the given thread.
  */
int ThreadDetach(Tid_t tid) {
	Mutex_Lock(&kernel_mutex);
	PTCB *ptcb = FindPTCB(tid);
	int returnVal;
	if (ptcb == NULL || ptcb->isExited) {
		returnVal = -1;
	} else {
		ptcb->isDetached = 1;
		Cond_Broadcast(&ptcb->condVar);
		returnVal = 0;
	}
	Mutex_Unlock(&kernel_mutex);
	return returnVal;
}
/**
  @brief Terminate the current thread.
  */
void ThreadExit(int exitval) {
	Mutex_Lock(&kernel_mutex);
	CURPROC->threads_counter--;
	PTCB *ptcb = ((PTCB *) ThreadSelf());
	ptcb->isExited = 1;
	ptcb->exitval = exitval;
	Cond_Broadcast(&ptcb->condVar);
	Cond_Broadcast(&CURPROC->condVar);
	sleep_releasing(EXITED, &kernel_mutex);
	Mutex_Unlock(&kernel_mutex);
}
/**
  @brief Awaken the thread, if it is sleeping.

  This call will set the interrupt flag of the
  thread.

  */
int ThreadInterrupt(Tid_t tid) {
	Mutex_Lock(&kernel_mutex);
	TCB *tcb = (TCB *) tid;
	tcb->interruptFlag = 1;
	if (tcb->state == STOPPED) {
		Mutex_Unlock(&kernel_mutex);
		wakeup(tcb);
		return 0;
	} else {
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
}
/**
  @brief Return the interrupt flag of the
  current thread.
  */
int ThreadIsInterrupted() {
	Mutex_Lock(&kernel_mutex);
	int returnVal = CURTHREAD->interruptFlag;
	Mutex_Unlock(&kernel_mutex);
	return returnVal;
}
/**
  @brief Clear the interrupt flag of the
  current thread.
  */
void ThreadClearInterrupt() {
	Mutex_Lock(&kernel_mutex);
	CURTHREAD->interruptFlag = 0;
	Mutex_Unlock(&kernel_mutex);
}