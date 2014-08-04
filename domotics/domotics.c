#define F_CPU 7372800UL
#define BAUD 9600

#define SETTING_ADDRESS 4

#include <stdio.h>
#include <util/setbaud.h>
#include <avr/io.h>
#include <string.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

#define LED_SER PB0 // 14
#define LED_CLK PB1 // 15
#define LED_LAT PB2 // 16
#define LED_PORT PORTB
#define LED_DDR DDRB

#define OUT_SER PC0 // 23
#define OUT_CLK PC1 // 24
#define OUT_LAT PC2 // 25
#define OUT_PORT PORTC
#define OUT_DDR DDRC

#define LCD_SER PC3 // 26
#define LCD_CLK PC4 // 27
#define LCD_LAT PC5 // 28
#define LCD_PORT PORTC
#define LCD_DDR DDRC

#define IN_SER PIND5 // 11
#define IN_CLK PD6   // 12
#define IN_LOAD PD7  // 13
#define IN_PIN PIND
#define IN_PORT PORTD
#define IN_DDR DDRD

uint32_t EEMEM eepromtable[64];
uint32_t EEMEM eepromoutputs;

char ipaddress[16];

uint8_t alloffmode = 0;
uint8_t allonmode = 0;
uint32_t previousoutputs = 0;
uint32_t currentoutputs = 0;
uint32_t translationtable[64];

void delay_ms(uint16_t millis)
{
	while (millis)
	{
		_delay_ms(1);
		millis--;
	}
}

void uart_init(void)
{
	UBRRH = UBRRH_VALUE;
	UBRRL = UBRRL_VALUE;
	#if USE_2X
		UCSRA |= (1 << U2X);
	#else
		UCSRA &= ~(1 << U2X);
   	#endif
   	UCSRB = (1 << TXEN) | (1 << RXEN);
}
void uart_putchar(char character)
{
	loop_until_bit_is_set(UCSRA, UDRE);
	UDR = character;
}
char uart_getchar(void)
{
	loop_until_bit_is_set(UCSRA, RXC);
	return (char)UDR;
}
char *uart_loadstring(char starttrigger, char endtrigger)
{
	uint8_t pointer = 0;
	char receivedchar = '\0';
	char receivedstring[16] = "                ";

	while (receivedchar != starttrigger)
	{
		receivedchar = uart_getchar();
	}
	receivedchar = uart_getchar(); // load the new character, ignoring the starttrigger
	while (receivedchar != endtrigger && pointer < 16)
	{
		 receivedstring[pointer] = receivedchar;
		 receivedchar = uart_getchar();
		 pointer++;
	}

	return strdup(receivedstring);
}

uint8_t ascii_to_hex(char character)
{
	if (character >= '0' && character <= '9')
	{
		return character - 48;
	}
	if (character >= 'A' && character <= 'F')
	{
		return character - 55;
	}
	if (character >= 'a' && character <= 'f')
	{
		return character - 87;
	}
	return 0;
}
char hex_to_ascii(uint8_t value)
{
	if (value <= 9)
	{
		return value + 48;
	}
	if (value > 9 && value <= 15)
	{
		return value + 55;
	}
	return '0';
}

uint8_t get_int8(void)
{
	uint8_t value = 0;
	value |= (ascii_to_hex(uart_getchar()) << 4);
	value |= (ascii_to_hex(uart_getchar()) << 0);
	return value;
}
uint32_t get_int32(void)
{
	uint32_t value = 0;
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 28);
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 24);
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 20);
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 16);
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 12);
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 8);
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 4);
	value |= ((uint32_t)ascii_to_hex(uart_getchar()) << 0);
	return value;
}


void latch(void)
{
	OUT_PORT &= ~(1 << OUT_LAT);
	OUT_PORT |= (1 << OUT_LAT);
	OUT_PORT &= ~(1 << OUT_LAT);
}
void load(void)
{
	IN_PORT |= (1 << IN_LOAD);
	IN_PORT &= ~(1 << IN_LOAD);
	IN_PORT |= (1 << IN_LOAD);
}

void shift_lcd_byte(uint8_t byte)
{
	for (uint8_t i = 128; i > 0; i >>= 1)
	{
		if (byte & i)
        {
            LCD_PORT |= (1 << LCD_SER);
        }
        else
        {
            LCD_PORT &= ~(1 << LCD_SER);
        }

		LCD_PORT &= ~(1 << LCD_CLK);
		LCD_PORT |= (1 << LCD_CLK);
		LCD_PORT &= ~(1 << LCD_CLK);
    }
	LCD_PORT &= ~(1 << LCD_LAT);
	LCD_PORT |= (1 << LCD_LAT);
	LCD_PORT &= ~(1 << LCD_LAT);
}
void shift_byte_out(uint8_t byte)
{
	for (uint8_t i = 128; i > 0; i >>= 1)
    {
		if (byte & i)
        {
            OUT_PORT |= (1 << OUT_SER);
        }
        else
        {
            OUT_PORT &= ~(1 << OUT_SER);
        }

		OUT_PORT &= ~(1 << OUT_CLK);
		OUT_PORT |= (1 << OUT_CLK);
		OUT_PORT &= ~(1 << OUT_CLK);
    }
}
void shift_int32_out(uint32_t int32)
{
	uint8_t byte;
	uint32_t shifted;
	for (uint8_t i = 0; i < 4; i++)
	{
		shifted = int32 >> ((3 - i) * 8);
		byte = shifted & 255;
		shift_byte_out(byte);
	}
}
uint8_t shift_byte_in(void)
{
	uint8_t loadedbyte = 0;
	for (uint8_t i = 0; i < 8; i++)
	{
		IN_PORT &= ~(1 << IN_CLK);
		IN_PORT |= (1 << IN_CLK);
		if (IN_PIN & (1 << IN_SER))
		{
			loadedbyte |= (1 << i);
		}
		IN_PORT &= ~(1 << IN_CLK);
	}
	return loadedbyte;
}
uint64_t shift_int64_in(void)
{
	uint64_t byte;
	uint64_t int64 = 0;
	for (uint8_t i = 0; i < 8; i++)
	{
		byte = 0 | shift_byte_in();
		int64 |= byte << (i * 8);
	}
	return int64;
}

void shift_led_out(uint64_t int64)
{
	uint8_t byte;
	uint64_t shifted;
	for (uint8_t i = 0; i < 8; i++)
	{
		shifted = int64 >> ((7 - i) * 8);
		byte = shifted & 255;
		for (uint8_t i = 128; i > 0; i >>= 1)
		{
			if (byte & i)
			{
				LED_PORT |= (1 << LED_SER);
			}
			else
			{
				LED_PORT &= ~(1 << LED_SER);
			}

			LED_PORT &= ~(1 << LED_CLK);
			LED_PORT |= (1 << LED_CLK);
			LED_PORT &= ~(1 << LED_CLK);
		}
	}
	LED_PORT &= ~(1 << LED_LAT);
	LED_PORT |= (1 << LED_LAT);
	LED_PORT &= ~(1 << LED_LAT);
}

void send_lcd_data(uint8_t data)
{
	// data comes in the form of:
	// * * RS RW DB7 DB6 DB5 DB4
	// we just need to tweak E onto the databyte

	shift_lcd_byte(data | (1 << 6));
	shift_lcd_byte(data & ~(1 << 6));
	shift_lcd_byte(data | (1 << 6));
}
void init_lcd(void)
{
	// * * RS RW DB7 DB6 DB5 DB4
	// yep, that is just like in the datasheet.

	// start hardware init
	delay_ms(50);
	send_lcd_data(0b00000011);
	delay_ms(5);
	send_lcd_data(0b00000011);
	delay_ms(2);
	send_lcd_data(0b00000011);
	delay_ms(5);
	// hardware init done

	// set to 4-bit operation
	send_lcd_data(0b00000010);
	delay_ms(2);

	// set 2 line display
	send_lcd_data(0b00000010);
	delay_ms(2);
	send_lcd_data(0b00001000);
	delay_ms(2);

	// set cursor settings
	send_lcd_data(0b00000000);
	delay_ms(2);
	send_lcd_data(0b00001100);
	delay_ms(2);
	send_lcd_data(0b00000000);
	delay_ms(2);
	send_lcd_data(0b00000110);
	delay_ms(2);
}
void clear_lcd(void)
{
	send_lcd_data(0b00000000);
	delay_ms(2);
	send_lcd_data(0b00000001);
	delay_ms(2);
}
void cursor_set_line(uint8_t line)
{
	send_lcd_data(0b00001000 + (line << 2));
	send_lcd_data(0b00000000);
}

void lcd_putchar(char character)
{
	send_lcd_data((character >> 4) | 0b00100000);
	send_lcd_data((character & 0b00001111) | 0b00100000);
}

void put_char(uint8_t device, char character)
{
	if (device == 0)
	{
		uart_putchar(character);
	}
	else if (device == 1)
	{
		lcd_putchar(character);
	}
}
void put_string_slow(uint8_t device, char *string)
{
	for (int i = 0; string[i] != '\0'; i++)
	{
		put_char(device, string[i]);
		delay_ms(50);
	}
}
void put_string(uint8_t device, char *string)
{
	for (int i = 0; string[i] != '\0'; i++)
	{
		put_char(device, string[i]);
	}
}
void put_number_as_hex(uint8_t device, uint8_t bits, uint32_t number)
{
	for (uint8_t i = 0; i < (bits / 4); i++)
	{
		uint8_t numberpart = (number >> (4 * ((bits / 4) - i - 1))) & 0x0F;
		put_char(device, hex_to_ascii(numberpart));
	}
}
void put_io(uint8_t device, uint8_t input, uint32_t output)
{
	put_string(device, "i:");
	put_number_as_hex(device, 8, input);
	put_string(device, " o:");
	put_number_as_hex(device, 32, output);
}

char *clean_ip(char *ipstring)
{
	char cleanedip[16] = "                ";
	uint8_t pointer = 0;
	uint8_t numberfound = 0;
	for (uint8_t i = 0; i < 16; i++)
	{
		if (ipstring[i] == '0' && numberfound == 0)
		{
			// ignore
		}
		else
		{
			cleanedip[pointer] = ipstring[i];
			pointer++;
			numberfound = 1;
		}
		if (ipstring[i] == '.')
		{
			numberfound = 0;
		}
	}
	return strdup(cleanedip);
}

void clear_line(uint8_t line)
{
	cursor_set_line(line);
	put_string(1, "                ");
	cursor_set_line(line);
}

uint8_t calculate_button_address(uint64_t button)
{
	uint8_t buttonaddress = 0;
	for (uint8_t i = 0; i < 64; i++)
	{
		if (button & ((uint64_t)((uint64_t)1 << i)))
		{
			break;
		}
		buttonaddress++;
	}
	return buttonaddress;
}
void apply_output_mask(uint32_t outputmask)
{
	if (outputmask != 0 && outputmask != 0xFFFFFFFF)
	{
		//if (allonmode == 1) { apply_output_mask(0xFFFFFFFF); }
		//if (alloffmode == 1) { apply_output_mask(0); }

		previousoutputs = currentoutputs;
		if ((currentoutputs & outputmask) == outputmask)
		{
			currentoutputs ^= outputmask;
		}
		else
		{
			currentoutputs |= outputmask;
		}
		alloffmode = 0;
		allonmode = 0;
	}
	else if (outputmask == 0)
	{
		if (alloffmode == 0) // if alloffmode == 0, we need to put all outputs off
		{
			previousoutputs = currentoutputs;
			currentoutputs = 0;
			alloffmode = 1;
		}
		else // if alloffmode == 1, all lights are off, and we need to restore all settings
		{
			currentoutputs = previousoutputs;
			alloffmode = 0;
		}
	}
	else if (outputmask == 0xFFFFFFFF)
	{
		if (allonmode == 0) // if allonmode == 0, we need to put all outputs on
		{
			previousoutputs = currentoutputs;
			currentoutputs = 0xFFFFFFFF;
			allonmode = 1;
		}
		else // if allonmode == 1, all lights are on, and we need to restore all settings
		{
			currentoutputs = previousoutputs;
			allonmode = 0;
		}
	}
}
void calculate_outputs(uint8_t buttonaddress)
{
	uint32_t outputmask = translationtable[buttonaddress];
	apply_output_mask(outputmask);
}

void save_current(void)
{
	eeprom_write_dword(&eepromoutputs, currentoutputs);
}
void load_current(void)
{
	currentoutputs = eeprom_read_dword(&eepromoutputs);
	previousoutputs = currentoutputs;
}
void clear_settings(void)
{
	for (uint8_t i = 0; i < 64; i++)
	{
		translationtable[i] = 0;
	}
}
void load_settings(void)
{
	clear_line(1);
	put_string(1, "Loading...");
	clear_settings();
	eeprom_read_block((void*)&translationtable[0], (const void*)&eepromtable, 256);
	clear_line(1);
	put_string(1, "Loading OK");
}
void save_settings(void)
{
	clear_line(1);
	put_string(1, "Saving...");
	eeprom_write_block((const void*)&translationtable[0], (void*)&eepromtable, 256);
	clear_line(1);
	put_string(1, "Saving OK");
}

void show_ip(void)
{
	clear_line(0);
	put_string(1, ipaddress);
}

void init_xport(void)
{
	clear_line(0);
	put_string(1, "Loading XPort...");

	// cycle power
	PORTD &= ~(1 << PD3);
	delay_ms(1000);
	PORTD |= (1 << PD3);
	delay_ms(3000);

	// enter setup mode
	put_string_slow(0, "xxx\r");
	delay_ms(3000);

	// restore to defaults
	put_string_slow(0, "7\r");
	delay_ms(1000);

	// enter server settings
	put_string_slow(0, "0\r");
	put_string_slow(0, "\r\r\r\r\r\r\r");
	put_string_slow(0, "y");
	put_string_slow(0, "3Q4N");
	put_string_slow(0, "y");
	put_string_slow(0, "Domotics\r");
	delay_ms(500);

	// enter channel settings
	put_string_slow(0, "1\r");
	put_string_slow(0, "\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r");
	delay_ms(500);

	// enter email settings
	put_string_slow(0, "3\r");
	put_string_slow(0, "\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r");
	delay_ms(500);

	// enter expert settings
	put_string_slow(0, "5\r");
	put_string_slow(0, "10\r");
	put_string_slow(0, "\r\r\r\r\r\r\r\r\r");
	put_string_slow(0, "6\r");
	put_string_slow(0, "yyyyyyy");
	put_string_slow(0, "\r\r\r");
	delay_ms(500);

	// save and exit
	put_string(0, "9\r");
	delay_ms(5000);

	clear_line(0);
	put_string(1, "XPort OK");

	strcpy(ipaddress, "NodeSet         ");
	while (ipaddress[0] == 'N')
	{
		clear_line(0);
		put_string(1, "Enter network...");

		// cycle power
		PORTD &= ~(1 << PD3);
		delay_ms(1000);
		PORTD |= (1 << PD3);
		delay_ms(3000);

		// enter monitor mode
		put_string_slow(0, "zzz\r");
		delay_ms(5000);

		// get network information
		put_string(0, "NC\r");
		strcpy(ipaddress, clean_ip(uart_loadstring(' ', ' ')));
		delay_ms(500);
	}
	show_ip();

	// quit mode
	put_string(0, "QU\r");
	delay_ms(500);
}

void handle_input(uint8_t buttonaddress)
{
	calculate_outputs(buttonaddress);
	shift_int32_out(currentoutputs);
	latch();
}
void show_led_feedback(void)
{
	uint64_t buttons = 0;
	uint32_t setting = 0;
	uint32_t maskedvalue = 0;
	for (uint8_t i = 0; i < 64; i++)
	{
		setting = translationtable[i];
		maskedvalue = currentoutputs & setting;
		if ((setting == maskedvalue && allonmode == 0 && setting > 0)
			|| (allonmode == 1 && setting == 0xFFFFFFFF)
			|| (alloffmode == 1 && setting == 0)
		   )
		{
			buttons |= ((uint64_t)((uint64_t)1 << i));
		}
	}
	shift_led_out(buttons);
}

void handle_command(void)
{
	put_char(0, 'C');
	char command = uart_getchar();
	if (command == '0') // Save settings to EEPROM
	{
		put_char(0, 'W');
		save_settings();
	}
	else if (command == '1') // Configure switch
	{
		put_char(0, 'A');
		uint8_t address = get_int8();

		put_char(0, 'V');
		uint32_t value = get_int32();
		translationtable[address] = value;
	}
	else if (command == '2') // Delete settings in RAM
	{
		put_char(0, 'D');
		clear_settings();
	}
	else if (command == '3') // Push switch
	{
		put_char(0, 'A');
		uint8_t address = get_int8();

		handle_input(address);
		show_led_feedback();
		save_current();
	}
	else if (command == '4') // Show settings in RAM
	{
		for (uint8_t i = 0; i < 64; i++)
		{
			put_io(0, i, translationtable[i]);
			put_string(0, "\r\n");
		}
	}
	else if (command == '5') // Apply outputmask
	{
		put_char(0, 'V');
		uint32_t value = get_int32();
		apply_output_mask(value);
		shift_int32_out(currentoutputs);
		latch();
	}
	else if (command == '6') // Set direct output values
	{
		put_char(0, 'V');
		uint32_t value = get_int32();
		currentoutputs = value;
		shift_int32_out(currentoutputs);
		latch();
	}
	else if (command == '7') // Get current output values
	{
		put_number_as_hex(0, 32, currentoutputs);
	}
	else if (command == '8') // Reads settings from EEPROM
	{
		put_char(0, 'R');
		load_settings();
	}
	else
	{
		put_char(0, '?');
	}
	put_char(0, '.');

	show_ip();
}

ISR(INT0_vect)
{
	delay_ms(5); // Anti-jitter
	load();
	uint64_t inputs = shift_int64_in();
	if (inputs)
	{
		uint8_t buttonaddress = calculate_button_address(inputs);

		handle_input(buttonaddress);
		show_led_feedback();

		clear_line(1);
		put_io(1, buttonaddress, currentoutputs);

		save_current();
	}
} 

ISR(TIMER1_OVF_vect) {
	show_led_feedback();
}

int main (void)
{
	// init UART
	uart_init();

	// define in- and outputports
	OUT_DDR |= (1 << OUT_SER) | (1 << OUT_CLK) | (1 << OUT_LAT);
	LCD_DDR |= (1 << LCD_SER) | (1 << LCD_CLK) | (1 << LCD_LAT);
	LED_DDR |= (1 << LED_SER) | (1 << LED_CLK) | (1 << LED_LAT);
	IN_DDR &= ~(1 << IN_SER);
	IN_DDR |= (1 << IN_CLK) | (1 << IN_LOAD);

	DDRD |= (1 << PD3); // pin for xport power
	DDRD |= (1 << PD4);

	// some other settings
	MCUCR = (1 << ISC01) | (1 << ISC00);

	// init and clear the LCD
	init_lcd();
	clear_lcd();

	// some startup information
	cursor_set_line(0);
	put_string(1, "Domotics v1.45");

	// load current output settings
	load_current();

	// shift restored outputs to outputports
	shift_int32_out(currentoutputs);
	latch();
	show_led_feedback();

	// load translation table & fetch IP
	load_settings();

	// define interrupts and start them
	GIMSK |= (1 << INT0);
	TIMSK |= (1 << TOIE1);
	TCCR1B |= (1 << CS12); // About every 2.27s
	sei();

	// re-initialize xport
	init_xport();

	while(1)
	{
		char character = uart_getchar();
		if (character == '?')
		{
			handle_command();
		}
	}

	return 0;
}
