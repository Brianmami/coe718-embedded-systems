// COE 718 - Lab 3b, Question 2: Pre-emptive OS task simulation

#define osObjectsPublic
#include "osObjects.h"
#include "LPC17xx.h"
#include "cmsis_os.h"

//#define __USE_LCD        // comment this out for the ANALYSIS version (must match Thread.c)

#ifdef __USE_LCD
  #include "GLCD.h"
  #include "LED.h"
#endif

extern int Init_Thread (void);

int main (void) {
  SystemInit();
  osKernelInitialize();             // initialize CMSIS-RTOS

#ifdef __USE_LCD
  LED_Init();
  GLCD_Init();
  GLCD_Clear(White);
  GLCD_SetBackColor(Blue);
  GLCD_SetTextColor(White);
  GLCD_DisplayString(0, 0, 1, "   OS Pre-emptive   ");
  GLCD_SetBackColor(White);
  GLCD_SetTextColor(Blue);
#endif

  Init_Thread();                    // creates all tasks; Memory Management runs first
  osKernelStart();                  // start pre-emptive (priority-based) scheduling
  osDelay(osWaitForever);           // main blocks forever; tasks run by priority
}