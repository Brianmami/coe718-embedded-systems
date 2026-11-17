# COE 718 Lab 4 – Real-Time Scheduling: RMS and Priority Inversion

**Course:** COE 718 – Embedded Systems Design  
**Tools:** C, CMSIS-RTOS (Keil RTX), Keil µVision 5, LPC1768

## Overview

Real-time scheduling experiments on CMSIS-RTOS:

- `rms.c` – rate-monotonic scheduling of periodic threads
- `priority_inv.c` – a priority-inversion scenario
- `mars_pf.c` – a re-creation of the Mars Pathfinder priority-inversion bug and its fix (priority inheritance)
- `virtual_demo.c` – virtual timer demo

## Build / run

Open the `.uvprojx` project in Keil µVision 5 (MCB1700 / LPC1768 device pack installed), build, then run on the board or in the simulator. µVision regenerates the `RTE/` device files from the project settings.
