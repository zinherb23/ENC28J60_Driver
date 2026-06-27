/*
 * enc.c
 */
#include "enc.h"
#include "stm32f4xx_hal.h"
#include "stdio.h"

extern SPI_HandleTypeDef hspi3;
extern UART_HandleTypeDef huart2;

#define CS_PORT GPIOA
#define CS_PIN  GPIO_PIN_4

//#define ENC28J60_MAC0 0x02
//#define ENC28J60_MAC1 0x1A
//#define ENC28J60_MAC2 0x2B
//#define ENC28J60_MAC3 0x3C
//#define ENC28J60_MAC4 0x4D
//#define ENC28J60_MAC5 0x5E

//#define RXRDBUFF_START 0x1FFF

#define MISTAT_BUSY   0x01
#define ESTAT_CLKRDY  0x01

//ERXFCON setup
#define ERXFCON_UCEN   0x80
#define ERXFCON_ANDOR  0x40
#define ERXFCON_CRCEN  0x20
#define ERXFCON_PMEN   0x10
#define ERXFCON_MPEN   0x08
#define ERXFCON_HTEN   0x04
#define ERXFCON_MCEN   0x02
#define ERXFCON_BCEN   0x01

//ECON1 setup
#define ECON1_TXRTS    0x08

//ECON2
#define ECON2_PKTDEC   0x40

//MACON1 setup
#define MACON1_TXPAUS  0x08
#define MACON1_RXPAUS  0x04
#define MACON1_PASSALL 0x02
#define MACON1_MARXEN  0x01

//MACON3 setup
#define MACON3_PADCFG0 0x20
#define MACON3_TXCRCEN 0x10
#define MACON3_FRMLNEN 0x02
#define MACON3_FULDPX  0x01

//MACON4 setup
//no?


#define FRAMELEN 1500

//PHCON1
#define PHCON1_PDPXMD  0x0100

//PHCON2
#define PHCON2_HDLDIS  0x0100

#define SELECT()   HAL_GPIO_WritePin(CS_PORT,CS_PIN, GPIO_PIN_RESET);
#define DESELECT() HAL_GPIO_WritePin(CS_PORT,CS_PIN, GPIO_PIN_SET);

//others
static uint16_t NextPacketPtr;
#define RXSTAT_OK      0x80
uint8_t macaddr[6] = {0x00,0x11,0x22,0x33,0x44,0x55};

//read and write 1 byte
static uint8_t SPI_ReadWrite(uint8_t tx){
	uint8_t rx;
	HAL_SPI_TransmitReceive(&hspi3,&tx,&rx,sizeof(tx),HAL_MAX_DELAY);
	return rx;
}

//read and write data from register
static uint8_t readOp(uint8_t op, uint8_t address){
	uint8_t opdata = op | (address & ADDR_MASK);
	SELECT();
	SPI_ReadWrite(opdata);
	opdata = SPI_ReadWrite(0xFF);
	if(address & 0x80){
		opdata = SPI_ReadWrite(0xFF);
	}
	DESELECT();
	return opdata;
}
static void writeOp(uint8_t op, uint8_t address, uint8_t data){
	uint8_t opdata = op | (address & ADDR_MASK);
	SELECT();
	SPI_ReadWrite(opdata);
	SPI_ReadWrite(data);
	DESELECT();
}


//read and write data from buffer
static void read_buffer(uint8_t* data, uint16_t len){
	SELECT();
	SPI_ReadWrite(READ_BUFFER_MEM);
	while(len){
		len--;
		*data = (uint8_t)SPI_ReadWrite(0xFF);
		data++;
	}
	*data = '\0';
	DESELECT();
}

static void write_buffer(uint8_t* data, uint16_t len){
	SELECT();
	SPI_ReadWrite(WRITE_BUFFER_MEM);
	while(len){
		len--;
		SPI_ReadWrite(*data);
		data++;
	}
	DESELECT();
}

//set the bank
static uint8_t currentbank = 0x00;
static void set_bank(uint8_t address){
	uint8_t bank = address & BANK_MASK;
	if(bank != currentbank){
		writeOp(BIT_FIELD_CLR,ECON1,0x03);
		writeOp(BIT_FIELD_SET,ECON1,bank>>5);
		currentbank = bank;
	}
}

// second layer operation for setting bank
static uint8_t enc_read(uint8_t address){
	set_bank(address);
	return readOp(READ_CTRL_REG, address);
}

static void enc_write(uint8_t address, uint8_t data){
	set_bank(address);
	writeOp(WRITE_CTRL_REG, address, data);
}

// read and write the data in PHY register
uint16_t readPhy(uint8_t address){
	uint16_t retry = 0;
	uint16_t data;
	enc_write(MIREGADR,address);
	enc_write(MICMD,0x01);
	while((enc_read(MISTAT) & MISTAT_BUSY) && retry < 0xFF){
		retry++;
	}
	if(retry >= 0xFF){
		char msg[64] = {0};
		int len = sprintf(msg, "Readphy is failed: %c\r\n", address);
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}
	enc_write(MICMD,0x00);
	data = enc_read(MIRDH) << 8;
	data |= enc_read(MIRDL);
	return data;
	//writeOp(BIT_FIELD_SET,MICMD,0x01);
}

void writePhy(uint8_t address, uint16_t data){
	uint16_t retry = 0;
	enc_write(MIREGADR,address);
	enc_write(MIWRL,data);
	enc_write(MIWRH,data>>8);
	while((enc_read(MISTAT) & MISTAT_BUSY) && retry < 0xFF){
		retry++;
		char msg[64] = {0};
		int len = sprintf(msg, "Wait for writePhy - Retry: %d\r\n", retry);
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}
}

void enc_init(){
	uint16_t retry = 0;
	writeOp(SOFT_RESET,0,SOFT_RESET);
	HAL_Delay(1);
	while(!(enc_read(ESTAT) & ESTAT_CLKRDY) && retry < 0xFF){
		retry++;
	}
	if(retry < 0xFF){
		char msg[64] = {0};
		int len = sprintf(msg, "Start of initiation is successful.\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}else{
		char msg[64] = {0};
		int len = sprintf(msg, "Start of initiation is failed.\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}

	NextPacketPtr = RXBUFF_START;
	//receive buffer read
	enc_write(ERXRDPTL,(RXBUFF_START & 0xFF));
	enc_write(ERXRDPTH,(RXBUFF_START >> 8));
	//receive buffer start
	enc_write(ERXSTL,(RXBUFF_START & 0xFF));
	enc_write(ERXSTH,(RXBUFF_START >> 8));
	//receive buffer end
	enc_write(ERXNDL,(RXBUFF_END & 0xFF));
	enc_write(ERXNDH,(RXBUFF_END >> 8));
	//write buffer start
	enc_write(ETXSTL,(TXBUFF_START & 0xFF));
	enc_write(ETXSTH,(TXBUFF_START >> 8));
	//write buffer end
	enc_write(ETXNDL,(TXBUFF_END & 0xFF));
	enc_write(ETXNDH,(TXBUFF_END >> 8));
	//ERXFCON setup
	enc_write(ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_PMEN);
	enc_write(EPMM0, 0x3f);
	enc_write(EPMM1, 0x30);
	enc_write(EPMCSL, 0xf9);
	enc_write(EPMCSH, 0xf7);
	//MACON setup
	enc_write(MACON1, MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS);
	//MACON2 is reserved or not? A:Version 6 enable MACON2
	enc_write(MACON2,0x00);
	//auto negotiation issue, need to set object to "full duplex"
	writeOp(BIT_FIELD_SET,MACON3,\
			MACON3_PADCFG0 | \
			MACON3_TXCRCEN | \
			MACON3_FRMLNEN | \
			MACON3_FULDPX);
	//half vs full duplex issue
	enc_write(MAIPGL,0x12);
	enc_write(MAIPGH,0x0C);
	enc_write(MABBIPG,0x15);
	enc_write(MAMXFLL,FRAMELEN & 0xFF);
	enc_write(MAMXFLH,FRAMELEN >> 8);

	//mac address issue
	enc_write(MAADR5, macaddr[0]);
	enc_write(MAADR4, macaddr[1]);
	enc_write(MAADR3, macaddr[2]);
	enc_write(MAADR2, macaddr[3]);
	enc_write(MAADR1, macaddr[4]);
	enc_write(MAADR0, macaddr[5]);

	//PHCON address issue
	writePhy(PHCON1,PHCON1_PDPXMD);
	writePhy(PHCON2,PHCON2_HDLDIS);

	set_bank(ECON1);
	//EIE: interrupt
	writeOp(BIT_FIELD_SET, EIE, EIE_INTIE | EIE_PKTIE);
	//RXEN
	writeOp(BIT_FIELD_SET, ECON1, ECON1_RXEN);

	if(enc_read(MAADR5) == macaddr[0]){
		char msg[64] = {0};
		int len = sprintf(msg, "Full initiation is successful.\n\r");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}else{
		char msg[64] = {0};
		int len = sprintf(msg, "Full initiation is failed.\n\r");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}
	HAL_Delay(2000);
}

void check_duplex(){
	uint16_t retry = 0;
	//check LLSTATE
	if(!(readPhy(PHSTAT1) & (1<<2))){
		  uint8_t meg[] = {"LLSTATE was failed\r\n"};
		  HAL_UART_Transmit(&huart2,meg,sizeof(meg),HAL_MAX_DELAY);
	}else{
		  uint8_t meg[] = {"LLSTATE was active\r\n"};
		  HAL_UART_Transmit(&huart2,meg,sizeof(meg),HAL_MAX_DELAY);
	}

	//check LSTATE
	do{
		if(readPhy(PHSTAT2) & (1<<10)) break;
		HAL_Delay(10);
		retry++;
	}while(retry < 0xFF);
	if(retry >= 0xFF){
		char msg[64] = {0};
		int len = sprintf(msg, "LSTATE is failed\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
		return;
	}else{
		char msg[64] = {0};
		int len = sprintf(msg, "LSTATE is active\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}

	//check duplex
	if(readPhy(PHSTAT2) & (1<<9)){
		  uint8_t meg[] = {"Full duplex is active\r\n"};
		  HAL_UART_Transmit(&huart2,meg,sizeof(meg),HAL_MAX_DELAY);
	}else{
		  uint8_t meg[] = {"Full duplex is NOT active\r\n"};
		  HAL_UART_Transmit(&huart2,meg,sizeof(meg),HAL_MAX_DELAY);
	}
	HAL_Delay(2000);
}

void check_version(void){
	uint8_t erevid = enc_read(EREVID);
	char msg[50];
	int len = sprintf(msg, "EREVID = 0x%02X\r\n", erevid);
	HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	HAL_Delay(2000);
}

void enc_sendpacket(uint8_t* data, uint16_t len){
	//start of send buffer
	enc_write(EWRPTL,TXBUFF_START & 0xFF);
	enc_write(EWRPTH,TXBUFF_START >> 8);
	//end of send buffer
	enc_write(ETXNDL,(TXBUFF_START+len) & 0xFF);
	enc_write(ETXNDH,(TXBUFF_START+len) >> 8);
	//control --default-macon3
	writeOp(WRITE_BUFFER_MEM, 0, 0x00);
	//send data to buffer
	write_buffer(data,len);
	//transfer the date
	writeOp(BIT_FIELD_SET,ECON1,ECON1_TXRTS);
	//error handle
	uint8_t eir = enc_read(EIR);
	while(!(eir & (EIR_TXERIF|EIR_TXIF)));
	if(eir & EIR_TXERIF){
		writeOp(BIT_FIELD_SET,ECON1,ECON1_TXRTS);
		char msg[50];
		int len = sprintf(msg, "Transmit failed\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);

	}else if(eir & EIR_TXIF){
		char msg[50];
		int len = sprintf(msg, "Transmit succeeded\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}else{
		char msg[50];
		int len = sprintf(msg, "Transmit failed: unknown\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}
	HAL_Delay(2000);
}

uint8_t enc_receivepacket(uint8_t* date, uint16_t len){
	uint16_t rxstat;
	uint16_t rxlen;
	//if rx buffer is empty
	if(enc_read(EPKTCNT) == 0){
		char msg[50];
		int len = sprintf(msg, "No packet, stop receiving\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
		return 0;
	}
	//read pointer in rx buffer
	enc_write(ERDPTL,NextPacketPtr);
	enc_write(ERDPTH,(NextPacketPtr>>8));
	//next pointer of a packet
	NextPacketPtr = readOp(READ_BUFFER_MEM,0);
	NextPacketPtr |= (readOp(READ_BUFFER_MEM,0)<<8);
	//rsv: len
	rxlen = readOp(READ_BUFFER_MEM,0);
	rxlen |= (readOp(READ_BUFFER_MEM,0)<<8);
	//cut off crc
	rxlen -= 4;
	//rsv: status
	rxstat = readOp(READ_BUFFER_MEM,0);
	rxstat |= (readOp(READ_BUFFER_MEM,0)<<8);
	//length limit
	if(rxlen > len-1){
		rxlen = len-1;
	}
	//check if packet is okay
	if((rxstat & RXSTAT_OK) == 0){
		rxlen = 0;
	}else{
		read_buffer(date,rxlen);
	}
	if(rxlen == 0){
		char msg[50];
		int len = sprintf(msg, "Receive failed\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
		return 0;
	}else{
		char msg[50];
		int len = sprintf(msg, "Receive succeed: buffer is read\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
	}
	//move RXRD pointer to unread packet
	enc_write(ERXRDPTL,NextPacketPtr);
	enc_write(ERXRDPTH,(NextPacketPtr>>8));
	//reduce packet counter
	writeOp(BIT_FIELD_SET,ECON2,ECON2_PKTDEC);

	HAL_Delay(2000);
	return(rxlen);
}


void sendarp(){

	uint8_t arp_request[60] = {0};

	//ethernet header
	//destination mac
	memset(arp_request, 0xFF, 6);
	//source mac
	arp_request[6] = macaddr[0];
	arp_request[7] = macaddr[1];
	arp_request[8] = macaddr[2];
	arp_request[9] = macaddr[3];
	arp_request[10] = macaddr[4];
	arp_request[11] = macaddr[5];
	//ethetype: arp
	arp_request[12] = 0x08;
	arp_request[13] = 0x06;

	//arp payload
	//hardware type: ethernet
	arp_request[14] = 0x00; arp_request[15] = 0x01;
	//protocol type: ipv4
	arp_request[16] = 0x08; arp_request[17] = 0x00;
	//hardware size: 6
	arp_request[18] = 0x06;
	//protocol size: 4
	arp_request[19] = 0x04;
	//opcode = request
	arp_request[20] = 0x00; arp_request[21] = 0x01;

	//source mac
	arp_request[22] = macaddr[0];
	arp_request[23] = macaddr[1];
	arp_request[24] = macaddr[2];
	arp_request[25] = macaddr[3];
	arp_request[26] = macaddr[4];
	arp_request[27] = macaddr[5];
	//source ip
	arp_request[28] = 192;
	arp_request[29] = 168;
	arp_request[30] = 1;
	arp_request[31] = 50;
	//target mac
	arp_request[32] = 0;
	arp_request[33] = 0;
	arp_request[34] = 0;
	arp_request[35] = 0;
	arp_request[36] = 0;
	arp_request[37] = 0;
	//target ip
	arp_request[38] = 192;
	arp_request[39] = 168;
	arp_request[40] = 1;
	arp_request[41] = 212;
	//send arp
	enc_sendpacket(arp_request,sizeof(arp_request));
	HAL_Delay(2000);
}


