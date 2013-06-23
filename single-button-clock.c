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
 * if you set hours past 12, or mins past 59, the LED will blink three
 * times to indicate an invalid value, and you will have to try entering
 * the invalid value again
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
 * MIDI chimes:
 *
 * you can connect a MIDI device e.g. synth module, and have it play
 * chimes (currently an arpeggio) every second.  Just connect up a 5-pin
 * male DIN plug as follows:
 *
 *		pin 5:	AVR TXD (pin 3 on a 328P)
 *		pin 2:	GND
 *		pin 4:	+5V via 220ohm resistor
 *
 * and connect the plug to the MIDI IN of a synth module.  Set it to listen
 * on MIDI Channel 1.
 *
 * BUGS:
 *
 * you can't set the minutes to 0
 * 
 * you can't set the seconds
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sfr_defs.h>

#define F_CPU 8000000UL
#define BAUD 31250
#define BAUD_PRESCALE (((F_CPU / (BAUD * 16UL))) - 1)

#include <util/delay.h>

#define	LED				PC5
#define LED_PORT		PORTC
#define LED_DDR			DDRC

#define BUTTON			PD2
#define BUTTON_DDR		DDRD
#define BUTTON_PIN		PIND
#define	BUTTON_PORT		PORTD

#define DEBOUNCE_TIME	25
#define TIME_STR_LEN	7

#define	MIDI_NOTE_ON	0x90
#define MIDI_NOTE_OFF	0x80
#define MIDI_VELOCITY_MAX	127

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

void init_uart(void) {
    UBRR0H = (BAUD_PRESCALE) >> 8;
    UBRR0L = BAUD_PRESCALE;

    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);     /* 8-bit data */
    UCSR0B = _BV(TXEN0);       /* Only enable TX */

	return;
}

void uart_send_char(char d) {
    loop_until_bit_is_set(UCSR0A, UDRE0);
    UDR0 = d;

	return;
}

void uart_send_string(char *s) {
	while (*s) {
		uart_send_char(*s);
		s++;
	}

	return;
}

void uart_send_midi_note_on(uint8_t channel, uint8_t keynum, uint8_t velocity) {
	uint8_t b0, b1, b2;

	b0 = MIDI_NOTE_ON | (channel-1);
	b1 = keynum;
	b2 = velocity;
	uart_send_char(b0);
	uart_send_char(b1);
	uart_send_char(b2);

	return;
}

void uart_send_midi_note_off(uint8_t channel, uint8_t keynum, uint8_t velocity) {
	uint8_t b0, b1, b2;

	b0 = MIDI_NOTE_OFF | (channel-1);
	b1 = keynum;
	b2 = velocity;
	uart_send_char(b0);
	uart_send_char(b1);
	uart_send_char(b2);

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

void set_var(volatile uint8_t *var) {
	/* set *var to a value using only button presses. */

	loop_until_bit_is_clear(BUTTON_PIN, BUTTON);	/* wait for button press */
	_delay_ms(DEBOUNCE_TIME);
	loop_until_bit_is_set(BUTTON_PIN, BUTTON);
	_delay_ms(DEBOUNCE_TIME);
	/* press is complete */
	(*var)++;
	TCCR1B = (_BV(CS12) | _BV(CS10));	/* prescaler on 1024 */
	TCNT1 = 0;
	while (TCNT1 < 23460) {				/* approx 3 secs */
		if bit_is_clear(BUTTON_PIN, BUTTON) {
			_delay_ms(DEBOUNCE_TIME);
			loop_until_bit_is_set(BUTTON_PIN, BUTTON);
			_delay_ms(DEBOUNCE_TIME);
			(*var)++;
			TCNT1 = 0;
		}
	}

	return;
}

void set_clock(void) {
	/* to set clock, press button x number of times to set (12-hour) hour
	 * TOD to x.  Each button press must occur within 3 seconds.
	 * LED will flash once to signify that hour is set.  Then, press button
	 * y number of times to set minute TOD to y.  Again, each button press
	 * must occur within 3 seconds.  LED will flash twice to signify that
	 * minute is set. */

	set_var(&tod_hours);
	while (tod_hours > 12) {
		led_flash(3);			/* invalid hours */
		tod_hours = 0;
		set_var(&tod_hours);	/* repeat until correct */
	}
	led_flash(1);				/* indicate we have a valid hours val */
	set_var(&tod_mins);
	while (tod_mins > 59) {
		led_flash(3);			/* invalid mins */
		tod_mins = 0;
		set_var(&tod_mins);
	}
	led_flash(2);				/* indicate we have a valid mins val */

	return;
}

void uart_play_note(uint8_t channel, uint8_t keynum, uint8_t velocity) {
	uart_send_midi_note_on(channel, keynum, velocity);
	led_short_delay();
	uart_send_midi_note_off(channel, keynum, velocity);
	return;
}

void uart_play_arpeggio(uint8_t basenote) {
	uart_play_note(1, basenote, MIDI_VELOCITY_MAX);
	led_long_delay();
	uart_play_note(1, basenote+4, MIDI_VELOCITY_MAX);
	led_long_delay();
	uart_play_note(1, basenote+7, MIDI_VELOCITY_MAX);
	led_long_delay();
	uart_play_note(1, basenote+12, MIDI_VELOCITY_MAX);
	led_long_delay();

	return;
}

int main(void) {

	/* char time_str[TIME_STR_LEN]; */

	init_ports();
	set_clock();
	init_timer();
	init_uart();
	sei();

	for (;;) {
		if (display_flag) {
			/* snprintf(time_str, TIME_STR_LEN, "%2d:%2d\n", tod_hours, tod_mins); */
			/* uart_send_string(time_str); */
			uart_play_arpeggio(60);
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
