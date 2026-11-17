/*----------------------------------------------------------------------------
 * COE718 Lab 4 - Part 2: Mars Pathfinder priority inversion
 *
 * Resource R1 is a plain Boolean (NOT os_mut, which has built-in
 * priority inheritance and would hide the problem).
 *
 *   taskA - high priority,   released at 60 ms, needs R1
 *   taskB - medium priority, released at 50 ms, does not use R1
 *   taskC - low priority,    released at 0 ms,  locks R1
 *
 * PROBLEM  (SOLUTION commented out):
 *   0-20  C runs, 20-50 C holds R1, 50 B preempts C, 60 A preempts B,
 *   A finds R1 busy and spins forever -> C can never run to release R1,
 *   B is also starved. A, B and C are all stuck behind low-priority C.
 *
 * SOLUTION (#define SOLUTION): priority inheritance.
 *   When A finds R1 busy it raises C (the holder) to A's priority and
 *   yields. C finishes its critical section, frees R1 and drops back to
 *   low priority. Expected order:
 *   0-20 C | 20-50 C(R1) | 50-60 B | 60-90 C(R1, boosted) |
 *   90-110 A(R1) | 110-140 B | 140-150 C
 *--------------------------------------------------------------------------*/

#include "cmsis_os.h"
#include <stdbool.h>

#define SOLUTION                 // <<< uncomment for the priority-inheritance solution

#define LOOPS_PER_MS  10000        // use the calibrated value from rms.c

/*----- Shared resource R1: Boolean instead of an RTX mutex -----*/
volatile bool R1_busy = false;     // true while some task owns R1

void taskA (void const *argument);
void taskB (void const *argument);
void taskC (void const *argument);

osThreadDef(taskA, osPriorityHigh,   1, 0);   // highest
osThreadDef(taskB, osPriorityNormal, 1, 0);   // medium
osThreadDef(taskC, osPriorityLow,    1, 0);   // lowest

osThreadId t_main, t_A, t_B, t_C;

/* Busy-wait: consumes CPU time (not osDelay, so the task stays running) */
void work(unsigned int ms){
	volatile unsigned long i;
	while (ms--) {
		for (i = 0; i < LOOPS_PER_MS; i++);
	}
}

/*------------------------- taskC: low priority --------------------------*/
void taskC (void const *argument){
	work(20);                       // 0-20 ms: normal work

	R1_busy = true;                 // lock R1
	work(60);                       // critical section on R1 (preempted by B at 50 ms)
	R1_busy = false;                // release R1

#ifdef SOLUTION
	osThreadSetPriority(t_C, osPriorityLow);  // drop inherited priority back to low
#endif

	work(10);                       // remaining non-critical work
	osThreadTerminate(osThreadGetId());
}

/*------------------------ taskB: medium priority ------------------------*/
void taskB (void const *argument){
	osDelay(50);                    // released at 50 ms
	work(40);                       // does not need R1
	osThreadTerminate(osThreadGetId());
}

/*------------------------- taskA: high priority -------------------------*/
void taskA (void const *argument){
	osDelay(60);                    // released at 60 ms

	if (R1_busy) {
#ifdef SOLUTION
		/* Priority inheritance: boost the holder of R1 to A's priority */
		osThreadSetPriority(t_C, osPriorityHigh);
#endif
		while (R1_busy) {           // wait for R1
#ifdef SOLUTION
			osThreadYield();        // let boosted C (same priority) run
#endif
			/* PROBLEM: without SOLUTION, A spins here forever. C is low
			   priority, never gets the CPU, and never frees R1. */
		}
	}

	R1_busy = true;                 // lock R1
	work(20);                       // A's critical section on R1
	R1_busy = false;                // release R1

	osThreadTerminate(osThreadGetId());
}

/*--------------------------------- main ---------------------------------*/
int main(void){
	osKernelInitialize();

	/* Keep main above all tasks while creating them */
	t_main = osThreadGetId();
	osThreadSetPriority(t_main, osPriorityRealtime);

	t_C = osThreadCreate(osThread(taskC), NULL);
	t_B = osThreadCreate(osThread(taskB), NULL);
	t_A = osThreadCreate(osThread(taskA), NULL);

	osKernelStart();
	osThreadTerminate(t_main);       // main no longer needed
	for (;;);
}