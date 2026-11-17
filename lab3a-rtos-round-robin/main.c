//part 2

#define osObjectsPublic
#include "osObjects.h"
#include "LPC17xx.h"
#include "cmsis_os.h"

//#define __USE_LCD        // comment this out for the ANALYSIS version

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
  GLCD_DisplayString(0, 0, 1, "   OS Round-Robin   ");
  GLCD_SetBackColor(White);
  GLCD_SetTextColor(Blue);
#endif

  Init_Thread();                    // Memory Management is created first
  osKernelStart();                  // start thread switching
  osDelay(osWaitForever);
}