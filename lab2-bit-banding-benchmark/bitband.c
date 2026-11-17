/*============================================================================
 * COE718 Lab 2 - Bit Banding, Masking, Conditional Execution, Barrel Shifter
 * Board : Keil MCB1700 (NXP LPC1768, ARM Cortex-M3 @ 100 MHz)
 * Author: Brian
 *
 * ONE FILE, TWO VERSIONS
 *   - Debug (Performance Analysis) version: leave TARGET_VERSION commented out.
 *       * No delays (they would hide the real cost of each method).
 *       * run_benchmark() times each LED method with the DWT cycle counter
 *         and prints the result through ITM printf.
 *   - Target (Demo) version: uncomment TARGET_VERSION.
 *       * A delay() follows every LED on/off so you can see the LEDs change
 *         (and so you can see the GPIO pin checkboxes change in the simulator).
 *   Save the file twice (e.g. lab2_debug.c and lab2_target.c) with the
 *   switch set differently, and hand both in as the manual asks.
 *
 * CONTROLS
 *   SELECT (push joystick in) : next mode  0 -> 1 -> 2 -> 3 -> 4 -> 0
 *   UP / DOWN in modes 1-3    : LED1 (P1.28) and LED2 (P2.2) ON / OFF
 *   UP / DOWN in mode 4       : x + 1 / x - 1 for the barrel shifter demo
 *
 * MODES
 *   0 = idle / instructions
 *   1 = BitBand() function method   (alias address computed at run time)
 *   2 = Direct bit banding          (alias address calculated by hand)
 *   3 = Masking                     (read-modify-write with |= and &=)
 *   4 = Barrel shifter: x*10 = (x<<3) + (x<<1), result shown on LCD + LEDs
 *===========================================================================*/

//#define TARGET_VERSION        // <-- UNCOMMENT for the Target (demo) version

#include "LPC17xx.h"     // LPC_GPIO1, LPC_GPIO2, SystemCoreClock, uint32_t
#include <stdio.h>       // sprintf(), printf()
#include <stdbool.h>     // bool, true, false
#include "KBD.h"         // KBD_Init(), get_button(), KBD_UP/DOWN/SELECT
#include "GLCD.h"        // LCD driver
#include "LED.h"         // LED_Init(), LED_Out()

#define __FI        1    // Font index 1 = 16x24 font -> 20 characters per line
//#define __USE_LCD        // Comment this line to turn off LCD

#define LCD_COLS    20   // 320 px wide / 16 px per character

/*----------------------------------------------------------------------------
 * printf through ITM (Instrumentation Trace Macrocell)
 * printf() calls fputc() for each character; we send it to ITM stimulus
 * port 0. The text shows up in View -> Serial Windows -> Debug (printf) Viewer.
 * Used by the Debug version to print the benchmark results.
 *--------------------------------------------------------------------------*/
#define ITM_Port8(n)    (*((volatile unsigned char *)(0xE0000000+4*n)))
#define ITM_Port32(n)   (*((volatile unsigned long *)(0xE0000000+4*n)))
#define DEMCR           (*((volatile unsigned long *)(0xE000EDFC)))  // Debug Exception & Monitor Control
#define TRCENA          0x01000000                                   // DEMCR bit 24: enables DWT + ITM

struct __FILE { int handle; };
FILE __stdout;
FILE __stdin;

int fputc(int ch, FILE *f) {
    if (DEMCR & TRCENA) {            // only when tracing is enabled
        while (ITM_Port32(0) == 0);  // wait until the port can take a byte
        ITM_Port8(0) = ch;           // send the character
    }
    return ch;
}

/*----------------------------------------------------------------------------
 * BIT BANDING - background
 * Every bit in a bit-band region has its own 32-bit "alias" word.
 * Writing 1/0 to the alias word sets/clears ONLY that bit, in ONE store
 * instruction, atomically (no read-modify-write in software).
 *
 *   alias = alias_base + (byte_offset * 32) + (bit_number * 4)
 *
 *   Region      range                     alias base
 *   SRAM        0x20000000 - 0x200FFFFF   0x22000000   <- GPIO is here
 *   Peripheral  0x40000000 - 0x400FFFFF   0x42000000
 *
 * The BitBand macro does the same formula with bit operations:
 *   (x & 0xF0000000) | 0x02000000  -> picks 0x22000000 or 0x42000000
 *   ((x & 0x000FFFFF) << 5)        -> byte_offset * 32
 *   ((y) << 2)                     -> bit_number * 4
 *--------------------------------------------------------------------------*/
#define ADDRESS(x)    (*((volatile unsigned long *)(x)))
#define BitBand(x, y) ADDRESS(((unsigned long)(x) & 0xF0000000) | 0x02000000 | \
                              (((unsigned long)(x) & 0x000FFFFF) << 5) | ((y) << 2))

/*----------------------------------------------------------------------------
 * EFFECTIVE (ALIAS) ADDRESSES - hand calculation using Table 101
 *
 * LED1 = P1.28
 *   FIO1PIN     = 0x2009C020 (GPIO1 base) + 0x14 (FIOPIN offset) = 0x2009C034
 *   byte_offset = 0x2009C034 - 0x20000000                        = 0x0009C034
 *   alias       = 0x22000000 + (0x0009C034 * 32) + (28 * 4)
 *               = 0x22000000 +  0x01380680       +  0x70         = 0x233806F0
 *
 * LED2 = P2.2
 *   FIO2PIN     = 0x2009C040 (GPIO2 base) + 0x14                 = 0x2009C054
 *   byte_offset                                                  = 0x0009C054
 *   alias       = 0x22000000 + (0x0009C054 * 32) + (2 * 4)
 *               = 0x22000000 +  0x01380A80       +  0x08         = 0x23380A88
 *
 *
 *--------------------------------------------------------------------------*/
#define LED1 (*((volatile unsigned long *)0x233806F0))   // P1.28 alias word
#define LED2 (*((volatile unsigned long *)0x23380A88))   // P2.2  alias word

/*----------------------------------------------------------------------------
 * DELAY (Target version only)
 * DEMO_DELAY() becomes delay() in the Target version and NOTHING in the
 * Debug version, so the benchmark measures only the LED code itself.
 * For the report: put a breakpoint on a delay() call and use the stopwatch
 * (t1 in the status bar) or the Execution Profiler to get one call's time.
 *--------------------------------------------------------------------------*/
#define DELAY_COUNT  3000000UL

__attribute__((noinline)) void delay(void) {
    volatile uint32_t d;                  // volatile: stops -O3 deleting the loop
    for (d = 0; d < DELAY_COUNT; d++);    // busy-wait
}

#ifdef TARGET_VERSION
    #define DEMO_DELAY()  delay()
#else
    #define DEMO_DELAY()                  // expands to nothing in Debug version
#endif

/*----------------------------------------------------------------------------
 * CONDITIONAL EXECUTION METHOD
 *
 * Method used: ITE (If-Then-Else) conditional execution.
 *   The if (led_on) {...} else {...} in the three methods below compiles
 *   (at -O3) to an IT block like:
 *        CMP   r0, #0
 *        ITE   NE          ; If led_on != 0 Then ... Else ...
 *        MOVNE r1, #1      ; runs only if led_on != 0
 *        MOVEQ r1, #0      ; runs only if led_on == 0
 *        STR   r1, [r2]
 *   so both paths run WITHOUT a branch (no pipeline flush on the
 *   3-stage Cortex-M3 pipeline).
 *   At -O0 the compiler usually emits CMP + BEQ/B branches instead.
 *
 *   The 'S' suffix also appears: 16-bit Thumb instructions such as MOVS /
 *   LSLS (see the barrel shifter) update the N,Z,C,V flags, and the CMP/IT
 *   pair then uses those flags.
 *
 *--------------------------------------------------------------------------*/

/* METHOD 1: BitBand() function
 * Alias address is CALCULATED AT RUN TIME with the BitBand macro
 * (AND, shift, OR instructions) every time the function runs.
 * Expected: slowest bit-band method, most extra work at -O0. */
__attribute__((noinline)) void bitbanding(bool led_on) {
    volatile unsigned long *bb_led1 = &BitBand(&LPC_GPIO1->FIOPIN, 28);  // -> 0x233806F0
    volatile unsigned long *bb_led2 = &BitBand(&LPC_GPIO2->FIOPIN, 2);   // -> 0x23380A88

    if (led_on) {                 // ITE conditional execution 
        *bb_led1 = 1;  DEMO_DELAY();   // one STR sets only P1.28
        *bb_led2 = 1;  DEMO_DELAY();   // one STR sets only P2.2
    } else {
        *bb_led1 = 0;  DEMO_DELAY();
        *bb_led2 = 0;  DEMO_DELAY();
    }
}

/* METHOD 2: Direct bit banding
 * Alias address is a CONSTANT we calculated by hand, so each LED is just
 * "load address, STR value" - no address math at run time.
 * Expected: fastest method. */
__attribute__((noinline)) void direct_bitbanding(bool led_on) {
    if (led_on) {                 // ITE conditional execution
        LED1 = 1;  DEMO_DELAY();
        LED2 = 1;  DEMO_DELAY();
    } else {
        LED1 = 0;  DEMO_DELAY();
        LED2 = 0;  DEMO_DELAY();
    }
}

/* METHOD 3: Masking
 * Software read-modify-write: LDR (read port) -> ORR/BIC (change one bit)
 * -> STR (write back). 3 steps per LED, and NOT atomic: an interrupt between
 * the LDR and STR could overwrite other pins on the same port.
 * Expected: slower than direct bit banding. */
__attribute__((noinline)) void masking(bool led_on) {
    if (led_on) {                 // ITE conditional execution
        LPC_GPIO1->FIOPIN |=  (1UL << 28);  DEMO_DELAY();   // set bit 28
        LPC_GPIO2->FIOPIN |=  (1UL << 2);   DEMO_DELAY();   // set bit 2
    } else {
        LPC_GPIO1->FIOPIN &= ~(1UL << 28);  DEMO_DELAY();   // FIX: 1UL everywhere
        LPC_GPIO2->FIOPIN &= ~(1UL << 2);   DEMO_DELAY();   // clear bit 2
    }
}

/*----------------------------------------------------------------------------
 * BARREL SHIFTER APPLICATION: fast multiply by 10
 *
 *   x * 10 = x * (8 + 2) = (x << 3) + (x << 1)
 *
 * Where this is used: converting typed digits into a number (atoi, keypad
 * entry, parsing a serial command): result = result*10 + digit. It is the
 * classic "multiply by a constant with shift-and-add" trick.
 *
 * Why it fits the Cortex-M3: the barrel shifter sits on the second operand
 * of data-processing instructions, so the shift is FREE inside the ADD:
 *        LSLS  r1, r0, #3          ; r1 = x << 3   ('S' suffix: sets flags)
 *        ADD   r2, r1, r0, LSL #1  ; r2 = r1 + (x << 1)  <- shift in same instr.
 * (At -O3 the compiler may instead emit ADD r0,r0,r0,LSL #2 then LSLS #1,
 *  i.e. (x + 4x) * 2 - still the barrel shifter. Print whichever you get.)
 *
 * Output: the equation + result on the LCD, and the result in BINARY on the
 * 8 LEDs (LED_Out treats the number as a bitmask). x is limited to 0..25 so
 * the result (max 250) fits in 8 bits / 8 LEDs.
 *--------------------------------------------------------------------------*/
#define BS_X_MAX  25

unsigned int bs_x = 1;   // input  x
unsigned int bs_y;       // result  (= 10x)

// Barrel shift calculation only (no LED output) so it can be timed on its own
__attribute__((noinline)) void barrel_shift_calc(void) {
    //y = x*8 + 1
    bs_y = (bs_x << 3) + 1;
}

__attribute__((noinline)) void barrel_shift_times10(void) {
    barrel_shift_calc();
    LED_Out(bs_y);           // show result in binary on the 8 LEDs
}

/*----------------------------------------------------------------------------
 * LCD helpers
 *--------------------------------------------------------------------------*/
char lcd_buf[LCD_COLS + 8];

// Write a line padded with spaces to 20 chars, so old text is always erased.
// FIX: this replaces the stale 'text' problem from the old version.
void lcd_line(unsigned int row, const char *s) {
#ifdef __USE_LCD
    sprintf(lcd_buf, "%-20.20s", s);    // left-justify, pad/truncate to 20
    GLCD_DisplayString(row, 0, __FI, (unsigned char *)lcd_buf);
#endif
}

// Turn an 8-bit number into "0b01111000" (for showing what the LEDs mean).
void to_binary8(unsigned int v, char *out) {
    int b;
    out[0] = '0'; out[1] = 'b';
    for (b = 7; b >= 0; b--) {
        out[2 + (7 - b)] = ((v >> b) & 1U) ? '1' : '0';   // shifts again!
    }
    out[10] = '\0';
}

// Redraw rows 5-9 for the current mode. Only called when something changed,
// so the LCD does not flicker and the main loop stays fast.
void update_lcd(int mode, bool led_on) {
    char tmp[32];
    char bin[12];

    switch (mode) {
    case 0:
        lcd_line(5, "Mode 0: Idle");
        lcd_line(6, "SELECT = next mode");
        lcd_line(7, "UP/DOWN = LED on/off");
        lcd_line(8, "");
        lcd_line(9, "");
        break;
    case 1:
    case 2:
    case 3:
        if (mode == 1) lcd_line(5, "1: BitBand Function");
        if (mode == 2) lcd_line(5, "2: Direct Bitband");
        if (mode == 3) lcd_line(5, "3: Masking");
        lcd_line(6, "LED1=P1.28 LED2=P2.2");
        lcd_line(7, led_on ? "LEDs: ON" : "LEDs: OFF");
        lcd_line(8, "");
        lcd_line(9, "");
        break;
    case 4:
        lcd_line(5, "4: Barrel Shift");
        lcd_line(6, "y=(x<<3)+1)");                 // the equation
        sprintf(tmp, "(%u<<3)+1)", bs_x);        // with numbers
        lcd_line(7, tmp);
        sprintf(tmp, "=%u+%u=%u", (bs_x << 3), 1, bs_y);        // the result
        lcd_line(8, tmp);
        to_binary8(bs_y, bin);
        sprintf(tmp, "LEDs=%s", bin);                       // what LEDs show
        lcd_line(9, tmp);
        break;
    }
}

/*----------------------------------------------------------------------------
 * PERFORMANCE MEASUREMENT (Debug version only)
 *
 * Technique: the DWT (Data Watchpoint & Trace) cycle counter, CYCCNT, counts
 * every CPU clock cycle. Read it before and after the code under test:
 *     cycles = end - start - overhead
 * 'overhead' is the cost of just reading CYCCNT twice, measured first and
 * subtracted so only the method itself is counted.
 * Time in ns = cycles * 1000 / (clock in MHz). At 100 MHz, 1 cycle = 10 ns.
 *
 * Each measurement is one ON + one OFF call of the method.
 * Results: printed in the Debug (printf) Viewer AND stored in the globals
 * below so you can also read them in a Watch window.
 * Run once built with -O0 and once with -O3 to fill in the report table.
 *
 * Cross-check: the uVision "States" counter / stopwatch (t1) between two
 * breakpoints gives the same kind of number. If CYCCNT reads 0 in the
 * simulator, use that method instead.
 *--------------------------------------------------------------------------*/
#ifndef TARGET_VERSION

#define DWT_CTRL     (*((volatile unsigned long *)0xE0001000))  // bit 0 = CYCCNTENA
#define DWT_CYCCNT   (*((volatile unsigned long *)0xE0001004))  // the cycle counter

volatile uint32_t cyc_overhead;
volatile uint32_t cyc_masking;
volatile uint32_t cyc_bb_function;
volatile uint32_t cyc_bb_direct;
volatile uint32_t cyc_barrel;

void run_benchmark(void) {
    uint32_t start, end, mhz;

    DEMCR    |= TRCENA;      // 1) enable the DWT unit
    DWT_CYCCNT = 0;          // 2) reset the counter
    DWT_CTRL |= 1UL;         // 3) start counting

    start = DWT_CYCCNT;  end = DWT_CYCCNT;
    cyc_overhead = end - start;                     // cost of the measurement itself

    start = DWT_CYCCNT; //start the count
    masking(true);  masking(false); //turn masking on then off
    end = DWT_CYCCNT; //end the count 
    cyc_masking = end - start - cyc_overhead;

    start = DWT_CYCCNT;
    bitbanding(true);  bitbanding(false); // turn bitband on then off
    end = DWT_CYCCNT;
    cyc_bb_function = end - start - cyc_overhead;

    start = DWT_CYCCNT;
    direct_bitbanding(true);  direct_bitbanding(false); // turn direct bb on then off
    end = DWT_CYCCNT;
    cyc_bb_direct = end - start - cyc_overhead;

    start = DWT_CYCCNT;
    barrel_shift_calc(); // run the barrel shift once: y = (x << 3) + 1
    end = DWT_CYCCNT;
    cyc_barrel = end - start - cyc_overhead;

    mhz = SystemCoreClock / 1000000UL;              // 100 on the MCB1700
    if (mhz == 0) mhz = 1;                          // guard against divide by 0

    printf("Benchmark (1 ON + 1 OFF), %u MHz\n", mhz);
    printf("Masking      : %u cycles = %u ns\n", cyc_masking,     cyc_masking     * 1000 / mhz);
    printf("BitBand func : %u cycles = %u ns\n", cyc_bb_function, cyc_bb_function * 1000 / mhz);
    printf("Direct BB    : %u cycles = %u ns\n", cyc_bb_direct,   cyc_bb_direct   * 1000 / mhz);
    printf("Barrel shift : %u cycles = %u ns\n", cyc_barrel,      cyc_barrel      * 1000 / mhz);
}
#endif

int main(void) {
    uint32_t joy;                  // joystick reading this pass
    uint32_t prev_joy = 0;         // FIX: initialised (was garbage before)
    int  mode = 0;                 // current mode 0..4
    bool led_on = false;           // desired LED state for modes 1-3

    bool mode_changed = true;      // "something happened" flags, so we only
    bool led_changed  = false;     //  touch the LEDs / LCD when needed
    bool lcd_changed  = true;

    LED_Init();                    // LED pins -> outputs
    KBD_Init();                    // joystick pins -> inputs

#ifdef __USE_LCD
    GLCD_Init();
    GLCD_Clear(White);
    GLCD_SetBackColor(Blue);
    GLCD_SetTextColor(Yellow);
    lcd_line(0, "   COE718 Lab 2");
    GLCD_SetTextColor(White);
    lcd_line(1, "Bit Banding");
#ifdef TARGET_VERSION
    lcd_line(2, "Target (Demo) ver.");
#else
    lcd_line(2, "Debug (Perf) ver.");
#endif
    GLCD_SetTextColor(Red);
#endif

#ifndef TARGET_VERSION
    run_benchmark();               // Debug version: time the 3 methods once
    LED_Out(0);                    // leave all LEDs off afterwards
#endif

    while (1) {
        joy = get_button();

        /* EDGE DETECTION: a button counts only on the pass where it goes
         * from "not pressed" to "pressed", so holding it down = one press. */
        #define PRESSED(k)  ((joy == (k)) && (prev_joy != (k)))

        if (PRESSED(KBD_SELECT)) {           // next mode, wrap 4 -> 0
            mode++;
            if (mode > 4) mode = 0;
            mode_changed = true;
        }

        if (PRESSED(KBD_UP)) {
            if (mode == 4) {                 // barrel mode: x + 1 (wraps)
                bs_x = (bs_x >= BS_X_MAX) ? 0 : bs_x + 1;
                barrel_shift_times10();
                lcd_changed = true;
            } else {                         // LED modes: turn ON
                led_on = true;
                led_changed = true;
            }
        }

        if (PRESSED(KBD_DOWN)) {
            if (mode == 4) {                 // barrel mode: x - 1 (wraps)
                bs_x = (bs_x == 0) ? BS_X_MAX : bs_x - 1;
                barrel_shift_times10();
                lcd_changed = true;
            } else {                         // LED modes: turn OFF
                led_on = false;
                led_changed = true;
            }
        }

        prev_joy = joy;                      // remember for next pass

        /* Entering a new mode: start clean. All 8 LEDs off (the barrel mode
         * uses all of them), LED state reset, and the barrel result shown
         * immediately if we just entered mode 4. */
        if (mode_changed) {
            LED_Out(0);
            led_on = false;
            if (mode == 4) {
                barrel_shift_times10();
            }
            mode_changed = false;
            lcd_changed = true;
        }

        /* Run the selected LED method ONLY when UP/DOWN changed the state.
         * (Before, the method was called on every loop pass; with delays in
         * the Target version that would make the joystick very sluggish.) */
        if (led_changed) {
            if      (mode == 1) bitbanding(led_on);
            else if (mode == 2) direct_bitbanding(led_on);
            else if (mode == 3) masking(led_on);
            led_changed = false;
            lcd_changed = true;
        }

        if (lcd_changed) {
            update_lcd(mode, led_on);
            lcd_changed = false;
        }
    }
}