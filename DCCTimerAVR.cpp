/*
 *  © 2021 Mike S
 *  © 2021-2022 Harald Barth
 *  © 2021 Fred Decker
 *  © 2021 Chris Harlow
 *  © 2021 David Cutting
 *  All rights reserved.
 *  
 *  This file is part of Asbelos DCC API
 *
 *  This is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  It is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CommandStation.  If not, see <https://www.gnu.org/licenses/>.
 */

// ATTENTION: this file only compiles on a UNO or MEGA
// Please refer to DCCTimer.h for general comments about how this class works
// This is to avoid repetition and duplication.
#ifdef ARDUINO_ARCH_AVR
#include <avr/boot.h> 
#include <avr/wdt.h>
#include "DCCTimer.h"
INTERRUPT_CALLBACK interruptHandler=0;

  // Arduino nano, uno, mega etc
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    #define TIMER1_A_PIN   11
    #define TIMER1_B_PIN   12
    #define TIMER1_C_PIN   13
#else
   #define TIMER1_A_PIN   9
   #define TIMER1_B_PIN   10
#endif

void DCCTimer::begin(INTERRUPT_CALLBACK callback) {
    interruptHandler=callback;
    noInterrupts();
    TCCR1A = 0;
    ICR1 = CLOCK_CYCLES;
    TCNT1 = 0;   
    TCCR1B = _BV(WGM13) | _BV(CS10);     // Mode 8, clock select 1
    TIMSK1 = _BV(TOIE1); // Enable Software interrupt
    interrupts();
  }

// ISR called by timer interrupt every 58uS
  ISR(TIMER1_OVF_vect){ interruptHandler(); }

// Alternative pin manipulation via PWM control.
  bool DCCTimer::isPWMPin(byte pin) {
       return pin==TIMER1_A_PIN 
           || pin==TIMER1_B_PIN
       #ifdef TIMER1_C_PIN 
           || pin==TIMER1_C_PIN
       #endif       
       ;
  }

 void DCCTimer::setPWM(byte pin, bool high) {
    if (pin==TIMER1_A_PIN) {
      TCCR1A |= _BV(COM1A1);
      OCR1A= high?1024:0;
    }
    else if (pin==TIMER1_B_PIN) { 
      TCCR1A |= _BV(COM1B1);
      OCR1B= high?1024:0;
    }
 #ifdef TIMER1_C_PIN 
    else if (pin==TIMER1_C_PIN) { 
      TCCR1A |= _BV(COM1C1);
      OCR1C= high?1024:0;
    }
 #endif       
 }

void DCCTimer::clearPWM() {
  TCCR1A= 0;
}

  void DCCTimer::getSimulatedMacAddress(byte mac[6]) {
    for (byte i=0; i<6; i++) {
      mac[i]=boot_signature_byte_get(0x0E + i);
    }
    mac[0] &= 0xFE;
    mac[0] |= 0x02;
  }


volatile int DCCTimer::minimum_free_memory=__INT_MAX__;

// Return low memory value... 
int DCCTimer::getMinimumFreeMemory() {
  noInterrupts(); // Disable interrupts to get volatile value 
  int retval = minimum_free_memory;
  interrupts();
  return retval;
}

extern char *__brkval;
extern char *__malloc_heap_start;

int DCCTimer::freeMemory() {
  char top;
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
}

void DCCTimer::reset() {
  wdt_enable( WDTO_15MS); // set Arduino watchdog timer for 15ms 
  delay(50);            // wait for the prescaller time to expire

}

#if defined(ARDUINO_AVR_MEGA) || defined(ARDUINO_AVR_MEGA2560)
#define NUM_ADC_INPUTS 16
#else
#define NUM_ADC_INPUTS 8
#endif
uint16_t ADCee::usedpins = 0;
int * ADCee::analogvals = NULL;
byte *ADCee::idarr = NULL;
static bool ADCusesHighPort = false;

/*
 * Register a new pin to be scanned
 * Returns current reading of pin and
 * stores that as well
 */
int ADCee::init(uint8_t pin) {
  uint8_t id = pin - A0;
  byte n;
  if (id >= NUM_ADC_INPUTS)
    return -1023;
  if (id > 7)
    ADCusesHighPort = true;
  pinMode(pin, INPUT);
  int value = analogRead(pin);
  if (analogvals == NULL) {
    analogvals = (int *)calloc(NUM_ADC_INPUTS, sizeof(int));
    for (n=0 ; n < NUM_ADC_INPUTS; n++) // set unreasonable value at startup as marker
      analogvals[n] = -32768;           // 16 bit int min value
    idarr = (byte *)calloc(NUM_ADC_INPUTS+1, sizeof(byte));  // +1 for terminator value
    for (n=0 ; n <= NUM_ADC_INPUTS; n++)
      idarr[n] = 255;                   // set 255 as end of array marker
  }
  analogvals[id] = value; // store before enable by idarr[n]
  for (n=0 ; n <= NUM_ADC_INPUTS; n++) {
    if (idarr[n] == 255) {
      idarr[n] = id;
      break;
    }
  }
  return value;
}
int16_t ADCee::ADCmax() {
  return 1023;
}
/*
 * Read function ADCee::read(pin) to get value instead of analogRead(pin)
 */
int ADCee::read(uint8_t pin, bool fromISR) {
  (void)fromISR; // AVR does ignore this arg
  uint8_t id = pin - A0;
  // we do not need to check (analogvals == NULL)
  // because usedpins would still be 0 in that case
  return analogvals[id];
}
/*
 * Scan function that is called from interrupt
 */
#pragma GCC push_options
#pragma GCC optimize ("-O3")
void ADCee::scan() {
  static byte num = 0;       // index into id array
  static bool waiting = false;

  if (waiting) {
    // look if we have a result
    byte low, high;
    if (bit_is_set(ADCSRA, ADSC))
      return; // no result, continue to wait
    // found value
    low = ADCL; //must read low before high
    high = ADCH;
    bitSet(ADCSRA, ADIF);
    analogvals[idarr[num]] = (high << 8) | low;
    waiting = false;
  }
  if (!waiting) {
    // cycle around in-use analogue pins
    num++;
    if (idarr[num] == 255)
      num = 0;
    // start new ADC aquire on id
#if defined(ADCSRB) && defined(MUX5)
    if (ADCusesHighPort) {     // if we ever have started to use high pins)
      if (idarr[num] > 7)              // if we use a high ADC pin
	bitSet(ADCSRB, MUX5);  // set MUX5 bit
      else
	bitClear(ADCSRB, MUX5);
    }
#endif
    ADMUX = (1 << REFS0) | (idarr[num] & 0x07); // select AVCC as reference and set MUX
    bitSet(ADCSRA, ADSC);               // start conversion
    waiting = true;
  }
}
#pragma GCC pop_options

void ADCee::begin() {
  noInterrupts();
  // ADCSRA = (ADCSRA & 0b11111000) | 0b00000100;   // speed up analogRead sample time
  // Set up ADC for free running mode
  ADMUX=(1<<REFS0); //select AVCC as reference. We set MUX later
  ADCSRA = (1<<ADEN)|(1 << ADPS2); // ADPS2 means divisor 32 and 16Mhz/32=500kHz.
  //bitSet(ADCSRA, ADSC); //do not start the ADC yet. Done when we have set the MUX
  interrupts();
}
#endif
