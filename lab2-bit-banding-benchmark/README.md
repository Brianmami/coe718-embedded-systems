# COE 718 Lab 2 – Bit-Banding, Masking, Conditional Execution and Barrel Shifter

**Course:** COE 718 – Embedded Systems Design  
**Tools:** C, Keil µVision 5, ARM Cortex-M3 (NXP LPC1768)

## Overview

Compares three ways to toggle GPIO LEDs on the Cortex-M3: read-modify-write masking, a bit-band alias computed by a function, and a hand-calculated direct bit-band alias. Each method is timed with the DWT cycle counter at -O0 and -O3. The program also demonstrates conditional execution (IT blocks) and barrel-shifter arithmetic (`x*8 + 1`), with joystick-selected modes and LCD output.

Screenshots of the performance-analyzer results are in `screenshots/`.

## Build / run

Open the `.uvprojx` project in Keil µVision 5 (MCB1700 / LPC1768 device pack installed), build, then run on the board or in the simulator. µVision regenerates the `RTE/` device files from the project settings.
