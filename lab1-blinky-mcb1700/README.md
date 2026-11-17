# COE 718 Lab 1 – Blinky on the MCB1700 (LPC1768)

**Course:** COE 718 – Embedded Systems Design  
**Tools:** C, Keil µVision 5, ARM Cortex-M3 (NXP LPC1768)

## Overview

Introductory embedded lab based on Keil's Blinky example: SysTick-driven LED flashing, ADC reading in the interrupt handler (`IRQ.c`), joystick input (`KBD.c`) and GLCD output. Used to learn the µVision debugger, the performance analyzer and ITM `printf`.

## Build / run

Open the `.uvprojx` project in Keil µVision 5 (MCB1700 / LPC1768 device pack installed), build, then run on the board or in the simulator. µVision regenerates the `RTE/` device files from the project settings.
