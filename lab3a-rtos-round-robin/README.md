# COE 718 Lab 3a – CMSIS-RTOS Round-Robin Task Simulation

**Course:** COE 718 – Embedded Systems Design  
**Tools:** C, CMSIS-RTOS (Keil RTX), Keil µVision 5, LPC1768

## Overview

Five RTOS threads, scheduled round-robin, that simulate operating-system tasks. Each one does work on a shared word in the bit-band SRAM region, so single bits can be set atomically without a read-modify-write that a context switch could corrupt. The demo build shows task status on the GLCD and LEDs; `images.c` holds the bitmap data.

## Build / run

Open the `.uvprojx` project in Keil µVision 5 (MCB1700 / LPC1768 device pack installed), build, then run on the board or in the simulator. µVision regenerates the `RTE/` device files from the project settings.
