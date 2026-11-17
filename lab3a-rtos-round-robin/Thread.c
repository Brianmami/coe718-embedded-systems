/*============================================================================
 *  COE 718 - Lab 3a, Question 2: Round-robin OS task simulation
 *  Comment out __USE_LCD (here AND in main.c) for the analysis version.
 
 Pro Of ROUNDROBIN:
 
 - its fair and makes it so that there is no starvation. All 5 of the threads run with equal priority
 - tasks that are blocked, like a device waiting on a signal, use no CPU time
 
 Con Of ROUNDROBIN:
 
 - round robin favours CPU bound processes, so scheduler can't enforce Memory->CPU or App->Device order; every dependency needs hand-written signals, and a missed one hangs a task.
 *===========================================================================*/

#include "cmsis_os.h"            // CMSIS-RTOS API (threads, mutexes, signals)
#include "LPC17xx.h"             // LPC1768 register definitions
#include <string.h>              // strcpy, strcat, memset, memcpy, strlen
#include <stdio.h>               // sprintf (formatting LCD lines)

//#define __USE_LCD                // demo version: LCD + LEDs on. Must match main.c
#define __FI 1                   // GLCD font index 1 = 16x24 font (20 cols x 10 rows)

#ifdef __USE_LCD
  #include "GLCD.h"              // LCD driver
  #include "LED.h"               // LED driver
#endif

/*----------------------------------------------------------------------------
 *  Bit-banding
 *  The Cortex-M3 maps every bit of the SRAM bit-band region
 *  (0x20000000-0x200FFFFF) to its own 32-bit word in the alias region
 *  (starting at 0x22000000). Writing 0/1 to an alias word clears/sets that
 *  single bit in ONE atomic store - no read-modify-write, so a context
 *  switch cannot corrupt it.
 *
 *  The LPC1768 AHB SRAM (0x2007C000) lies inside the bit-band region.
 *  The main SRAM at 0x10000000 does NOT, so it can't be used here.
 *
 *  alias = 0x22000000 + (byte_offset * 32) + (bit * 4)
 *        byte_offset = addr - 0x20000000
 *        "<< 5" = *32,  "<< 2" = *4
 *---------------------------------------------------------------------------*/
#define MEM_ADDR   0x2007C000                              // the volatile word we operate on
#define MEM_LOC    (*(volatile unsigned int *)MEM_ADDR)    // access it as a whole 32-bit word
#define BITBAND_SRAM(addr, bit) \
  (*(volatile unsigned int *)(0x22000000 + (((addr) - 0x20000000) << 5) + ((bit) << 2)))
  /* e.g. bit 5 of 0x2007C000 -> 0x22000000 + 0xF80000 + 0x14 = 0x22F80014 */

/*----------------------------------------------------------------------------
 *  Global variables - one per task, as the spec requires.
 *  Add these to the Watch window. Expected values once tasksDone == 5 are
 *  shown on the right.
 *---------------------------------------------------------------------------*/
unsigned int memAccessCount = 0;   // Memory Mgmt access counter          -> 1
unsigned int bitbandResult  = 0;   // result of the bit-band computation  -> 0x29 (41)
unsigned int cpuAccessCount = 0;   // CPU Mgmt access counter             -> 1
unsigned int cpuResultA     = 0;   // cond_barrel(20, 7)                  -> 48
unsigned int cpuResultB     = 0;   // cond_barrel(7, 20)                  -> 17
unsigned int appCount       = 0;   // Application Interface counter       -> 1
unsigned int devCount       = 0;   // Device Management counter           -> 1
unsigned int userCount      = 0;   // User Interface: number of users     -> 1
char         logger[41]     = "";  // shared "file", written by App then Device
                                   //   -> "Log: App started, Device done."
unsigned int tasksDone      = 0;   // incremented as each task deletes itself -> 5
/* Each counter ends at 1 because every task is a finite workload: it runs its
   body once, increments its counter once, then deletes itself. */

/* Mutexes: only one thread at a time may touch the protected resource */
osMutexDef(log_mutex);   osMutexId log_mutex_id;    // protects logger[]
osMutexDef(done_mutex);  osMutexId done_mutex_id;   // protects tasksDone

/* Signal flags. Each thread has its own set of flags; a thread waiting on a
   flag is BLOCKED (uses no CPU) until another thread sets it. */
#define SIG_MEM_TO_CPU  0x01     // Memory -> CPU:    "you may run now"
#define SIG_CPU_TO_MEM  0x02     // CPU    -> Memory: "I'm finished"
#define SIG_APP_TO_DEV  0x04     // App    -> Device: "my half of logger is written"
#define SIG_DEV_TO_APP  0x08     // Device -> App:    "logger is complete"

/*----------------------------------------------------------------------------
 *  Demo-only display: LED 0-4 and a ">" marker show which task is running.
 *  None of this is compiled in the analysis version.
 *---------------------------------------------------------------------------*/
#ifdef __USE_LCD
  osMutexDef(lcd_mutex);  osMutexId lcd_mutex_id;   // only one thread draws at a time
  const char *names[5] = {"Memory", "CPU", "App", "Device", "User"};   // index = task/LED number

  /* Busy-wait so each step stays on screen long enough to see.
     'volatile' stops the compiler optimizing the empty loop away. */
  void demo_delay(void) { volatile int d; for (d = 0; d < 2000000; d++); }

  /* Show task t's status on its own LCD row and light its LED.
     The delay is INSIDE the mutex: the display stays on this task until it
     releases, and the other threads queue (first come, first served) for
     their turn. That queue is what sets the order seen on the board. */
  void show(unsigned int t, const char *msg) {
    char buf[21];                                     // 20 chars + terminator
    unsigned int i;
    osMutexWait(lcd_mutex_id, osWaitForever);         // take the LCD
    for (i = 0; i < 5; i++) {                         // clear all LEDs and markers
      LED_Off(i);
      GLCD_DisplayString(1 + i, 0, __FI, (unsigned char *)(i == t ? ">" : " "));
    }
    LED_On(t);                                        // LED for the running task
    sprintf(buf, "%-6s %-12s", names[t], msg);        // pad so old text is overwritten
    GLCD_DisplayString(1 + t, 1, __FI, (unsigned char *)buf);   // row 1-5 = tasks 0-4
    demo_delay();                                     // hold the display
    osMutexRelease(lcd_mutex_id);                     // next waiting task may draw
  }

  /* Show the current contents of logger on rows 7-9 (two lines of 20 chars). */
  void show_logger(void) {
    char copy[41], line[21];
    unsigned int len;
    osMutexWait(log_mutex_id, osWaitForever);         // read logger safely...
    strcpy(copy, logger);                             // ...into a local copy
    osMutexRelease(log_mutex_id);                     // release fast; never hold
    len = strlen(copy);                               // log_mutex while drawing

    osMutexWait(lcd_mutex_id, osWaitForever);         // take the LCD
    GLCD_DisplayString(7, 0, __FI, (unsigned char *)"Logger:             ");
    memset(line, ' ', 20); line[20] = 0;              // blank line
    memcpy(line, copy, len > 20 ? 20 : len);          // first 20 chars
    GLCD_DisplayString(8, 0, __FI, (unsigned char *)line);
    memset(line, ' ', 20);
    if (len > 20) memcpy(line, copy + 20, len - 20);  // remaining chars (max 40 total)
    GLCD_DisplayString(9, 0, __FI, (unsigned char *)line);
    demo_delay();
    osMutexRelease(lcd_mutex_id);
  }

  #define SHOW(t, m)  show(t, m)      // demo: real display calls
  #define SHOW_LOG()  show_logger()
#else
  #define SHOW(t, m)                  // analysis: macros expand to nothing,
  #define SHOW_LOG()                  // so the task code stays identical
#endif

/* Called by each task just before it deletes itself. The mutex makes the
   increment safe even if two tasks finish in the same time slice.
   tasksDone == 5 is the breakpoint condition for the Performance Analyzer. */
void task_done(void) {
  osMutexWait(done_mutex_id, osWaitForever);
  tasksDone++;
  osMutexRelease(done_mutex_id);
}

/*----------------------------------------------------------------------------
 *  CPU hardware check: conditional execution + barrel shifter
 *  Embedded assembly (ARM Compiler 5). Arguments arrive in r0 = a, r1 = b;
 *  the return value goes back in r0.
 *
 *    if (a > b) return a + (b << 2);
 *    else       return b - (a >> 1);
 *
 *  Thumb-2 needs an IT ("If-Then") block to execute instructions
 *  conditionally. The shifts are done by the barrel shifter INSIDE the
 *  ADD/SUB instruction, at no extra cost.
 *---------------------------------------------------------------------------*/
__asm unsigned int cond_barrel(unsigned int a, unsigned int b) {
  CMP     r0, r1                  ; compare a with b, set the flags
  ITE     HI                      ; If-Then-Else: next instr if HI, the one after if LS
  ADDHI   r0, r0, r1, LSL #2      ; HI (a > b, unsigned):  r0 = a + (b << 2)
  SUBLS   r0, r1, r0, LSR #1      ; LS (a <= b, unsigned): r0 = b - (a >> 1)
  BX      lr                      ; return, result in r0
}

/*----------------------------------------------------------------------------
 *  Threads - all osPriorityNormal, so RTX schedules them round-robin
 *  osThreadDef(name, priority, instances, stacksize): 0 = default stack size
 *  (set it to 512+ in RTX_Conf_CM.c because of the LCD/sprintf calls)
 *---------------------------------------------------------------------------*/
void MemMgmt  (void const *argument);
void CPUMgmt  (void const *argument);
void AppIntf  (void const *argument);
void DevMgmt  (void const *argument);
void UserIntf (void const *argument);

osThreadId tid_mem, tid_cpu, tid_app, tid_dev, tid_user;   // IDs used to send signals
osThreadDef(MemMgmt,  osPriorityNormal, 1, 0);
osThreadDef(CPUMgmt,  osPriorityNormal, 1, 0);
osThreadDef(AppIntf,  osPriorityNormal, 1, 0);
osThreadDef(DevMgmt,  osPriorityNormal, 1, 0);
osThreadDef(UserIntf, osPriorityNormal, 1, 0);

/* Called from main() before osKernelStart(). */
int Init_Thread (void) {
  /* Mutexes must exist before any thread tries to use them */
  log_mutex_id  = osMutexCreate(osMutex(log_mutex));
  done_mutex_id = osMutexCreate(osMutex(done_mutex));
#ifdef __USE_LCD
  lcd_mutex_id  = osMutexCreate(osMutex(lcd_mutex));
#endif

  /* Equal-priority threads join the ready queue in creation order, so
     Memory Management is created first and therefore runs first. */
  tid_mem  = osThreadCreate(osThread(MemMgmt),  NULL);
  tid_cpu  = osThreadCreate(osThread(CPUMgmt),  NULL);
  tid_app  = osThreadCreate(osThread(AppIntf),  NULL);
  tid_dev  = osThreadCreate(osThread(DevMgmt),  NULL);
  tid_user = osThreadCreate(osThread(UserIntf), NULL);

  /* A NULL ID means the thread couldn't be created (usually too little
     stack or too few "concurrent running threads" in RTX_Conf_CM.c) */
  if (!tid_mem || !tid_cpu || !tid_app || !tid_dev || !tid_user) return -1;
  return 0;
}

/*----------------------------------------------------------------------------
 *  Memory Management
 *  - increments its access counter
 *  - bit-band computation on a volatile memory location
 *  - passes control to CPU Management
 *  - waits for CPU's signal back, delays 1 tick, deletes itself
 *---------------------------------------------------------------------------*/
void MemMgmt (void const *argument) {
  SHOW(0, "Running");
  memAccessCount++;

  /* Bit-band computation on a volatile SRAM word */
  MEM_LOC = 0x21;                          // whole word = 0010 0001 (bits 5 and 0 set)
  BITBAND_SRAM(MEM_ADDR, 3) = 1;           // set bit 3 atomically   -> 0010 1001 (0x29)
  BITBAND_SRAM(MEM_ADDR, 0) = 0;           // clear bit 0 atomically -> 0010 1000 (0x28)
  bitbandResult = MEM_LOC + BITBAND_SRAM(MEM_ADDR, 5);   // read bit 5 (=1): 0x28 + 1 = 0x29
  SHOW(0, "Bit-band");

  osSignalSet(tid_cpu, SIG_MEM_TO_CPU);    // pass control: unblocks CPU Management
  SHOW(0, "Wait CPU");
  osSignalWait(SIG_CPU_TO_MEM, osWaitForever);   // block until CPU signals back

  SHOW(0, "Delay 1 tick");
  osDelay(1);                              // give up the CPU for one OS tick
  SHOW(0, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());      // delete itself
}

/*----------------------------------------------------------------------------
 *  CPU Management
 *  - waits until Memory Management passes control
 *  - increments its access counter
 *  - conditional execution + barrel shifting (internal hardware check)
 *  - signals back to Memory Management, deletes itself
 *---------------------------------------------------------------------------*/
void CPUMgmt (void const *argument) {
  osSignalWait(SIG_MEM_TO_CPU, osWaitForever);   // blocked until Memory passes control
  SHOW(1, "Running");
  cpuAccessCount++;

  /* Call twice with swapped arguments so BOTH conditional paths execute */
  cpuResultA = cond_barrel(20, 7);         // 20 > 7  -> ADDHI: 20 + (7 << 2) = 20 + 28 = 48
  cpuResultB = cond_barrel(7, 20);         // 7 <= 20 -> SUBLS: 20 - (7 >> 1) = 20 - 3  = 17
  SHOW(1, "Barrel shift");

  osSignalSet(tid_mem, SIG_CPU_TO_MEM);    // signal back: unblocks Memory Management
  SHOW(1, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());      // delete itself
}

/*----------------------------------------------------------------------------
 *  Application Interface
 *  - must execute before Device Management
 *  - writes a partial message to logger (under the mutex)
 *  - waits for Device Management to finish writing logger
 *  - increments its counter, delays 1 tick, deletes itself
 *---------------------------------------------------------------------------*/
void AppIntf (void const *argument) {
  SHOW(2, "Running");

  osMutexWait(log_mutex_id, osWaitForever);    // exclusive access to logger
  strcpy(logger, "Log: App started, ");        // partial message
  osMutexRelease(log_mutex_id);                // MUST release before waiting -
                                               // otherwise Device could never
                                               // lock logger = deadlock
  SHOW_LOG();

  osSignalSet(tid_dev, SIG_APP_TO_DEV);        // App has run -> Device may start
  SHOW(2, "Wait Device");
  osSignalWait(SIG_DEV_TO_APP, osWaitForever); // block until Device finishes logger

  appCount++;
  SHOW(2, "Delay 1 tick");
  osDelay(1);
  SHOW(2, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}

/*----------------------------------------------------------------------------
 *  Device Management
 *  - waits for App Interface (guarantees App runs first)
 *  - writes the ending of the logger message (under the mutex)
 *  - signals back to App Interface
 *  - increments its counter, delays 1 tick ("file closed"), deletes itself
 *---------------------------------------------------------------------------*/
void DevMgmt (void const *argument) {
  osSignalWait(SIG_APP_TO_DEV, osWaitForever);   // blocked until App has written its half
  SHOW(3, "Running");

  osMutexWait(log_mutex_id, osWaitForever);      // exclusive access to logger
  strcat(logger, "Device done.");                // append the ending
  osMutexRelease(log_mutex_id);
  SHOW_LOG();

  osSignalSet(tid_app, SIG_DEV_TO_APP);          // signal back: unblocks App
  devCount++;
  SHOW(3, "Delay 1 tick");
  osDelay(1);                                    // one tick to "close the file"
  SHOW(3, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}

/*----------------------------------------------------------------------------
 *  User Interface
 *  - independent of the other tasks
 *  - increments the number of users, delays 1 tick, deletes itself
 *---------------------------------------------------------------------------*/
void UserIntf (void const *argument) {
  SHOW(4, "Running");
  userCount++;
  SHOW(4, "Delay 1 tick");
  osDelay(1);
  SHOW(4, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}