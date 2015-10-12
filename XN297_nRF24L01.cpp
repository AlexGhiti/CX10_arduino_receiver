#include "XN297_nRF24L01.h"

#define TXRX_OFF 0
#define TX_EN    1
#define RX_EN    2

#define CX10A_PACKET_SIZE       19
#define CX10A_PACKET_PERIOD     6000
// Bit vector from bit position
#define BV(bit) (1 << bit)

static int xn297_addr_len;
static uint8_t xn297_tx_addr[5];
static uint8_t xn297_rx_addr[5];
static uint8_t xn297_crc = 0;
static uint8_t is_xn297 = 0;
static const uint8_t xn297_scramble[] = {
        0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
        0x0d, 0xae, 0x8c, 0x88, 0x12, 0x69, 0xee, 0x1f,
        0xc7, 0x62, 0x97, 0xd5, 0x0b, 0x79, 0xca, 0xcc,
        0x1b, 0x5d, 0x19, 0x10, 0x24, 0xd3, 0xdc, 0x3f,
        0x8e, 0xc5, 0x2f};

static const uint16_t xn297_crc_xorout[] = {
        0x0000, 0x3448, 0x9BA7, 0x8BBB, 0x85E1, 0x3E8C, // 1st entry is missing, probably never needed
        0x451E, 0x18E6, 0x6B24, 0xE7AB, 0x3828, 0x8148, // it's used for 3-byte address w/ 0 byte payload only
        0xD461, 0xF494, 0x2503, 0x691D, 0xFE8B, 0x9BA7,
        0x8B17, 0x2920, 0x8B5F, 0x61B1, 0xD391, 0x7401, 
        0x2138, 0x129F, 0xB3A0, 0x2988};

#define RF_BIND_CHANNEL         0x02
#define NUM_RF_CHANNELS         4
static uint8_t current_chan = 0;
static uint8_t txid[4];
static uint8_t rf_chans[4];

static uint8_t rf_setup;

enum {
        CX10_INIT1 = 0,
        CX10_BIND1,
        CX10_BIND2,
        CX10_DATA,
        CX10_RECV,
};

enum TxPower {
    TXPOWER_100uW,
    TXPOWER_300uW,
    TXPOWER_1mW,
    TXPOWER_3mW,
    TXPOWER_10mW,
    TXPOWER_30mW,
    TXPOWER_100mW,
    TXPOWER_150mW,
    TXPOWER_LAST,
};

uint8_t packet[CX10A_PACKET_SIZE]; // CX10A (blue board) has larger packet size
uint8_t packet_size;

const uint8_t rx_address[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
const uint64_t rx_address_64 = 0xCCCCCCCCCCull;
static const uint16_t polynomial = 0x1021;
static const uint16_t initial    = 0xb5d2;

static uint16_t crc16_update(uint16_t crc, unsigned char a)
{
    crc ^= a << 8;
    for (int i = 0; i < 8; ++i) {
        if (crc & 0x8000) {
            crc = (crc << 1) ^ polynomial;
        } else {
            crc = crc << 1;
        }
    }
    return crc;
}

static uint8_t bit_reverse(uint8_t b_in)
{
    uint8_t b_out = 0;
    for (int i = 0; i < 8; ++i) {
        b_out = (b_out << 1) | (b_in & 1);
        b_in >>= 1;
    }
    return b_out;
}

void XN297_SetRXAddr(const uint8_t* addr, int len)
{
        if (len > 5) len = 5;
        if (len < 3) len = 3;
        uint8_t buf[] = { 0, 0, 0, 0, 0 };
        memcpy(buf, addr, len);
        memcpy(xn297_rx_addr, addr, len);
        for (int i = 0; i < xn297_addr_len; ++i) {
                buf[i] = xn297_rx_addr[i] ^ xn297_scramble[xn297_addr_len-i-1];
        }
        radio.write_register(SETUP_AW, len-2);
        radio.write_register(RX_ADDR_P0, buf, 5);
}

void NRF24L01_Initialize()
{
    rf_setup = 0x0F;
}

uint8_t XN297_ReadPayload(uint8_t* msg, int len)
{
    // TODO: if xn297_crc==1, check CRC before filling *msg 
    uint8_t res = radio.read_payload(msg, len);
    for(uint8_t i=0; i<len; i++)
      msg[i] = bit_reverse(msg[i])^bit_reverse(xn297_scramble[i+xn297_addr_len]);
    return res;
}

void NRF24L01_SetTxRxMode(int mode)
{
    if(mode == TX_EN) {
        radio.ce(LOW);
        radio.write_register(STATUS, (1 << RX_DR)    //reset the flag(s)
                                            | (1 << TX_DS)
                                            | (1 << MAX_RT));
        radio.write_register(CONFIG, (1 << EN_CRC)   // switch to TX mode
                                            | (1 << CRCO)
                                            | (1 << PWR_UP));
        delayMicroseconds(130);
        radio.ce(HIGH);
    } else if (mode == RX_EN) {
        radio.ce(LOW);
        radio.write_register(STATUS, 0x70);        // reset the flag(s)
        radio.write_register(CONFIG, 0x0F);        // switch to RX mode
        radio.write_register(STATUS, (1 << RX_DR)    //reset the flag(s)
                                            | (1 << TX_DS)
                                            | (1 << MAX_RT));
        radio.write_register(CONFIG, (1 << EN_CRC)   // switch to RX mode
                                            | (1 << CRCO)
                                            | (1 << PWR_UP)
                                            | (1 << PRIM_RX));
        delayMicroseconds(130);
        radio.ce(HIGH);
    } else {
        radio.write_register(CONFIG, (1 << EN_CRC)); //PowerDown
        radio.ce(LOW);
    }
}

void XN297_Configure(uint8_t flags)
{
    if (!is_xn297) {
        xn297_crc = !!(flags & BV(EN_CRC));
        flags &= ~(BV(EN_CRC) | BV(CRCO));
    }
    radio.write_register(CONFIG, flags);
}

void XN297_SetTXAddr(const uint8_t* addr, int len)
{
    if (len > 5) len = 5;
    if (len < 3) len = 3;
    if (is_xn297) {
        uint8_t buf[] = { 0, 0, 0, 0, 0 };
        memcpy(buf, addr, len);
        radio.write_register(SETUP_AW, len-2);
        radio.write_register(RX_ADDR_P0, buf, 5);
    } else {
        uint8_t buf[] = { 0x55, 0x0F, 0x71, 0x0C, 0x00 }; // bytes for XN297 preamble 0xC710F55 (28 bit)
        xn297_addr_len = len;
        if (xn297_addr_len < 4) {
            for (int i = 0; i < 4; ++i) {
                buf[i] = buf[i+1];
            }
        }
        radio.write_register(SETUP_AW, len-2);
        radio.write_register(TX_ADDR, buf, 5);
        // Receive address is complicated. We need to use scrambled actual address as a receive address
        // but the TX code now assumes fixed 4-byte transmit address for preamble. We need to adjust it
        // first. Also, if the scrambled address begings with 1 nRF24 will look for preamble byte 0xAA
       // instead of 0x55 to ensure enough 0-1 transitions to tune the receiver. Still need to experiment
        // with receiving signals.
        memcpy(xn297_tx_addr, addr, len);
    }
}


uint8_t XN297_WritePayload(uint8_t* msg, int len)
{
    uint8_t packet[32];
    uint8_t res;
    /* TODO remove is_xn297, we are not */
    if (is_xn297) {
        res = radio.write_payload(msg, len);
    } else {
        int last = 0;
        if (xn297_addr_len < 4) {
            // If address length (which is defined by receive address length)
            // is less than 4 the TX address can't fit the preamble, so the last
            // byte goes here
            packet[last++] = 0x55;
        }
        for (int i = 0; i < xn297_addr_len; ++i) {
            packet[last++] = xn297_tx_addr[xn297_addr_len-i-1] ^ xn297_scramble[i];
        }

        for (int i = 0; i < len; ++i) {
            // bit-reverse bytes in packet
            uint8_t b_out = bit_reverse(msg[i]);
            packet[last++] = b_out ^ xn297_scramble[xn297_addr_len+i];
        }
        if (xn297_crc) { 
            int offset = xn297_addr_len < 4 ? 1 : 0;
            uint16_t crc = initial;
            for (int i = offset; i < last; ++i) {
                crc = crc16_update(crc, packet[i]);
            }
            crc ^= xn297_crc_xorout[xn297_addr_len - 3 + len];
            packet[last++] = crc >> 8;
            packet[last++] = crc & 0xff;
        }
        res = radio.write_payload(packet, last);
    }
    return res;
}

uint8_t NRF24L01_SetPower(uint8_t power)
{
    uint8_t nrf_power = 0;
    switch(power) {
        case TXPOWER_100uW: nrf_power = 0; break;
        case TXPOWER_300uW: nrf_power = 0; break;
        case TXPOWER_1mW:   nrf_power = 0; break;
        case TXPOWER_3mW:   nrf_power = 1; break;
        case TXPOWER_10mW:  nrf_power = 1; break;
        case TXPOWER_30mW:  nrf_power = 2; break;
        case TXPOWER_100mW: nrf_power = 3; break;
        case TXPOWER_150mW: nrf_power = 3; break;
        default:            nrf_power = 0; break;
    };
    // Power is in range 0..3 for nRF24L01
    rf_setup = (rf_setup & 0xF9) | ((nrf_power & 0x03) << 1);
    return radio.write_register(RF_SETUP, rf_setup);
}


static void send_packet(uint8_t bind)
{
        //uint8_t offset=0;
        //offset = 4;
        packet[0] = bind ? 0xAA : 0x55;
        packet[1] = txid[0];
        packet[2] = txid[1];
        packet[3] = txid[2];
        packet[4] = txid[3];
        // for CX-10A [5]-[8] is aircraft id received during bind 
        //read_controls(&throttle, &rudder, &elevator, &aileron, &flags, &flags2);
        //packet[5+offset] = aileron & 0xff;
        //packet[6+offset] = (aileron >> 8) & 0xff;
        //packet[7+offset] = elevator & 0xff;
        //packet[8+offset] = (elevator >> 8) & 0xff;
        //packet[9+offset] = throttle & 0xff;
        //packet[10+offset] = (throttle >> 8) & 0xff;
        //packet[11+offset] = rudder & 0xff;
        //packet[12+offset] = ((rudder >> 8) & 0xff) | ((flags & FLAG_FLIP) >> 8);  // 0x10 here is a flip flag 
        //packet[13+offset] = flags & 0xff;
        //packet[14+offset] = flags2 & 0xff;

        // Power on, TX mode, 2byte CRC
        // Why CRC0? xn297 does not interpret it - either 16-bit CRC or nothing
        XN297_Configure(BV(EN_CRC) | BV(CRCO) | BV(PWR_UP));
        if (bind) {
                radio.write_register(RF_CH, RF_BIND_CHANNEL);
        } else {
                radio.write_register(RF_CH, rf_chans[current_chan++]);
                current_chan %= NUM_RF_CHANNELS;
        }
        // clear packet status bits and TX FIFO
        radio.write_register(STATUS, 0x70);
        radio.flush_tx();

        XN297_WritePayload(packet, packet_size);

        //    radio.ce(HIGH);
        //    delayMicroseconds(15);
        // It saves power to turn off radio after the transmission,
        // so as long as we have pins to do so, it is wise to turn
        // it back.
        //    radio.ce(LOW);

        // Check and adjust transmission power. We do this after
        // transmission to not bother with timeout after power
        // settings change -  we have plenty of time until next
        // packet.
        //if (tx_power != global_tx_power) {
        //        //Keep transmit power updated
        //        tx_power = global_tx_power;
        //        NRF24L01_SetPower(tx_power);
        //}
}

uint8_t NRF24L01_SetBitrate(uint8_t bitrate)
{
        // Note that bitrate 250kbps (and bit RF_DR_LOW) is valid only
        // for nRF24L01+. There is no way to programmatically tell it from
        // older version, nRF24L01, but the older is practically phased out
        // by Nordic, so we assume that we deal with with modern version.

        // Bit 0 goes to RF_DR_HIGH, bit 1 - to RF_DR_LOW
        rf_setup = (rf_setup & 0xD7) | ((bitrate & 0x02) << 4) | ((bitrate & 0x01) << 3);
        return radio.write_register(RF_SETUP, rf_setup);
}

void XN297_init() 
{
        txid[0] = 0;
        txid[1] = 0;
        txid[2] = 0;
        txid[3] = 0;
        rf_chans[0] = 0x03;
        rf_chans[1] = 0x16;
        rf_chans[2] = 0x2D;
        rf_chans[3] = 0x40;
        packet_size = CX10A_PACKET_SIZE;
        for (uint8_t i = 0; i < 4; i++)
                packet[5 + i] = 0xFF; // clear aircraft id
        packet[9] = 0;
radio.begin();

        NRF24L01_Initialize();
        NRF24L01_SetTxRxMode(TX_EN);
        XN297_SetTXAddr(rx_address, 5);
        XN297_SetRXAddr(rx_address, 5);

        radio.flush_tx();
        radio.flush_rx();

        radio.write_register(STATUS, 0x70);
        radio.write_register(EN_AA, 0x00);
        radio.write_register(EN_RXADDR, 0x01);
        radio.write_register(RX_PW_P0, CX10A_PACKET_SIZE);
        radio.write_register(RF_CH, RF_BIND_CHANNEL);
        radio.write_register(RF_SETUP, 0x07);
        NRF24L01_SetBitrate(0); //1M 
        NRF24L01_SetPower(3);
        // this sequence necessary for module from stock tx
        radio.read_register(FEATURE);
        radio.toggle_features();
        radio.read_register(FEATURE);
        radio.write_register(DYNPD, 0x00);       // Disable dynamic payload length on all pipes
        radio.write_register(FEATURE, 0x00);     // Set feature bits on

        NRF24L01_SetTxRxMode(TXRX_OFF);
        NRF24L01_SetTxRxMode(RX_EN);
        XN297_Configure(BV(EN_CRC) | BV(CRCO) | BV(PWR_UP) | BV(PRIM_RX));
}


