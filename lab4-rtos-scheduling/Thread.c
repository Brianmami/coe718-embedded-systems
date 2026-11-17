/*============================================================================
 *  COE 718 - Lab 3b, Question 2: Pre-emptive OS task simulation
 *  Comment out __USE_LCD (here AND in main.c) for the analysis version.
 *
 *  Lab 3a vs Lab 3b:
 *  - 3a: all tasks osPriorityNormal + round-robin ON -> tasks shared time
 *        slices; the order came only from creation order and signals.
 *  - 3b: tasks get priorities from the table + round-robin OFF
 *        (RTX_Conf_CM.c) -> the highest-priority ready task always runs and
 *        pre-empts lower ones; equal-priority tasks run until they block/yield.
 *
 *  Pre-emptive pros: critical OS tasks (Memory/CPU) always run first and
 *  respond immediately when they wake; no time-slice switching overhead.
 *  Pre-emptive cons: low-priority tasks (User) can starve; equal-priority
 *  tasks still need signals/yields; shared data needs mutexes (priority
 *  inversion, limited by RTX's priority inheritance).
 *===========================================================================*/

#include "cmsis_os.h"
#include "LPC17xx.h"
#include <string.h>
#include <stdio.h>

//#define __USE_LCD                // demo version. Must match main.c
#define __FI 1                   // 16x24 font (20 cols x 10 rows)

#ifdef __USE_LCD
  #include "GLCD.h"
  #include "LED.h"
#endif

/* Bit-banding: each bit of SRAM 0x20000000-0x200FFFFF has its own alias word
   at 0x22000000+. Writing 0/1 there clears/sets that one bit atomically.
   alias = 0x22000000 + (addr - 0x20000000)*32 + bit*4 */
#define MEM_ADDR   0x2007C000
#define MEM_LOC    (*(volatile unsigned int *)MEM_ADDR)
#define BITBAND_SRAM(addr, bit) \
  (*(volatile unsigned int *)(0x22000000 + (((addr) - 0x20000000) << 5) + ((bit) << 2)))

/* Watch window - expected values once tasksDone == 5 */
unsigned int memAccessCount = 0;   // -> 1
unsigned int bitbandResult  = 0;   // -> 0x29
unsigned int cpuAccessCount = 0;   // -> 1
unsigned int cpuResultA     = 0;   // -> 48
unsigned int cpuResultB     = 0;   // -> 17
unsigned int appCount       = 0;   // -> 1
unsigned int devCount       = 0;   // -> 1
unsigned int userCount      = 0;   // -> 1
char         logger[41]     = "";  // -> "Log: App started, Device done."
unsigned int tasksDone      = 0;   // -> 5 (Performance Analyzer breakpoint)

osMutexDef(log_mutex);   osMutexId log_mutex_id;    // protects logger
osMutexDef(done_mutex);  osMutexId done_mutex_id;   // protects tasksDone

#define SIG_MEM_TO_CPU  0x01     // Memory -> CPU:    "you may run"
#define SIG_CPU_TO_MEM  0x02     // CPU    -> Memory: "I'm finished"
#define SIG_APP_TO_DEV  0x04     // App    -> Device: "first half written"
#define SIG_DEV_TO_APP  0x08     // Device -> App:    "logger complete"

#ifdef __USE_LCD
  osMutexDef(lcd_mutex);  osMutexId lcd_mutex_id;
  const char *names[5] = {"Memory", "CPU", "App", "Device", "User"};

  /* Busy-wait, not osDelay: it keeps the CPU, so the demo delays don't
     change the priority-based order */
  void demo_delay(void) { volatile int d; for (d = 0; d < 2000000; d++); }

  void show(unsigned int t, const char *msg) {
    char buf[21];
    unsigned int i;
    osMutexWait(lcd_mutex_id, osWaitForever);
    for (i = 0; i < 5; i++) {
      LED_Off(i);
      GLCD_DisplayString(1 + i, 0, __FI, (unsigned char *)(i == t ? ">" : " "));
    }
    LED_On(t);
    sprintf(buf, "%-6s %-12s", names[t], msg);
    GLCD_DisplayString(1 + t, 1, __FI, (unsigned char *)buf);
    demo_delay();
    osMutexRelease(lcd_mutex_id);
  }

  void show_logger(void) {
    char copy[41], line[21];
    unsigned int len;
    osMutexWait(log_mutex_id, osWaitForever);
    strcpy(copy, logger);
    osMutexRelease(log_mutex_id);
    len = strlen(copy);

    osMutexWait(lcd_mutex_id, osWaitForever);
    GLCD_DisplayString(7, 0, __FI, (unsigned char *)"Logger:             ");
    memset(line, ' ', 20); line[20] = 0;
    memcpy(line, copy, len > 20 ? 20 : len);
    GLCD_DisplayString(8, 0, __FI, (unsigned char *)line);
    memset(line, ' ', 20);
    if (len > 20) memcpy(line, copy + 20, len - 20);
    GLCD_DisplayString(9, 0, __FI, (unsigned char *)line);
    demo_delay();
    osMutexRelease(lcd_mutex_id);
  }

  #define SHOW(t, m)  show(t, m)
  #define SHOW_LOG()  show_logger()
#else
  #define SHOW(t, m)
  #define SHOW_LOG()
#endif

void task_done(void) {
  osMutexWait(done_mutex_id, osWaitForever);
  tasksDone++;
  osMutexRelease(done_mutex_id);
}

/* Conditional execution + barrel shifter:
     if (a > b) return a + (b << 2);  else return b - (a >> 1); */
__asm unsigned int cond_barrel(unsigned int a, unsigned int b) {
  CMP     r0, r1
  ITE     HI
  ADDHI   r0, r0, r1, LSL #2
  SUBLS   r0, r1, r0, LSR #1
  BX      lr
}

void MemMgmt  (void const *argument);
void CPUMgmt  (void const *argument);
void AppIntf  (void const *argument);
void DevMgmt  (void const *argument);
void UserIntf (void const *argument);

osThreadId tid_mem, tid_cpu, tid_app, tid_dev, tid_user;

/* PRIORITIES SET HERE (3a: all osPriorityNormal).
   Lab table (lower number = higher priority) -> CMSIS-RTOS priority */
osThreadDef(MemMgmt,  osPriorityHigh,         1, 0);   // priority 1
osThreadDef(CPUMgmt,  osPriorityHigh,         1, 0);   // priority 1
osThreadDef(AppIntf,  osPriorityAboveNormal,  1, 0);   // priority 2
osThreadDef(DevMgmt,  osPriorityAboveNormal,  1, 0);   // priority 2
osThreadDef(UserIntf, osPriorityBelowNormal,  1, 0);   // priority 4 (lowest)

int Init_Thread (void) {
  log_mutex_id  = osMutexCreate(osMutex(log_mutex));
  done_mutex_id = osMutexCreate(osMutex(done_mutex));
#ifdef __USE_LCD
  lcd_mutex_id  = osMutexCreate(osMutex(lcd_mutex));
#endif

  /* NEW in 3b - PRIORITY USED: main is raised above every task so all five
     are created before any runs. Otherwise Memory (High) would pre-empt main
     as soon as it was created and signal CPU before tid_cpu existed. */
  osThreadSetPriority(osThreadGetId(), osPriorityRealtime);

  /* Execution starts with Memory Management: created first, and CPU (same
     priority) is blocked waiting for Memory's signal */
  tid_mem  = osThreadCreate(osThread(MemMgmt),  NULL);
  tid_cpu  = osThreadCreate(osThread(CPUMgmt),  NULL);
  tid_app  = osThreadCreate(osThread(AppIntf),  NULL);
  tid_dev  = osThreadCreate(osThread(DevMgmt),  NULL);
  tid_user = osThreadCreate(osThread(UserIntf), NULL);

  if (!tid_mem || !tid_cpu || !tid_app || !tid_dev || !tid_user) return -1;
  return 0;
}

/* Memory Management - priority 1 */
void MemMgmt (void const *argument) {
  SHOW(0, "Running");
  memAccessCount++;

  MEM_LOC = 0x21;                          // 0010 0001
  BITBAND_SRAM(MEM_ADDR, 3) = 1;           // set bit 3   -> 0x29
  BITBAND_SRAM(MEM_ADDR, 0) = 0;           // clear bit 0 -> 0x28
  bitbandResult = MEM_LOC + BITBAND_SRAM(MEM_ADDR, 5);   // 0x28 + 1 = 0x29
  SHOW(0, "Bit-band");

  /* NEW in 3b - PRIORITY USED: CPU has the same priority, so the signal alone
     doesn't pre-empt Memory (no round-robin). The yield hands control over. */
  osSignalSet(tid_cpu, SIG_MEM_TO_CPU);
  osThreadYield();

  SHOW(0, "Wait CPU");
  osSignalWait(SIG_CPU_TO_MEM, osWaitForever);

  SHOW(0, "Delay 1 tick");
  osDelay(1);
  /* PRIORITY USED: on waking, Memory pre-empts any lower-priority task */
  SHOW(0, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}

/* CPU Management - priority 1 */
void CPUMgmt (void const *argument) {
  osSignalWait(SIG_MEM_TO_CPU, osWaitForever);
  SHOW(1, "Running");
  cpuAccessCount++;

  cpuResultA = cond_barrel(20, 7);         // 20 > 7  -> 20 + (7 << 2) = 48
  cpuResultB = cond_barrel(7, 20);         // 7 <= 20 -> 20 - (7 >> 1) = 17
  SHOW(1, "Barrel shift");

  osSignalSet(tid_mem, SIG_CPU_TO_MEM);
  SHOW(1, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}

/* Application Interface - priority 2
   PRIORITY USED: only gets the CPU once Memory and CPU are blocked/deleted */
void AppIntf (void const *argument) {
  SHOW(2, "Running");

  osMutexWait(log_mutex_id, osWaitForever);
  strcpy(logger, "Log: App started, ");
  osMutexRelease(log_mutex_id);            // release before waiting, or deadlock
  SHOW_LOG();

  osSignalSet(tid_dev, SIG_APP_TO_DEV);    // guarantees App runs before Device
  SHOW(2, "Wait Device");
  osSignalWait(SIG_DEV_TO_APP, osWaitForever);

  appCount++;
  SHOW(2, "Delay 1 tick");
  osDelay(1);
  SHOW(2, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}

/* Device Management - priority 2 */
void DevMgmt (void const *argument) {
  osSignalWait(SIG_APP_TO_DEV, osWaitForever);
  SHOW(3, "Running");

  osMutexWait(log_mutex_id, osWaitForever);
  strcat(logger, "Device done.");
  osMutexRelease(log_mutex_id);
  SHOW_LOG();

  osSignalSet(tid_app, SIG_DEV_TO_APP);
  devCount++;
  SHOW(3, "Delay 1 tick");
  osDelay(1);                              // "close the file"
  SHOW(3, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}

/* User Interface - priority 4
   PRIORITY USED: lowest, so it only runs when every other task is blocked */
void UserIntf (void const *argument) {
  SHOW(4, "Running");
  userCount++;
  SHOW(4, "Delay 1 tick");
  osDelay(1);
  SHOW(4, "Deleted");
  task_done();
  osThreadTerminate(osThreadGetId());
}