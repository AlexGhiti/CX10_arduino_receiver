#include "XN297_nRF24L01.h"
#include "printf.h"

RF24 radio(9,10);

void setup(void)
{
        Serial.begin(57600);
        printf_begin();
        printf("\n\rSniff CX10A/\n\r");

        XN297_init();
        radio.printDetails();
        delayMicroseconds(500);
}

void loop(void)
{
        if (radio.available()) {//read_register(STATUS) & BV(RX_DR)) {
                XN297_ReadPayload(packet, packet_size);
                int i = 0;
                while (i < packet_size) {
                        printf("%02X ",(int)packet[i]);  i++;
                }
                printf("\r\n");
        }

}
