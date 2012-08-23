// Library for Adafruit 8x8 LED matrix watch.  This version displays 8-bit
// monochrome graphics with one row enabled at any given time, using a fast
// timer interrupt and bit angle modulation (vs PWM).
// (C) Adafruit Industries.

#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
 #include "pins_arduino.h"
#endif
#include <avr/pgmspace.h>
#include "Watch.h"

// Some of this code might be painful to read in that 'row' and 'column'
// here refer to the hardware pin functions as described in the LED matrix
// datasheet...but with the matrix installed sideways in the watch for
// better component placement, these aren't the same as 'row' and 'column'
// as we usually consider them in computer graphics.  So...the graphics
// drawing functions use conventional intuitive X/Y coordinates from the
// top left of the watch display, and the lower-level code takes care of
// mapping this to the oddly rotated layout of the LED matrix.

// These tables help facilitate pixel drawing.  They are intentionally
// NOT placed in PROGMEM in order to save a few instruction cycles.
// There's ample RAM for this as the screen buffer isn't very large
// (192 bytes single-buffered, 384 bytes double-buffered).  Oink oink!
static const uint8_t
  rowBitPortB[] = {    0, 0x20,    0, 0x10, 0x04,    0, 0x01,    0},
  rowBitPortC[] = {    0,    0, 0x08,    0,    0, 0x04,    0,    0},
  rowBitPortD[] = { 0x10,    0,    0,    0,    0,    0,    0, 0x20};

// The 'off' state for rows and columns is different because one represents
// anodes and the other cathodes.  So this weird combination of bits sets
// all rows and columns to their respective 'off' states:
#define PORTB_OFF B11001010
#define PORTC_OFF B00110011 // PC4, PC5 are set to enable I2C pinups
#define PORTD_OFF B11001100 // PD2, PD3 are set to enable button pullups

// Because the LED matrix ties up so many of the microcontroller's pins,
// the idea of object-orientating the code for multiple instances is a bit
// nonsensical.  It's done insofar as Adafruit_GFX can be used, but all
// the matrix-specific variables and such are simply declared in the code
// here rather than in private vars.  Keeps the interrupt code simple.
static uint8_t
  *img[2];                // Display data
static volatile uint8_t
  plane    = 7,
  col      = 7,
  *ptr,                   // Current pointer into front buffer
  frontIdx = 0,           // Buffer # being displayed (vs modified)
  bSave,                  // Last button state
  bCount   = 0,           // Timer2 overflow counter
  bAction  = ACTION_NONE; // Last button action
static volatile boolean
  swapFlag = false;
static volatile unsigned int 
  frames   = 0; // For delay() counter

// Constructor: pass 'true' to enable double-buffering (default = false).
Watch::Watch(boolean dbuf) {
  int bufSize   = 3 * 8 * 8, // 3 bytes/row * 8 rows * 8 planes
      allocSize = (dbuf == true) ? (bufSize * 2) : bufSize;

  // Allocate and initialize front image buffer:
  if(NULL == (img[0] = (uint8_t *)malloc(allocSize))) return;
  ptr = img[0];
  for(uint8_t i=0; i<bufSize;) {
    ptr[i++] = PORTB_OFF;
    ptr[i++] = PORTC_OFF;
    ptr[i++] = PORTD_OFF;
  }
  // If double-buffered, copy front image buffer to back
  if(dbuf) {
    img[1] = &img[0][bufSize];
    memcpy(img[1], img[0], bufSize);
  } else {
    img[1] = img[0]; // Else both point to the same address
  }
  constructor(8, 8); // Init Adafruit_GFX
}

// Initialize PORT registers and enable timer and button interrupts.
void Watch::begin() {
  PORTB = PORTB_OFF; // Turn all rows/columns off
  PORTC = PORTC_OFF;
  PORTD = PORTD_OFF;
  DDRB  = B11111111; // And enable outputs
  DDRC  = B00001111;
  DDRD  = B11110000;

  // Set up Timer1 for matrix interrupt.  Mode 4 (CTC), OC1A off, no prescale.
  TCCR1A  = 0;
  TCCR1B  = _BV(WGM12) | _BV(CS10);
  OCR1A   = 100;
  TIMSK1 |= _BV(OCIE1A);

  // Timer0 interrupt is disabled as this throws off the delicate PWM
  // timing.  Unfortunately this means delay(), millis() won't work,
  // so we have our own function later for passing time.
  TIMSK0 = 0;

  // Set up Timer2 for button-hold counter.  Mode 0, OC2A off, 1024x prescale.
  TCCR2A = 0;
  TCCR2B = _BV(CS22) | _BV(CS21) | _BV(CS20);
  // Timer2 interrupt is not enabled until a button is pressed.

  // Set up interrupt-on-change for buttons.
  EICRA = _BV(ISC10)  | _BV(ISC00);  // Trigger on any logic change
  EIMSK = _BV(INT1)   | _BV(INT0);   // Enable interrupts on pins
  bSave = _BV(PORTD3) | _BV(PORTD2); // Get initial button state

  sei(); // Enable global interrupts
}

// For double-buffered animation, call this function to display new data.
void Watch::swapBuffers(boolean copy) {
  // Swap actually takes place at specific point in interrupt.
  // Set flag to request swap, then wait for change to complete:
  for(swapFlag = true; swapFlag; );
  if(copy) memcpy(img[1 - frontIdx], img[frontIdx], 3 * 8 * 8);
}

// Basic pixel-drawing function for Adafruit_GFX.
void Watch::drawPixel(int16_t x, int16_t y, uint16_t c) {
  if((x >= 0) && (y >= 0) && (x < 8) && (y < 8)) {
    uint8_t bmask = rowBitPortB[x],
            cmask = rowBitPortC[x],
            dmask = rowBitPortD[x],
            c8    = (uint8_t)c,
            *p    = (uint8_t *)&img[1 - frontIdx][y * 3];
    for(uint8_t bit = 1; bit; bit <<= 1) {
      if(c8 & bit) {
        p[0] |=  bmask;
        p[1] |=  cmask;
        p[2] |=  dmask;
      } else {
        p[0] &= ~bmask;
        p[1] &= ~cmask;
        p[2] &= ~dmask;
      }
      p += 24;
    }
  }
}

// Because Timer0 is disabled (throws off LED brightness), our own delay
// function is provided.  Unlike normal Arduino delay(), the units here
// are NOT milliseconds, but frames.  As noted below, one frame is about
// 1/65 second(ish).
void Watch::delay(unsigned int f) {
  for(frames = 0; frames < f;);
}

// OVERHEAD is the estimated instruction cycle count for the stack work
// done entering and exiting the timer interrupt.  LEDMINTIME is the
// shortest LED 'on' time and must be more than OVERHEAD.  Total PWM cycle
// time will be LEDMINTIME * 255, total refresh time will be 8X cycle time.
#define OVERHEAD   53
#define LEDMINTIME 60
// 60 * 255 = 15300, * 8 = 122400, 8M / 122400 = ~65 Hz
// Because columns are cycled within PWM intervals, the appearance is more
// like 2X this (~130 Hz), though the actual full frame rate is still ~65.

// Turn prior column off, then load new row bits on all 3 PORTs
#define COLSTART(n, port, bit, idx) \
   case n:                     \
    asm volatile(              \
     "sbi %0,%1\n\t"           \
     "ld  __tmp_reg__,%a2\n\t" \
     "out %3,__tmp_reg__\n\t"  \
     "ld  __tmp_reg__,%a4\n\t" \
     "out %5,__tmp_reg__\n\t"  \
     "ld  __tmp_reg__,%a6\n\t" \
     "out %7,__tmp_reg__\n\t"  \
     ::                        \
     "I"(_SFR_IO_ADDR(port)),  \
     "I"(bit),                 \
     "e"(&p[idx]),             \
     "I"(_SFR_IO_ADDR(PORTB)), \
     "e"(&p[idx+1]),           \
     "I"(_SFR_IO_ADDR(PORTC)), \
     "e"(&p[idx+2]),           \
     "I"(_SFR_IO_ADDR(PORTD)));

// Advance 'col' to next column, then enable current column loaded above.
// Columns advance in a bizarre interleaved order of horizontal lines in
// order to reduce apparent flicker and to make multiplexing artifacts
// less objectionable, esp. when scrolling text horizontally.
#define COLEND(port, bit, nxt) \
    col = nxt; \
    asm volatile("cbi %0,%1" :: "I"(_SFR_IO_ADDR(port)), "I"(bit)); \
    break;

// Plan is to eventually make this a 'naked' interrupt w/100% assembly,
// avr-gcc output looks a little bloaty esp. in the stack work...if this
// can be tightened up, OVERHEAD and LEDMINTIME constants above can be
// reduced and a a better refresh rate should be possible.  Already
// unrolled this (e.g. no array lookups), just needs a bit more TLC.
ISR(TIMER1_COMPA_vect, ISR_BLOCK) {

  uint8_t *p = (uint8_t *)ptr;

  switch(col) {
    COLSTART(0, PORTD, 7,  0)
      OCR1A = (LEDMINTIME << plane) - OVERHEAD; // Interrupt time for plane
    COLEND(PORTD, 6, 4)
    COLSTART(1, PORTB, 3,  3) COLEND(PORTB, 6, 5)
    COLSTART(2, PORTC, 0,  6) COLEND(PORTC, 1, 6)
    COLSTART(3, PORTB, 7,  9) COLEND(PORTB, 1, 7)
    COLSTART(4, PORTD, 6, 12) COLEND(PORTC, 0, 2)
    COLSTART(5, PORTB, 6, 15) COLEND(PORTB, 7, 3)
    COLSTART(6, PORTC, 1, 18) COLEND(PORTB, 3, 1)
    COLSTART(7, PORTB, 1, 21)
      if(++plane >= 8) { // Advance plane counter
        plane = 0;       // Back to plane 0
        if(swapFlag) {       // If requested, swap
          frontIdx ^= 1;     // buffers on return
          swapFlag  = false; // to first column
        }
        ptr = img[frontIdx];
        frames++; // For delay() function
      } else ptr += 24;
    COLEND(PORTD, 7, 0)
  }
  // Reset Timer0 counter.  This is done so that the LED 'on' time is
  // more precise -- the above plane-advancing conditional logic won't
  // throw the brightness off.
  TCNT1 = 0;
}

uint8_t *Watch::backBuffer() {
  return img[1 - frontIdx];
}

uint8_t Watch::action(void) {
  uint8_t a = bAction;
  bAction   = ACTION_NONE;
  return  a;
}

ISR(INT0_vect) {

  uint8_t b = PIND & (_BV(PORTD3) | _BV(PORTD2));

  if(b == (_BV(PORTD3) | _BV(PORTD2))) { // Buttons released
    TIMSK2 &= ~_BV(TOIE2);               // Disable Timer2 interrupt
    if((bCount > 2)) {                   // Past debounce threshold?
      if     (bSave == _BV(PORTD3)) bAction = ACTION_TAP_LEFT;
      else if(bSave == _BV(PORTD2)) bAction = ACTION_TAP_RIGHT;
    }
  } else {                 // Button pressed
    if(b == bSave) return; // Debounce
    bCount  = TCNT2 = 0;   // Clear counter + timer
    TIMSK2 |= _BV(TOIE2);  // Enable Timer2 interrupt
  }
  bSave = b; // Note last button change
}

ISR(INT1_vect, ISR_ALIASOF(INT0_vect));

// Overflow = 256 * 1024 inst.  8MHz / (256 * 1024) = ~30.5 Hz, ~33 msec.
ISR(TIMER2_OVF_vect) {
  if(bCount >= 76) {              // ~2.5 second hold
    TIMSK2 &= ~_BV(TOIE2);        // Stop interrupt
    if     (bSave == _BV(PORTD3)) bAction = ACTION_HOLD_LEFT;
    else if(bSave == _BV(PORTD2)) bAction = ACTION_HOLD_RIGHT;
    else if(bSave == 0          ) bAction = ACTION_HOLD_BOTH;
    bSave = bCount = 0;           // So button release code isn't confused
  } else bCount++;                // else keep counting...
}
