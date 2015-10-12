#ifndef _XN297_NRF24L01_H
#define _XN297_NRF24L01_H

#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"

#define CX10A_PACKET_SIZE       19
#define CX10A_PACKET_PERIOD     6000

extern RF24 radio;
extern uint8_t packet[CX10A_PACKET_SIZE];
extern uint8_t packet_size; 

void XN297_init();
uint8_t XN297_ReadPayload(uint8_t* msg, int len);

#endif
