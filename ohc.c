#include <avr/io.h>        // for port and register definitions
#include <avr/interrupt.h> // for ISR
#include <util/delay.h>    // for _delay_ms
#include <string.h>        // for memcpy
#include "message.h"

// 01010101
#define PACKET_HEADER 0x55
#define PACKET_SIZE   128+4
enum {
    PACKET_STOP,
    PACKET_LEDTOGGLE,
    PACKET_FORWARDMSG,
    PACKET_FORWARDRAWMSG,
    PACKET_BOOTPAGE,
    PACKET_GPSFRAME
};

uint8_t packet_buffer[PACKET_SIZE];
uint8_t packet_head = 0;
uint8_t packet_checksum = 0;
uint8_t new_packet[PACKET_SIZE];
volatile uint8_t packet_type;
volatile uint8_t has_new_packet = 0;
uint8_t leds_toggle = 0;
message_t msg;

#define ir_port PORTD
#define ir_mask (1<<3)
#define blue_port PORTD
#define blue_mask (1<<2)
#define green_port PORTB
#define green_mask (1<<1)

int main() {
    cli();
    // Set port outputs
    DDRB = (1<<1)|(1<<2);         // enable green led & blue led
    DDRD = (1<<2)|(1<<3);         // enable ir led & blue led
    // Turn IR led off
    ir_port &= ~ir_mask;
    // turn off analog comparator (to avoid detecting collisions)
    ACSR |= (1<<ACD);

    //move interrupt vectors to bootloader interupts
    MCUCR = (1<<IVCE);
    MCUCR = (1<<IVSEL);

#define BAUD 76800
#include <util/setbaud.h>
    UBRR0 = UBRR_VALUE;
#if USE_2X
    UCSR0A |= (1<<U2X0);
#else
    UCSR0A &= ~(1<<U2X0);
#endif
    UCSR0C |= (1<<UCSZ01)|(1<<UCSZ00);              // No parity, 8 bits comm, 1 stop bit
    UCSR0B |= (1<<RXCIE0)|(1<<RXEN0)|(1<<TXEN0);    // Enable reception, transmission, and reception interrupts
    sei();

    tx_mask = ir_mask;

	// Use LEDs to flash power on indicator signal.
    uint8_t i;
    for (i=0; i<5; i++) {
        blue_port |= blue_mask;
        green_port |= green_mask;
        _delay_ms(200);
        blue_port &= ~blue_mask;
        green_port &= ~green_mask;
        _delay_ms(200);
    }

	while(1) {
        if (has_new_packet) {
            has_new_packet = 0;
            switch(packet_type) {
            case PACKET_STOP:
                break;
            case PACKET_LEDTOGGLE:
                leds_toggle = !leds_toggle;
                if (leds_toggle)
                    blue_port |= blue_mask;
                else
                    blue_port &= ~blue_mask;
                break;
            case PACKET_FORWARDMSG:
                for (i = 0; i<sizeof(message_t)-sizeof(msg.crc); i++)
                    msg.rawdata[i] = new_packet[i+2];
                msg.crc = message_crc(&msg);
                while(!has_new_packet) {
                    message_send(&msg);
                    green_port |= green_mask;
                    _delay_ms(3);
                    green_port &= ~green_mask;
                    _delay_ms(3);
                }
                break;
            case PACKET_FORWARDRAWMSG:
                for (i = 0; i<sizeof(message_t); i++)
                    msg.rawdata[i] = new_packet[i+2];
                while(!has_new_packet) {
                    message_send(&msg);
                    green_port |= green_mask;
                    _delay_ms(3);
                    green_port &= ~green_mask;
                    _delay_ms(3);
                }
                break;
            case PACKET_BOOTPAGE:
                msg.type = BOOTPGM_PAGE;
                msg.bootmsg.page_address = new_packet[2];
                msg.bootmsg.unused = 0;
                cli();
                for (i = 0; i<SPM_PAGESIZE && !has_new_packet; i+=6) {
                    msg.bootmsg.page_offset = i/2;
                    memcpy(&msg.bootmsg.word1, new_packet+3+i, 6);
                    msg.crc = message_crc(&msg);
                    message_send(&msg);
                }
                sei();
                green_port |= green_mask;
                _delay_ms(10);
                green_port &= ~green_mask;
                _delay_ms(10);
                break;
            case PACKET_GPSFRAME:
                msg.type = GPS;
                msg.gpsmsg.unused = 0;
                cli();
                for (i = 2; i<PACKET_SIZE-7; i+=7) {
                    memcpy(&msg.gpsmsg, new_packet+i, 7);
                    if (msg.gpsmsg.id == 0)
                        break;
                    msg.crc = message_crc(&msg);
                    message_send(&msg);
                }
                sei();
                green_port |= green_mask;
                _delay_ms(10);
                green_port &= ~green_mask;
                _delay_ms(10);
                break;
            }
		}
	}

    return 0;
}

ISR(USART_RX_vect) {
    uint8_t rx = UDR0;

    packet_checksum ^= packet_buffer[packet_head];
    packet_buffer[packet_head] = rx;
    packet_checksum ^= rx;
    packet_head++;
    if (packet_head >= PACKET_SIZE)
        packet_head = 0;

    if (packet_buffer[packet_head] == PACKET_HEADER) {
        if (packet_checksum == 0) {
            uint16_t i;
            uint16_t num = PACKET_SIZE-packet_head;
            for (i = 0; i < num; i++)
                new_packet[i] = packet_buffer[i+packet_head];
            for (i = num; i < PACKET_SIZE; i++)
                new_packet[i] = packet_buffer[i-num];
            has_new_packet = 1;
            packet_type = new_packet[1];
        }
    }
}
