#include <RTClib.h>
#include <Watch.h>

#ifndef EXTERN
#define EXTERN(a, b) extern a
extern PROGMEM uint8_t fade[30] ;	// Sigh. Size must match count below.
#else
// Used by various display modes for smooth fade-out before sleep
PROGMEM uint8_t fade[] =
  {  0,  1,  1,  2,  4,  5,  8, 10, 13, 17, 22, 27, 32, 39, 46,
      53, 62, 71, 82, 93,105,117,131,146,161,178,196,214,234,255 } ;
#endif

EXTERN(Watch watch,(2, LED_PLEX_1, true)) ;
EXTERN(uint16_t fps, = 100) ;
EXTERN(boolean h24, = false) ;  // 24-hour display mode
EXTERN(RTC_DS1307 RTC,) ;
EXTERN(uint8_t digit[13],);
EXTERN(int     curX,);


#define DIGIT_YEAR0  0
#define DIGIT_YEAR1  1
#define DIGIT_MON0   2
#define DIGIT_MON1   3
#define DIGIT_DAY0   4
#define DIGIT_DAY1   5
#define DIGIT_HR0    6
#define DIGIT_HR1    7
#define DIGIT_MIN0   8
#define DIGIT_MIN1   9
#define DIGIT_24    10
#define DIGIT_SEC0  11
#define DIGIT_SEC1  12


void blit(uint8_t *, int, int, int, int, int, int, int, int, uint8_t) ;
void loadDigits(int, uint8_t) ;
void drawTime() ;
void set() ;

#define BATT_LOW_MV  2881 // Use reduced display below this voltage
