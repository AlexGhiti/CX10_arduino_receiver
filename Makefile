BOARD_TAG = nano328
ARDUINO_PORT = $(SERIAL_PORT)
ARDUINO_DIR = /usr/share/arduino
ARDUINO_LIBS = SPI RF24 UsbMouse 

CFLAGS += -std=c99 -Werror

include Arduino.mk

