/*
 * single button clock
 * (c) 2012 graumannteague
 * 
 * The world's most useless clock, designed for the Olimex AVR-P28
 * dev board, an ATmega328P MCU, and an 8MHz crystal
 *
 * An exercise for myself to learn more about avr-gcc and the AVR
 * chip in general.  Don't mistake this for an actual timepiece!
 *
 * requires avrdude and avr-gcc environment
 * Makefile assumes a USBtinyISP programmer, change to suit
 * 
 * INSTRUCTIONS:
 *
 * build and burn with make / make fuse / make install
 *
 * press button to set hours (12-hour time), wait for LED to blink once
 * press button to set minutes, wait for LED to blink twice
 * clock is now set
 *
 * on the minute, it will blink the hours, pause, then blink the
 * minutes
 *
 * e.g. to set time to 11:07, press the button 11 times, wait for the
 * LED to blink once, then press the button 7 times, then wait for the
 * LED to blink twice
 *
 * TECHNICAL:
 *
 * this program uses the 16-bit Timer/Counter 1 in CTC mode, to call an
 * interrupt routine every 1/50 sec.  The interrupt routine just
 * increments the ticks (1/50 sec units), time-of-day seconds, time-of-day
 * minutes, and time-of-day hours.  It sets a flag (a global variable
 * declared volatile) to indicate to the main loop that the LED needs
 * to be blinked.  The main flag clears this flag after it is done
 * blinking the LED.
 *
 * Timer/Counter 1 is also used earlier on to set the time, with a
 * different prescaler value and with CTC turned off
 *
 * BUGS:
 *
 * no bounds checking when setting the time at the moment - so don't
 * set the hours past 12, or minutes past 59!
 *
 * you can't set the minutes to 0!
 * 
 * you can't set the seconds
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sfr_defs.h>

#define F_CPU	8000000UL

#include <util/delay.h>

#define	LED				PC5
#define LED_PORT		PORTC
#define LED_DDR			DDRC

#define BUTTON			PD2
#define BUTTON_DDR		DDRD
#define BUTTON_PIN		PIND
#define	BUTTON_PORT		PORTD

#define DEBOUNCE_TIME	25

volatile uint8_t display_flag = 0;
volatile uint8_t ticks = 0;		/* incremented every 1/50 sec, reset to 0 every second */
volatile uint8_t tod_secs = 0;
volatile uint8_t tod_mins = 0;
volatile uint8_t tod_hours = 0;

void init_ports(void) {
	LED_DDR |= _BV(LED);		/* set LED pin to OUTPUT */
	LED_PORT |= _BV(LED);		/* turn off LED */
	BUTTON_DDR &= ~(_BV(BUTTON));	/* set button pin to INPUT */
	BUTTON_PORT &= ~(_BV(BUTTON));	/* set button pin to tristate */

	return;
}

void init_timer(void) {
	TCCR1B = 0;					/* remove prescaler settings from set_clock() */
	TCCR1B |= _BV(WGM12);		/* set timer/counter 1 to CTC mode */
	cli();						/* disable global interrupts, only enable once we have the initial time set */
	TIMSK1 |= _BV(OCIE1A);		/* enable Output Compare A interrupt */
	OCR1A = 20000;				/* set CTC compare value to 50Hz at 8MHz AVR clock, with a prescaler of 8 */
	TCCR1B |= _BV(CS11);		/* set prescaler to 8 */
	TCNT1 = 0;
	
	return;
}

void led_short_delay(void) {

	uint8_t i = 0;

	for (i=0; i<10; i++) {
		_delay_ms(20);
	}

	return;
}

void led_long_delay(void) {

	uint8_t i = 0;

	for (i=0; i<3; i++) {
		led_short_delay();
	}

	return;
}

void led_flash(uint8_t count) {
	/* flash LED count times */

	uint8_t i = 0;

	for (i=0; i<count; i++) {
		LED_PORT &= ~(_BV(LED));
		led_short_delay();
		LED_PORT |= _BV(LED);
		led_short_delay();
	}

	return;
}

void set_var(volatile uint8_t *var, uint8_t count) {
	/* set *var to a value using only button presses.
	 * count is the number of times to blink the LED once set
	 */

	loop_until_bit_is_clear(BUTTON_PIN, BUTTON);	/* wait for button press */
	_delay_ms(DEBOUNCE_TIME);
	loop_until_bit_is_set(BUTTON_PIN, BUTTON);
	/* press is complete */
	(*var)++;
	TCCR1B = (_BV(CS12) | _BV(CS10));	/* prescaler on 1024 */
	TCNT1 = 0;
	while (TCNT1 < 23460) {				/* approx 3 secs */
		if bit_is_clear(BUTTON_PIN, BUTTON) {
			_delay_ms(DEBOUNCE_TIME);
			loop_until_bit_is_set(BUTTON_PIN, BUTTON);
			(*var)++;
			TCNT1 = 0;
		}
	}

	led_flash(count);

	return;
}

void set_clock(void) {
	/* to set clock, press button x number of times to set (12-hour) hour
	 * TOD to x.  Each button press must occur within 3 seconds.
	 * LED will flash once to signify that hour is set.  Then, press button
	 * y number of times to set minute TOD to y.  Again, each button press
	 * must occur within 3 seconds.  LED will flash twice to signify that
	 * minute is set. */

	set_var(&tod_hours, 1);
	set_var(&tod_mins, 2);

	return;
}

int main(void) {

	init_ports();
	set_clock();
	init_timer();
	sei();

	for (;;) {
		if (display_flag) {
			led_flash(tod_hours);
			led_long_delay();
			led_flash(tod_mins);
			display_flag = 0;
		}
	}

	return 0;
}

ISR(TIMER1_COMPA_vect) {
	if (++ticks >= 50) {
		ticks = 0;
		if (++tod_secs >= 60) {
			tod_secs = 0;
			display_flag = 1;	/* minute ticked over, need to flash LED */
			if (++tod_mins >= 60) {
				tod_mins = 0;
				if (++tod_hours >= 13) {
					tod_hours = 1;
				}
			}
		}
	}
}
