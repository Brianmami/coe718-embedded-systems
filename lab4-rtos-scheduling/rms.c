/*----------------------------------------------------------------------------
 * COE718 Lab 4 - Part 1: Rate Monotonic Scheduling (RMS)
 *
 * Process set (Table 1), times in ms:
 *   Process  Period(T)  Compute(C)  RMS Priority
 *     A        40000      20000       3 (lowest)  -> osPriorityNormal
 *     B        40000      10000       2           -> osPriorityAboveNormal
 *     C        20000       5000       1 (highest) -> osPriorityHigh
 *
 * U = 20000/40000 + 10000/40000 + 5000/20000 = 1.0
 * Periods are harmonic (20000 divides 40000), so the set is
 * schedulable under RMS even at U = 1.
 *
 * Expected timeline (one hyperperiod 0-40000 ms, then repeats):
 *   0     - 5000   C
 *   5000  - 15000  B
 *   15000 - 20000  A   (5000 of 20000 done)
 *   20000 - 25000  C   (released again, preempts A)
 *   25000 - 40000  A   (remaining 15000, finishes at deadline)
 *
 * Mechanism: virtual timers release each job with osSignalSet();
 * each thread blocks in osSignalWait() until released, then
 * "computes" using a busy-wait delay. The running thread's LED is on.
 *   LED 0 = A, LED 1 = B, LED 2 = C
 *--------------------------------------------------------------------------*/

#include "cmsis_os.h"
#include "LED.h"

/*------------------------- Timing configuration -------------------------*/
#define SCALE         100       // 1 = real lab timing; 100 = 100x faster for debugging
#define LOOPS_PER_MS  10000   // busy-loop iterations per ms -- CALIBRATE THIS

#define PERIOD_AB   (40000 / SCALE)
#define PERIOD_C    (20000 / SCALE)
#define COMPUTE_A   (20000 / SCALE)
#define COMPUTE_B   (10000 / SCALE)
#define COMPUTE_C   ( 5000 / SCALE)

#define SIG_RELEASE 0x01      // signal flag used to release a job

/* Which task is executing (watch it in the debugger): 0=idle 1=A 2=B 3=C */
volatile unsigned int running = 0;

/*------------------------------ Threads ---------------------------------*/
void taskA (void const *argument);
void taskB (void const *argument);
void taskC (void const *argument);

osThreadDef(taskA, osPriorityNormal,      1, 0);   // RMS priority 3 (lowest)
osThreadDef(taskB, osPriorityAboveNormal, 1, 0);   // RMS priority 2
osThreadDef(taskC, osPriorityHigh,        1, 0);   // RMS priority 1 (highest)

osThreadId t_main, t_A, t_B, t_C;

/*--------------------------------------------------------------------------
 * compute(): busy-wait for 'ms' milliseconds of CPU time.
 * Must NOT use osDelay(): the thread has to keep the CPU so a
 * higher-priority release can preempt it, as RMS requires.
 * A loop count measures CPU time actually used, so a preempted
 * task correctly resumes its remaining work afterwards.
 *--------------------------------------------------------------------------*/
void compute(unsigned int ms, unsigned int led, unsigned int id){
	volatile unsigned long i;
	while (ms--) {
		running = id;
		LED_Out(1UL << led);              // only the running task's LED on
		for (i = 0; i < LOOPS_PER_MS; i++);
	}
	LED_Out(0);
	running = 0;
}

/*--------------------------- Virtual timers -----------------------------*/
void timer_cb(void const *param){
	switch ((uint32_t)param) {
		case 0:                           // every 20000 ms: release C
			osSignalSet(t_C, SIG_RELEASE);
			break;
		case 1:                           // every 40000 ms: release A and B
			osSignalSet(t_B, SIG_RELEASE);
			osSignalSet(t_A, SIG_RELEASE);
			break;
	}
}
osTimerDef(timerC_def,  timer_cb);
osTimerDef(timerAB_def, timer_cb);

/*---------------------------- Task bodies -------------------------------*/
void taskA (void const *argument){
	for (;;) {
		osSignalWait(SIG_RELEASE, osWaitForever);   // wait for release
		compute(COMPUTE_A, 0, 1);                   // 20000 ms on LED 0
	}
}

void taskB (void const *argument){
	for (;;) {
		osSignalWait(SIG_RELEASE, osWaitForever);
		compute(COMPUTE_B, 1, 2);                   // 10000 ms on LED 1
	}
}

void taskC (void const *argument){
	for (;;) {
		osSignalWait(SIG_RELEASE, osWaitForever);
		compute(COMPUTE_C, 2, 3);                   // 5000 ms on LED 2
	}
}

/*-------------------------------- main ----------------------------------*/
int main(void){
	osTimerId timC, timAB;

	osKernelInitialize();
	LED_Init();

	/* Raise main above every task so nothing runs until setup is done */
	t_main = osThreadGetId();
	osThreadSetPriority(t_main, osPriorityRealtime);

	t_A = osThreadCreate(osThread(taskA), NULL);
	t_B = osThreadCreate(osThread(taskB), NULL);
	t_C = osThreadCreate(osThread(taskC), NULL);

	timC  = osTimerCreate(osTimer(timerC_def),  osTimerPeriodic, (void *)0);
	timAB = osTimerCreate(osTimer(timerAB_def), osTimerPeriodic, (void *)1);

	osTimerStart(timC,  PERIOD_C);
	osTimerStart(timAB, PERIOD_AB);

	/* First release of every job at t = 0 (timers first fire at t = period) */
	osSignalSet(t_C, SIG_RELEASE);
	osSignalSet(t_B, SIG_RELEASE);
	osSignalSet(t_A, SIG_RELEASE);

	osKernelStart();
	osThreadTerminate(t_main);              // main no longer needed; scheduler takes over
	for (;;);
}