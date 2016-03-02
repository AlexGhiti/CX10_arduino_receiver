#include "XN297_nRF24L01.h"
#include "printf.h"
#include "UsbMouse.h"

#define BYPASS_TIMER_ISR 1

RF24 radio(9,10);

void setup(void)
{
        Serial.begin(57600);
        printf_begin();
        printf("\n\rSniff CX10A/\n\r");

        /* Mouse part */
#if BYPASS_TIMER_ISR
        // disable timer 0 overflow interrupt (used for millis)
        TIMSK0 &= !(1 << TOIE0); // ++
#endif
        UsbMouse.update();

        /* CX10 receiver part */
        XN297_init();
        radio.printDetails();
        delayMicroseconds(500);

}

uint8_t previous_packet[19];

void loop(void)
{

        /* CX10 receiver part */
        if (radio.available()) {//read_register(STATUS) & BV(RX_DR)) {
                XN297_ReadPayload(packet, packet_size);
                int i = 0;
                while (i < packet_size) {
                        printf("%02X ",(int)packet[i]);  i++;
                }
                printf("\r\n");
        }
        
        if (packet[17] != previous_packet[17]) {
                printf("Click ! %d != %d\n", packet[17], previous_packet[17]);
                UsbMouse.set_buttons(1, 0, 0);
        } else 
                UsbMouse.set_buttons(0, 0, 0);
      
        int x, y, z;

        if ((packet[9] != previous_packet[9]) || packet[9] == 0xD0 || packet[9] == 0xE8) {
                // We have to determine the direction
                // The user is going left
                if (packet[10] > 0x5) {
                        if ((previous_packet[9] < packet[9]) ||
                                (previous_packet[9] > packet[9] && previous_packet[10] < packet[10]) ||
                                (packet[9] == 0xD0 && packet[10] == 0x7)) {
                                //UsbMouse.move(-5, 0, 0);
                                x = -5;
                        } else 
                                x = 0;
                } else if (packet[10] < 0x5) {
                // The user is going right
                        if ((previous_packet[9] > packet[9]) ||
                                (previous_packet[9] < packet[9] && previous_packet[10] > packet[10]) ||
                                (packet[9] == 0xE8 && packet[10] == 0x3)) {
                                //UsbMouse.move(5, 0, 0);
                                x = 5;
                        } else
                                x = 0;
                } else 
                        x = 0;
        } else {
                x = 0; 
                //UsbMouse.move(0, 0, 0);
        }

        if ((packet[11] != previous_packet[11]) || packet[11] == 0xD0 || packet[11] == 0xE8) {
                // We have to determine the direction
                if (packet[12] > 0x5) {
                        if ((previous_packet[11] < packet[11]) ||
                                (previous_packet[11] > packet[11] && previous_packet[12] < packet[12]) ||
                                (packet[11] == 0xD0 && packet[12] == 0x7)) {
                                //UsbMouse.move(-5, 0, 0);
                                y = 5;
                        } else 
                                y = 0;
                } else if (packet[12] < 0x5) {
                        if ((previous_packet[11] > packet[11]) ||
                                (previous_packet[11] < packet[11] && previous_packet[12] > packet[12]) ||
                                (packet[11] == 0xE8 && packet[12] == 0x3)) {
                                //UsbMouse.move(5, 0, 0);
                                y = -5;
                        } else 
                                y = 0;
                } else 
                        y = 0;
        } else {
                y = 0; 
                //UsbMouse.move(0, 0, 0);
        }

        UsbMouse.move(x, y, 0);
        UsbMouse.update();
#if BYPASS_TIMER_ISR  // check if timer isr fixed.
        delayMicroseconds(20000);
#else
        delay(20);
#endif
        for (int i = 0; i < packet_size; ++i)
                previous_packet[i] = packet[i];
}
