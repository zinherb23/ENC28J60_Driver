/*
 * enc.h
 */

#ifndef ENC_H
#define ENC_H

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdint.h>

//register map
#define ADDR_MASK        0x1F
#define BANK_MASK        0x60
#define MMRD_MASK        0x80

//general register
#define EIE              0x1B
#define EIR              0x1C
#define ESTAT            0x1D
#define ECON2            0x1E
#define ECON1            0x1F

//bank 0 register
#define ERDPTL           0x00|0x00
#define ERDPTH           0x01|0x00
#define EWRPTL           0x02|0x00
#define EWRPTH           0x03|0x00
#define ETXSTL           0x04|0x00
#define ETXSTH           0x05|0x00
#define ETXNDL           0x06|0x00
#define ETXNDH           0x07|0x00
#define ERXSTL           0x08|0x00
#define ERXSTH           0x09|0x00
#define ERXNDL           0x0A|0x00
#define ERXNDH           0x0B|0x00
#define ERXRDPTL         0x0C|0x00
#define ERXRDPTH         0x0D|0x00
#define ERXWRPTL         0x0E|0x00
#define ERXWRPTH         0x0F|0x00

//bank 1 register
#define EPKTCNT          0x19|0x20
#define EPMM0            0x08|0x20
#define EPMM1            0x09|0x20
#define EPMCSL           0x10|0x20
#define EPMCSH           0x11|0x20
#define ERXFCON          0x18|0x20


//bank 2 register
#define MACON1           0x00|0x40|0x80
#define MACON2           0x01|0x40|0x80
#define MACON3           0x02|0x40|0x80
#define MACON4           0x03|0x40|0x80
#define MABBIPG          0x04|0x40|0x80
#define MAIPGL           0x06|0x40|0x80
#define MAIPGH           0x07|0x40|0x80
#define MAMXFLL          0x0A|0x40|0x80
#define MAMXFLH          0x0B|0x40|0x80
#define MICMD            0x12|0x40|0x80
#define MIREGADR         0x14|0x40|0x80
#define MIWRL            0x16|0x40|0x80
#define MIWRH            0x17|0x40|0x80
#define MIRDL            0x18|0x40|0x80
#define MIRDH            0x19|0x40|0x80

//bank 3 register
#define MAADR1           0x00|0x60|0x80
#define MAADR0           0x01|0x60|0x80
#define MAADR3           0x02|0x60|0x80
#define MAADR2           0x03|0x60|0x80
#define MAADR5           0x04|0x60|0x80
#define MAADR4           0x05|0x60|0x80
#define MISTAT           0x0A|0x60|0x80
//version:0x06
#define EREVID           0x12|0x60|0x80

//phy registers
#define PHCON1           0x00
#define PHSTAT1          0x01
#define PHCON2           0x10
#define PHSTAT2          0x11
#define PHLCON           0x14


//EIE registers
#define EIE_INTIE        0x80
#define EIE_PKTIE        0x40

//EIR registers
#define EIR_TXERIF       0x02
#define EIR_TXIF         0x08

//ECON registers
#define ECON1_RXEN       0x40

//SPI instruction set
#define READ_CTRL_REG    0x00
#define READ_BUFFER_MEM  0x3A
#define WRITE_CTRL_REG   0x40
#define WRITE_BUFFER_MEM 0x7A
#define BIT_FIELD_SET    0x80
#define BIT_FIELD_CLR    0xA0
#define SOFT_RESET       0xFF

//buffer scope
#define RXBUFF_START   0x0000
#define RXBUFF_END     0x0FFF
#define TXBUFF_START   0x1000
#define TXBUFF_END     0x1FFF

void enc_sendpacket(uint8_t* data, uint16_t len);
uint8_t enc_receivepacket(uint8_t* date, uint16_t len);
void writePhy(uint8_t address, uint16_t data);
uint16_t readPhy(uint8_t address);
void enc_init();
void check_version();
void check_duplex();
void sendarp();

#endif /* ENC_H */





