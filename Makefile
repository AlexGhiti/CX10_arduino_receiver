BOARD_TAG = nano328
ARDUINO_PORT = $(SERIAL_PORT)
ARDUINO_DIR = /usr/share/arduino
ARDUINO_LIBS = SPI RF24 

CFLAGS += -std=c99 -Werror

include Arduino.mk

