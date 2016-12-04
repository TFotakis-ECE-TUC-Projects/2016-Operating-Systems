#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
/*
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */
/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;
PCB *get_pcb(Pid_t pid) {
	return PT[pid].pstate == FREE ? NULL : &PT[pid];
}
Pid_t get_pid(PCB *pcb) {
	return pcb == NULL ? NOPROC : pcb - PT;
}
/* Initialize a PCB */
static inline void initialize_PCB(PCB *pcb) {
	pcb->pstate = FREE;
	for (int i = 0; i < MAX_FILEID; i++) { pcb->FIDT[i] = NULL; }
	rlnode_init(&pcb->children_list, NULL);
	rlnode_init(&pcb->exited_list, NULL);
	rlnode_init(&pcb->children_node, pcb);
	rlnode_init(&pcb->exited_node, pcb);
	pcb->child_exit = COND_INIT;
	/*Our edits*/
	rlnode_new(&pcb->PTCB_list);
}
static PCB *pcb_freelist;
void initialize_processes() {
	/* initialize the PCBs */
	for (Pid_t p = 0; p < MAX_PROC; p++) {
		initialize_PCB(&PT[p]);
	}
	/* use the parent field to build a free list */
	PCB *pcbiter;
	pcb_freelist = NULL;
	for (pcbiter = PT + MAX_PROC; pcbiter != PT;) {
		--pcbiter;
		pcbiter->parent = pcb_freelist;
		pcb_freelist = pcbiter;
	}
	process_count = 0;
	/* Execute a null "idle" process */
	if (Exec(NULL, 0, NULL) != 0) {FATAL("The scheduler process does not have pid==0"); }
}
/*
  Must be called with kernel_mutex held
*/
PCB *acquire_PCB() {
	PCB *pcb = NULL;
	if (pcb_freelist != NULL) {
		pcb = pcb_freelist;
		pcb->pstate = ALIVE;
		pcb_freelist = pcb_freelist->parent;
		process_count++;
	}
	return pcb;
}
/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB *pcb) {
	pcb->pstate = FREE;
	pcb->parent = pcb_freelist;
	pcb_freelist = pcb;
	process_count--;
}
/*
 *
 * Process creation
 *
 */
/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_main_thread() {
	PTCB *ptcb = (PTCB *) ThreadSelf_withMutex();//Get the Current Thread's PTCB
	assert(ptcb != NULL);
	int argl = ptcb->argl;
	void *args = ptcb->args;
	Task call = ptcb->task;
	int exitval = call(argl, args);
	Exit(exitval);
}
/*
  System call to create a new process.
 */
Pid_t Exec(Task call, int argl, void *args) {
	PCB *curproc, *newproc;
	Mutex_Lock(&kernel_mutex);
	/* The new process PCB */
	newproc = acquire_PCB();
	if (newproc == NULL) { goto finish; } /* We have run out of PIDs! */
	if (get_pid(newproc) <= 1) {
		/* Processes with pid<=1 (the scheduler and the init process)
		   are parentless and are treated specially. */
		newproc->parent = NULL;
	} else {
		/* Inherit parent */
		curproc = CURPROC;
		/* Add new process to the parent's child list */
		newproc->parent = curproc;
		rlist_push_front(&curproc->children_list, &newproc->children_node);
		/* Inherit file streams from parent */
		for (int i = 0; i < MAX_FILEID; i++) {
			newproc->FIDT[i] = curproc->FIDT[i];
			if (newproc->FIDT[i]) { FCB_incref(newproc->FIDT[i]); }
		}
	}
	/*Our edits*/
	/*Initializing out new pcb properties*/
	newproc->main_task = call;
	newproc->argl = argl;
	if (args != NULL) {
		newproc->args = malloc(argl);
		memcpy(newproc->args, args, argl);
	} else
		newproc->args = NULL;
	newproc->threads_counter = 0;
	newproc->condVar = COND_INIT;
	/*Initializing new ptcb*/
	PTCB *ptcb = (PTCB *) malloc(sizeof(PTCB));
	ptcb->refcount = 0;
	ptcb->isExited = 0;
	ptcb->isDetached = 0;
	ptcb->task = call;
	ptcb->argl = argl;
	if (args != NULL) {
		ptcb->args = malloc(argl);
		memcpy(ptcb->args, args, argl);
	} else { ptcb->args = NULL; }
	/*
	  Create and wake up the thread for the main function. This must be the last thing
	  we do, because once we wakeup the new thread it may run! so we need to have finished
	  the initialization of the PCB.
	 */
	rlnode *ptcb_node = rlnode_init(&ptcb->node, ptcb);
	rlist_push_back(&newproc->PTCB_list, ptcb_node);
	if (call != NULL) {
		ptcb->thread = spawn_thread(newproc, start_main_thread);
		wakeup(ptcb->thread);
	}
	finish:
	Mutex_Unlock(&kernel_mutex);
	return get_pid(newproc);
}
/* System call */
Pid_t GetPid() {
	return get_pid(CURPROC);
}
Pid_t GetPPid() {
	return get_pid(CURPROC->parent);
}
static void cleanup_zombie(PCB *pcb, int *status) {
	if (status != NULL) { *status = pcb->exitval; }
	rlist_remove(&pcb->children_node);
	rlist_remove(&pcb->exited_node);
	release_PCB(pcb);
}
static Pid_t wait_for_specific_child(Pid_t cpid, int *status) {
	Mutex_Lock(&kernel_mutex);
	/* Legality checks */
	if ((cpid < 0) || (cpid >= MAX_PROC)) {
		cpid = NOPROC;
		goto finish;
	}
	PCB *parent = CURPROC;
	PCB *child = get_pcb(cpid);
	if (child == NULL || child->parent != parent) {
		cpid = NOPROC;
		goto finish;
	}
	/* Ok, child is a legal child of mine. Wait for it to exit. */
	while (child->pstate == ALIVE) { Cond_Wait(&kernel_mutex, &parent->child_exit); }
	cleanup_zombie(child, status);
	finish:
	Mutex_Unlock(&kernel_mutex);
	return cpid;
}
static Pid_t wait_for_any_child(int *status) {
	Pid_t cpid;
	Mutex_Lock(&kernel_mutex);
	PCB *parent = CURPROC;
	/* Make sure I have children! */
	if (is_rlist_empty(&parent->children_list)) {
		cpid = NOPROC;
		goto finish;
	}
	while (is_rlist_empty(&parent->exited_list)) {
		Cond_Wait(&kernel_mutex, &parent->child_exit);
	}
	PCB *child = parent->exited_list.next->pcb;
	assert(child->pstate == ZOMBIE);
	cpid = get_pid(child);
	cleanup_zombie(child, status);
	finish:
	Mutex_Unlock(&kernel_mutex);
	return cpid;
}
Pid_t WaitChild(Pid_t cpid, int *status) {
	/* Wait for specific child. */
	if (cpid != NOPROC) {
		return wait_for_specific_child(cpid, status);
	}
		/* Wait for any child */
	else {
		return wait_for_any_child(status);
	}
}
void Exit(int exitval) {
	/* Right here, we must check that we are not the boot task. If we are,
	   we must wait until all processes exit. */
	if (GetPid() == 1) {
		while (WaitChild(NOPROC, NULL) != NOPROC);
	}
	/* Now, we exit */
	Mutex_Lock(&kernel_mutex);
	/* Do all the other cleanup we want here, close files etc. */
	/*Our edits*/
	PCB *curproc = CURPROC;  /* cache for efficiency */
	while (curproc->threads_counter != 0) {
		Cond_Wait(&kernel_mutex, &CURPROC->condVar);
	}
	/*Free the Current PCB's PTCB list*/
	while (!is_rlist_empty(&curproc->PTCB_list)) {
		rlnode *tmp = rlist_pop_front(&curproc->PTCB_list);
		free(tmp->ptcb);
	}
	/* Clean up FIDT */
	for (int i = 0; i < MAX_FILEID; i++) {
		if (curproc->FIDT[i] != NULL) {
			FCB_decref(curproc->FIDT[i]);
			curproc->FIDT[i] = NULL;
		}
	}
	/* Reparent any children of the exiting process to the
	   initial task */
	PCB *initpcb = get_pcb(1);
	while (!is_rlist_empty(&curproc->children_list)) {
		rlnode *child = rlist_pop_front(&curproc->children_list);
		child->pcb->parent = initpcb;
		rlist_push_front(&initpcb->children_list, child);
	}
	/* Add exited children to the initial task's exited list
	   and signal the initial task */
	if (!is_rlist_empty(&curproc->exited_list)) {
		rlist_append(&initpcb->exited_list, &curproc->exited_list);
		Cond_Broadcast(&initpcb->child_exit);
	}
	/* Put me into my parent's exited list */
	if (curproc->parent != NULL) {  /* Maybe this is init */
		rlist_push_front(&curproc->parent->exited_list, &curproc->exited_node);
		Cond_Broadcast(&curproc->parent->child_exit);
	}
	/* Now, mark the process as exited. */
	curproc->pstate = ZOMBIE;
	curproc->exitval = exitval;
	/* Bye-bye cruel world */
	sleep_releasing(EXITED, &kernel_mutex);
}
typedef struct info_control_block {
	char buffer[MAX_PROC * sizeof(procinfo)];
	uint readPos;
} InfoCB;
int sysinfo_read(void *infoCB, char *buf, unsigned int size) {
	InfoCB *infocb = (InfoCB *) infoCB;
//	FCB *fcb = get_fcb(*infocb);
	uint count;
	for (count = 0; count < size && infocb->readPos < MAX_PROC; count++) {
		buf[count] = infocb->buffer[infocb->readPos];
		infocb->readPos++;
	}
//	MSG("it reads\n");
	return count;
}
int sysinfo_close(void *infoCB) {
	free(infoCB);
	return 0;
}
file_ops sysinfo_funcs = {
		.Open = NULL,
		.Read = sysinfo_read,
		.Write = NULL,
		.Close = sysinfo_close
};
Fid_t OpenInfo() {
	Fid_t fid;
	FCB *fcb;
	Mutex_Lock(&kernel_mutex);
	if (!FCB_reserve(1, &fid, &fcb)) {
		Mutex_Unlock(&kernel_mutex);
		return NOFILE;
	}
	fcb->streamobj = xmalloc(sizeof(InfoCB));
	((InfoCB *) fcb->streamobj)->readPos = 0;
	fcb->streamfunc = &sysinfo_funcs;
	procinfo *info = (procinfo *) xmalloc(sizeof(procinfo));
	for (int i = 0; i < MAX_PROC; i++) {
		PCB *pcb = &PT[i];
		info->pid = get_pid(&PT[i]);
		info->ppid = get_pid(pcb->parent);
		info->alive = pcb->pstate == ALIVE;
		info->thread_count = (unsigned long) (info->alive ? pcb->threads_counter + 1 : 0);
		info->main_task = pcb->main_task;
		info->argl = pcb->argl;
		for (int j = 0; j < info->argl; j++) {
			info->args[j] = ((char *) pcb->args)[j];
		}
		memcpy(&(((InfoCB *) fcb->streamobj)->buffer[i * sizeof(procinfo)]), info, sizeof(procinfo));
	}
	Mutex_Unlock(&kernel_mutex);
	return fid;
}