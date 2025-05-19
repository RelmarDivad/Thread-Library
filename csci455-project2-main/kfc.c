#include <assert.h>
#include <sys/types.h>
#include <stdlib.h>
#include "kfc.h"
#include <ucontext.h>
#include "queue.h" 
#include <valgrind/memcheck.h>
static int inited;
static int threadNo;
static int current_id;
static ucontext_t threadArray[KFC_MAX_THREADS];
static void *returnArray[KFC_MAX_THREADS];
static queue_t thrQueue;
static ucontext_t scheduler;
static queue_t WaitList[KFC_MAX_THREADS];
static int isRunning[KFC_MAX_THREADS];
static int isMem[KFC_MAX_THREADS];
/**
 * Initializes the kfc library.  Programs are required to call this function
 * before they may use anything else in the library's public interface.
 *
 * @param kthreads    Number of kernel threads (pthreads) to allocate
 * @param quantum_us  Preemption timeslice in microseconds, or 0 for cooperative
 *                    scheduling
 *
 * @return 0 if successful, nonzero on failure
 */


 void schedule_prot(){
	
	int dq = queue_dequeue(&thrQueue) - 1;
	current_id = dq;
	setcontext(&threadArray[dq]);
 }

 void trampoline(void *(*start_func)(void *), void *arg){
	kfc_exit(start_func(arg));
 }

int
kfc_init(int kthreads, int quantum_us)
{
	assert(!inited);
	
	queue_init(&thrQueue);
	
	caddr_t schedStack = malloc(KFC_DEF_STACK_SIZE);
	scheduler.uc_stack.ss_sp = schedStack;
	scheduler.uc_stack.ss_size = KFC_DEF_STACK_SIZE;
	VALGRIND_STACK_REGISTER(schedStack, schedStack + KFC_DEF_STACK_SIZE);
	getcontext(&scheduler);

	makecontext(&scheduler, schedule_prot, 0);
	threadNo = 0;
	current_id = 0;
	inited = 1;


	return 0;
}


/**
 * Cleans up any resources which were allocated by kfc_init.  You may assume
 * that this function is called only from the main thread, that any other
 * threads have terminated and been joined, and that threading will not be
 * needed again.  (In other words, just clean up and don't worry about the
 * consequences.)
 *
 * I won't be testing this function specifically, but it is provided as a
 * convenience to you if you are using Valgrind to check your code, which I
 * always encourage.
 */
void
kfc_teardown(void)
{
	assert(inited);
	
	inited = 0;
	queue_destroy(&thrQueue);
	for(int i = 0; i < KFC_MAX_THREADS; i++){
		if(isMem[i] == 1){
		queue_destroy(&WaitList[i]);
		free(threadArray[i].uc_stack.ss_sp);
		}
	}
	if(scheduler.uc_stack.ss_sp != NULL){
	free(scheduler.uc_stack.ss_sp);
	}
}

/**
 * Creates a new user thread which executes the provided function concurrently.
 * It is left up to the implementation to decide whether the calling thread
 * continues to execute or the new thread takes over immediately.
 *
 * @param ptid[out]   Pointer to a tid_t variable in which to store the new
 *                    thread's ID
 * @param start_func  Thread main function
 * @param arg         Argument to be passed to the thread main function
 * @param stack_base  Location of the thread's stack if already allocated, or
 *                    NULL if requesting that the library allocate it
 *                    dynamically
 * @param stack_size  Size (in bytes) of the thread's stack, or 0 to use the
 *                    default thread stack size KFC_DEF_STACK_SIZE
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_create(tid_t *ptid, void *(*start_func)(void *), void *arg,
	   caddr_t stack_base, size_t stack_size)
{
 	assert(inited);
	size_t size;
	*ptid = ++threadNo;
	caddr_t stackbase;
	isMem[*ptid] = 1;
	if(stack_size == 0){
		size = KFC_DEF_STACK_SIZE;
	}else{
		size = stack_size;
	}
	if(stack_base == NULL){
		stackbase = malloc(size);
		VALGRIND_STACK_REGISTER(stackbase, stackbase + size);
		
	}else{
		stackbase = stack_base;
		isMem[*ptid] = 0;
	}
	
	getcontext(&threadArray[*ptid]);
	threadArray[*ptid].uc_stack.ss_size = size;
	threadArray[*ptid].uc_stack.ss_sp = stackbase;
	
	threadArray[*ptid].uc_link = &scheduler;
	
	
	makecontext(&threadArray[*ptid], trampoline, 2, start_func, arg);
	
	
	//int temp = current_id;
	
	
	
	isRunning[*ptid] = 1;
	
	queue_enqueue(&thrQueue, *ptid+1);
	queue_init(&WaitList[*ptid]);
	//current_id = *ptid;
	//swapcontext(&threadArray[current_id], &scheduler);
	return 0;

}



/**
 * Exits the calling thread.  This should be the same thing that happens when
 * the thread's start_func returns.
 *
 * @param ret  Return value from the thread
 */
void
kfc_exit(void *ret)
{
	assert(inited);
	int temp;
	returnArray[current_id] = ret;
	while(queue_peek(&WaitList[current_id]) != NULL){
		temp = queue_dequeue(&WaitList[current_id]) - 1;
		queue_enqueue(&thrQueue, temp + 1);
		}
	
	isRunning[current_id] = 0;
	swapcontext(&threadArray[current_id], &scheduler);
	
	return returnArray[current_id];
}

/**
 * Waits for the thread specified by tid to terminate, retrieving that threads
 * return value.  Returns immediately if the target thread has already
 * terminated, otherwise blocks.  Attempting to join a thread which already has
 * another thread waiting to join it, or attempting to join a thread which has
 * already been joined, results in undefined behavior.
 *
 * @param pret[out]  Pointer to a void * in which the thread's return value from
 *                   kfc_exit should be stored, or NULL if the caller does not
 *                   care.
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_join(tid_t tid, void **pret)
{
	// trampoline weird
	// Array for return values
	// 0 or null or something if not exit or new queue 
	// don't clear ret values
	assert(inited);
	if(isRunning[tid] == 1){
		queue_enqueue(&WaitList[tid], current_id + 1);
		swapcontext(&threadArray[current_id], &scheduler);
	}
	*pret = returnArray[tid];
	return &pret;
}

/**
 * Returns a small integer which identifies the calling thread.
 *
 * @return Thread ID of the currently executing thread
 */
tid_t
kfc_self(void)
{
	assert(inited);
	return current_id;
}

/**
 * Causes the calling thread to yield the processor voluntarily.  This may
 * result in another thread being scheduled, but it does not preclude the
 * possibility of the same thread continuing if re-chosen by the scheduling
 * algorithm.
 */
void
kfc_yield(void)
{
	assert(inited);
	queue_enqueue(&thrQueue, current_id + 1);
	swapcontext(&threadArray[current_id], &scheduler);

}

/**
 * Initializes a user-level counting semaphore with a specific value.
 *
 * @param sem    Pointer to the semaphore to be initialized
 * @param value  Initial value for the semaphore's counter
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_sem_init(kfc_sem_t *sem, int value)
{
	assert(inited);
	sem->value = value;
	queue_init(&sem->listT);
	return 0;
}

/**
 * Increments the value of the semaphore.  This operation is also known as
 * up, signal, release, and V (Dutch verhoog, "increase").
 *
 * @param sem  Pointer to the semaphore which the thread is releasing
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_sem_post(kfc_sem_t *sem)
{
	assert(inited);
	sem->value++;
	if(sem -> value <= 0){
		int temp = queue_dequeue(&sem->listT) - 1;
		queue_enqueue(&thrQueue, temp + 1);
	} 
	return 0;
}

/**
 * Attempts to decrement the value of the semaphore.  This operation is also
 * known as down, acquire, and P (Dutch probeer, "try").  This operation should
 * block when the counter is not above 0.
 *
 * @param sem  Pointer to the semaphore which the thread wishes to acquire
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_sem_wait(kfc_sem_t *sem)
{
	assert(inited);
	sem->value--;
	if(sem->value < 0){
		queue_enqueue(&sem->listT, current_id + 1);
		swapcontext(&threadArray[current_id], &scheduler);
	}
	return 0;
}

/**
 * Frees any resources associated with a semaphore.  Destroying a semaphore on
 * which threads are waiting results in undefined behavior.
 *
 * @param sem  Pointer to the semaphore to be destroyed
 */
void
kfc_sem_destroy(kfc_sem_t *sem)
{
	queue_destroy(&sem->listT);
}
