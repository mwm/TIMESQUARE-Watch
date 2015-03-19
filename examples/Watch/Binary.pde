// Binary watch display.

#define BACKGROUND 0
#define BIT_CLEAR 32
#define BIT_SET  255

#include <avr/power.h>
#include <RTClib.h>
#include <Watch.h>
#include "Watch_common.h"

// Value fetchers  library.
static uint8_t fetch_century(DateTime *) ;
static uint8_t fetch_year(DateTime *) ;
static uint8_t fetch_weekday(DateTime *) ;
static uint8_t fetch_month(DateTime *) ;
static uint8_t fetch_day(DateTime *) ;
static uint8_t fetch_ampm(DateTime *) ;
static uint8_t fetch_hour(DateTime *) ;
static uint8_t fetch_hour12(DateTime *) ;
static uint8_t fetch_minute(DateTime *) ;
static uint8_t fetch_second(DateTime *) ;
static uint8_t fetch_now(DateTime *) ;

// Value displayers
static void display_time(struct Display *, DateTime *, uint8_t, uint8_t) ;
static void display_value(struct Display *, uint8_t, uint8_t, uint8_t) ;
static void display_nobcd(struct Display *, uint8_t, uint8_t, uint8_t) ;
static void display_ampm(struct Display *, uint8_t, uint8_t, uint8_t) ;
static void display_hour(struct Display *, uint8_t, uint8_t, uint8_t) ;
static void display_big(struct Display *, uint8_t, uint8_t, uint8_t) ;
static void display_blinker(struct Display *, uint8_t, uint8_t, uint8_t) ;

// A value to display is represented by:
typedef struct {
  unsigned col ;		// The column we draw the MSB in.
  unsigned mask ;		// Mask for the msb.
  void (*displayer)(Display *, uint8_t, uint8_t, uint8_t) ;	// Routine to display this value.
  uint8_t (*fetch)(DateTime *) ;	// Function to fetch value from time_t.
} Value ;
  
// And the values we can display
static Value century_value  = {1, 0x40, display_value, fetch_century} ;
static Value year_value     = {1, 0x40, display_value, fetch_year} ;
static Value dow_value      = {0, 0x04, display_nobcd, fetch_weekday} ;
static Value month_value    = {4, 0x08, display_nobcd, fetch_month} ;
static Value day_value      = {3, 0x10, display_value, fetch_day} ;
static Value ampm_value     = {0, 0x02, display_ampm,  fetch_ampm} ;
static Value hour_value     = {3, 0x10, display_hour,  fetch_hour} ;
static Value big_hour_value = {0, 0x08, display_big,   fetch_hour12} ;
static Value minute_value   = {2, 0x20, display_value, fetch_minute} ;
static Value second_value   = {2, 0x20, display_value, fetch_second} ;
static Value second_blinker = {0, 0x80, display_blinker, fetch_now} ;

// A display is a:
typedef struct Display {
  unsigned row ;	// Row on which to display.
  Value *value ;	// Value to display.
} Display ;

// We store these in arrays of pointers, terminated by a null pointer.
static Display year_display[] = {{0, &century_value} ,
				 {1, &year_value} ,
				 {2, &dow_value} ,
				 {2, &month_value} ,
				 {3, &day_value} ,
				 {5, &ampm_value} ,
				 {5, &hour_value} ,
				 {6, &minute_value} ,
				 {7, &second_value} ,
				 {0, NULL} ,
} ;

static Display month_display[] = {{0, &dow_value} ,
				  {0, &month_value} ,
				  {2, &day_value} ,
				  {4, &ampm_value} ,
				  {4, &hour_value} ,
				  {6, &minute_value} ,
				  {7, &second_blinker} ,
				  {0, NULL} ,
} ;


static Display day_display[] = {{1, &day_value} ,
				{3, &ampm_value} ,
				{3, &hour_value} ,
				{5, &minute_value} ,
				{7, &second_value} ,
				{0, NULL} ,
} ;

static Display hour_display[] = {{1, &big_hour_value} ,
				 {4, &minute_value} ,
				 {6, &second_value} ,
				 {0, NULL} ,
} ;

// An array of displays that we walk througho on the set button.
static Display *displays[] = {year_display ,
			      month_display ,
			      day_display ,
			      hour_display ,
} ;

// Whether we display values > 12 as bcd or binary
static bool bcd = false ;

void mode_binary(uint8_t action) {
  static uint8_t mode = 0 ;	
  DateTime now;
  uint8_t  h, x, bit, b_set, b_clear, depth;
  uint16_t t;

  if(action != ACTION_NONE) {
    // If we just arrived here (whether through mode change
    // or wake from sleep), initialize the matrix driver:
    if(action >= ACTION_HOLD_LEFT) {
      uint8_t plex = LED_PLEX_2;
      depth = 4;
      // Reduce depth/plex if battery voltage is low
      if(watch.getmV() < BATT_LOW_MV) {
        depth = 2;
        plex  = LED_PLEX_1;
      }
      // Reconfigure display if needed
      if((watch.getDepth() != depth) || (watch.getPlex() != plex))
        fps = watch.setDisplayMode(depth, plex, true);
    } else if (action == ACTION_TAP_LEFT) {
      mode = (mode + 1) % 4 ;
    } else if (action == ACTION_TAP_RIGHT) {
      bcd = !bcd ;
    }
    // Reset sleep timeout on ANY button action
    watch.setTimeout(fps * 8);
  }

  // Calc set/clear colors based on current fadeout value
  depth = watch.getDepth();
  if((t = watch.getTimeout()) < sizeof(fade)) {
    uint16_t b1 = (uint8_t)pgm_read_byte(&fade[t]) + 1;
    b_set       = (BIT_SET   * b1) >> (16 - depth);
    b_clear     = (BIT_CLEAR * b1) >> (16 - depth);
  } else {
    b_set       =  BIT_SET   >> (8 - depth);
    b_clear     =  BIT_CLEAR >> (8 - depth);
  }

  now = RTC.now();
  display_time(displays[mode], &now, b_set, b_clear) ;
}

// Fetchers for values to display (OO ugliness. Don't ask.)
static uint8_t
fetch_century(DateTime *t) {
  return t->year() / 100 ;
}

static uint8_t
fetch_year(DateTime *t) {
  return t->year() % 100 ;
}

static uint8_t
fetch_weekday(DateTime *t) {
  return t->dayOfWeek() + 1 ;
}

static uint8_t
fetch_month(DateTime *t) {
  return t->month() ;
}

static uint8_t
fetch_day(DateTime *t) {
  return t->day() ;
}

// am/pm is two bits, AM and PM, in that order.
static uint8_t
fetch_ampm(DateTime *t) {
  return (t->hour() < 12) + 1 ;
}  

// Hour value depends on whether or not we have a 12 or 24 hour clock.
static uint8_t
fetch_hour(DateTime *t) {
  return h24 ? t->hour() : fetch_hour12(t) ;
}

static uint8_t
fetch_hour12(DateTime *t) {
  uint8_t h ;

  h = t->hour() ;
  if (h > 12) {
    h -= 12 ;
  } else if (h == 0) {
    h = 12 ;
  }
  return h ;
}

static uint8_t
fetch_minute(DateTime *t) {
  return t->minute() ;
}

static uint8_t
fetch_second(DateTime *t) {
  return t->second() ;
}

// A faster timer is nice, but we have no millisecond clock!
static uint8_t
fetch_now(DateTime *t) {
  static int i = 0 ;

  return abs(i++ / 10 % 14 - 7) ;
}


// Send the current time to the available display.
static void
display_time(Display *dsp, DateTime *t, uint8_t on, uint8_t off) {
  watch.fillScreen(BACKGROUND) ;
  for (; dsp->value; dsp++) {
    dsp->value->displayer(dsp, dsp->value->fetch(t), on, off) ;
  }
}

// Display the bits in a value, one bit per led
static void
display_value(Display *dsp, uint8_t value, uint8_t on, uint8_t off) {
  if (!bcd) {
    return display_nobcd(dsp, value, on, off) ;
  }

  Value *val = dsp->value ;
  int digit = value / 10 ;

  value -= digit * 10 ;

  watch.drawPixel(4, dsp->row, value & 0x08 ? on : off) ;
  for (uint8_t mask = 0x04, col = 5; mask; mask >>= 1, col += 1) {
    watch.drawPixel(col, dsp->row, value & mask ? on : off) ;
    watch.drawPixel(col - 5, dsp->row, digit & mask ? on : off) ;
  }
}

static void
display_nobcd(Display *dsp, uint8_t value, uint8_t on, uint8_t off) {
  Value *val = dsp->value ;

  for (uint8_t mask = val->mask, col = val->col; mask; mask >>= 1, col += 1) {
    watch.drawPixel(col, dsp->row, value & mask ? on : off) ;
  }
}

static void
display_hour(Display *dsp, uint8_t value, uint8_t on, uint8_t off) {
  static Value hour_12 ;
  static Display new_dsp ;

  if (h24) {
    return display_nobcd(dsp, value, on, off) ;
  }
  
  // If we haven't done it yet, set up a Value for the 12 hour clock.
  // It's a standard hour Value sans the high order bit. Point our
  // Display at it so we only have to do that once.
  if (!hour_12.displayer) {
    hour_12 = *(dsp->value) ;
    hour_12.mask >>= 1 ;
    hour_12.col += 1 ;
    new_dsp.value = &hour_12 ;
  }

  new_dsp.row = dsp->row ;
  return display_nobcd(&new_dsp, value, on, off) ;
}  

static void
display_big(Display *dsp, uint8_t value, uint8_t on, uint8_t off) {
  Value *val = dsp->value ;

  for (uint8_t mask = val->mask, col = val->col; mask; mask >>= 1, col += 2) {
    watch.fillRect(col, dsp->row, 2, 2, value & mask ? on : off) ;
  }
}

static void
display_ampm(Display *dsp, uint8_t value, uint8_t on, uint8_t off) {

  if (!h24) {
    display_nobcd(dsp, value, on, off) ;
  }
}

static void
display_blinker(Display *dsp, uint8_t value, uint8_t on, uint8_t off) {
  display_nobcd(dsp, 1 << value, on, off) ;
}
