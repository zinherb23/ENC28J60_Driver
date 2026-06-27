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

static uint16_t NextPacketPtr;

#define RXSTAT_OK      0x80

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

void enc_init(uint8_t* macaddr){
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
}

void check_duplex(){
	uint16_t retry = 0;
	//check LLSTATE
	if(!(readPhy(PHSTAT1) & (1<<2))){
		  uint8_t meg[] = {"Link was failed\r\n"};
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
		int len = sprintf(msg, "Link is failed\r\n");
		HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
		return;
	}else{
		char msg[64] = {0};
		int len = sprintf(msg, "Link is active\r\n");
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
}

void check_version(void){
	uint8_t erevid = enc_read(EREVID);
	char msg[50];
	int len = sprintf(msg, "EREVID = 0x%02X\r\n", erevid);
	HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);
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

	/*
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	set_bank(ETXSTL);
	writeOp(WRITE_CTRL_REG,ETXSTL,TXBUFF_START & 0xFF);
	writeOp(WRITE_CTRL_REG,ETXSTH,TXBUFF_START >> 8);
	//writeOp(WRITE_CTRL_REG,EWRPTL,TXBUFF_START & 0xFF);
	//writeOp(WRITE_CTRL_REG,EWRPTH,TXBUFF_START >> 8);

	uint8_t control = 0x00;
	write_buffer(&control, 1);
	write_buffer(data, len);

	//uint16_t end = TXBUFF_START+len;
	//writeOp(WRITE_CTRL_REG,ETXNDL,end & 0xFF);
	//writeOp(WRITE_CTRL_REG,ETXNDH,end >> 8);

	writeOp(BIT_FIELD_SET,ECON1,0x08);
	//???
	uint8_t megdata = readOp(READ_CTRL_REG,ECON1);
	char meg[30];
	int meglen = sprintf(meg,"sendpacket_ECON1 = %#04x\r\n",megdata);
	HAL_UART_Transmit(&huart2,(uint8_t*)meg,meglen,HAL_MAX_DELAY);
	*/
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
	return(rxlen);
}


void sendarp(uint8_t* macaddr){
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
}




/*
//read and write data from register
static uint8_t readOp(uint8_t op, uint8_t address){
	uint8_t tx[3] = { op | (address & ADDR_MASK), 0x00, 0x00};
	uint8_t rx[3];
	SELECT();
	HAL_SPI_TransmitReceive(&hspi3,tx,rx,sizeof(tx),HAL_MAX_DELAY);
	uint8_t temp = 0;
	if(address & 0x80){
		temp = rx[2];
	}else{
		temp = rx[1];
	}
	DESELECT();
	return temp;
}

static void writeOp(uint8_t op, uint8_t address, uint8_t data){
	uint8_t tx[2] = { op | (address & ADDR_MASK), data};
	SELECT();
	HAL_SPI_Transmit(&hspi3,tx,2,HAL_MAX_DELAY);
	DESELECT();
}



//read and write data from buffer
static void read_buffer(uint8_t* data, uint16_t len){
	uint8_t cmd = READ_BUFFER_MEM;
	SELECT();
	HAL_SPI_Transmit(&hspi3,&cmd,1,HAL_MAX_DELAY);
	HAL_SPI_Receive(&hspi3,data,len,HAL_MAX_DELAY);
	DESELECT();
}

static void write_buffer(uint8_t* data, uint16_t len){
	uint8_t cmd = WRITE_BUFFER_MEM;
	SELECT();
	HAL_SPI_Transmit(&hspi3,&cmd,1,HAL_MAX_DELAY);
	HAL_SPI_Transmit(&hspi3,data,len,HAL_MAX_DELAY);
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
	set_bank(MIREGADR);
	writeOp(WRITE_CTRL_REG,MIREGADR,address);
	writeOp(BIT_FIELD_SET,MICMD,0x01);
	set_bank(MISTAT);
	while(readOp(READ_CTRL_REG,MISTAT) & 0x01){
		uint8_t meg[]={"wait for readphy\r\n"};
		HAL_UART_Transmit(&huart2,(uint8_t*)meg,sizeof(meg),HAL_MAX_DELAY);
		HAL_Delay(500);
	}
	set_bank(MICMD);
	writeOp(BIT_FIELD_CLR,MICMD,0x01);
	uint8_t phylow = readOp(READ_CTRL_REG,MIRDL);
	uint8_t phyhigh = readOp(READ_CTRL_REG,MIRDH);
	return (uint16_t)phyhigh << 8 | phylow;
}

void writePhy(uint8_t address, uint16_t data){
	set_bank(MIREGADR);
	writeOp(WRITE_CTRL_REG,MIREGADR,address);
	writeOp(WRITE_CTRL_REG,MIWRL,data & 0xFF);
	writeOp(WRITE_CTRL_REG,MIWRH,data >> 8);
	set_bank(MISTAT);
	while(readOp(READ_CTRL_REG,MISTAT) & 0x01){
		uint8_t meg[]={"wait for writephy\r\n"};
		HAL_UART_Transmit(&huart2,(uint8_t*)meg,sizeof(meg),HAL_MAX_DELAY);
		HAL_Delay(500);
	}
	set_bank(0x00);
}
*/


void enc_init_deprecated(void){
	//soft reset
	SELECT();
	uint8_t cmd = SOFT_RESET;
	HAL_SPI_Transmit(&hspi3,&cmd,1,HAL_MAX_DELAY);
	DESELECT();

	while(!(readOp(READ_CTRL_REG,ESTAT) & 0x01)){
		uint8_t meg[]={"wait for ESTAT: CLKRDY\r\n"};
		HAL_UART_Transmit(&huart2,(uint8_t*)meg,sizeof(meg),HAL_MAX_DELAY);
		HAL_Delay(500);
	}

	set_bank(0x00);
	//program buffer start
	//writeOp(WRITE_CTRL_REG,ETXSTL,(TXBUFF_START & 0xFF));
	//writeOp(WRITE_CTRL_REG,ETXSTH,(TXBUFF_START >> 8));
	//writeOp(WRITE_CTRL_REG,ETXNDL,(TXBUFF_END & 0xFF));
	//writeOp(WRITE_CTRL_REG,ETXNDH,(TXBUFF_END >> 8));
	//writeOp(WRITE_CTRL_REG,ERXSTL,(RXBUFF_START & 0xFF));
	//writeOp(WRITE_CTRL_REG,ERXSTH,(RXBUFF_START >> 8));
	//writeOp(WRITE_CTRL_REG,ERXNDL,(RXBUFF_END & 0xFF));
	//writeOp(WRITE_CTRL_REG,ERXNDH,(RXBUFF_END >> 8));

	//writeOp(WRITE_CTRL_REG,ERXRDPTL,(RXRDBUFF_START & 0xFF));
	//writeOp(WRITE_CTRL_REG,ERXRDPTH,(RXRDBUFF_START >> 8));

	////writeOp(WRITE_CTRL_REG,ERDPTL,(RXBUFF_START & 0xFF));
	////writeOp(WRITE_CTRL_REG,ERDPTH,(RXBUFF_START >> 8));

	writeOp(WRITE_CTRL_REG,EWRPTL,(TXBUFF_START & 0xFF));
	writeOp(WRITE_CTRL_REG,EWRPTH,(TXBUFF_START >> 8));

	set_bank(MACON1);
	//writeOp(WRITE_CTRL_REG,MACON1,0x0D);
	//writeOp(WRITE_CTRL_REG,MACON3,0x32);
	//writeOp(WRITE_CTRL_REG,MACON4,0x40);
	//writeOp(WRITE_CTRL_REG,MABBIPG,0x12);
	//writeOp(WRITE_CTRL_REG,MAIPGL,0x12);
	//writeOp(WRITE_CTRL_REG,MAIPGH,0x0C);
	//writeOp(WRITE_CTRL_REG,MAMXFLL,0xEE);
	//writeOp(WRITE_CTRL_REG,MAMXFLH,0x05);

	set_bank(MAADR1);
	//writeOp(WRITE_CTRL_REG, MAADR1, 0x02);
	//writeOp(WRITE_CTRL_REG, MAADR2, 0x02);
	//writeOp(WRITE_CTRL_REG, MAADR3, 0x02);
	//writeOp(WRITE_CTRL_REG, MAADR4, 0x02);
	//writeOp(WRITE_CTRL_REG, MAADR5, 0x02);
	//writeOp(WRITE_CTRL_REG, MAADR6, 0x02);

	//writePhy(PHCON1,0x0000); //half duplex
	//writePhy(PHCON2,0x0100); //HDLDIS
	writePhy(PHLCON,0x0472); //led

	set_bank(0x00);
	//writeOp(BIT_FIELD_SET,ECON1,0x04); //rxen
	writeOp(WRITE_CTRL_REG,ERXFCON,0x00);
}

void enc_sendpacket_deprecated(uint8_t* data, uint16_t len){
	set_bank(ETXSTL);
	writeOp(WRITE_CTRL_REG,ETXSTL,TXBUFF_START & 0xFF);
	writeOp(WRITE_CTRL_REG,ETXSTH,TXBUFF_START >> 8);
	writeOp(WRITE_CTRL_REG,EWRPTL,TXBUFF_START & 0xFF);
	writeOp(WRITE_CTRL_REG,EWRPTH,TXBUFF_START >> 8);

	uint8_t control = 0x00;
	write_buffer(&control, 1);
	write_buffer(data, len);

	uint16_t end = TXBUFF_START+len;
	writeOp(WRITE_CTRL_REG,ETXNDL,end & 0xFF);
	writeOp(WRITE_CTRL_REG,ETXNDH,end >> 8);

	writeOp(BIT_FIELD_SET,ECON1,0x08);
	//???
	uint8_t megdata = readOp(READ_CTRL_REG,ECON1);
	char meg[30];
	int meglen = sprintf(meg,"sendpacket_ECON1 = %#04x\r\n",megdata);
	HAL_UART_Transmit(&huart2,(uint8_t*)meg,meglen,HAL_MAX_DELAY);

}

uint8_t enc_receivepacket_deprecated(uint8_t* date, uint16_t len){
	//check pktcnt
	set_bank(EPKTCNT);
	uint8_t megdata = readOp(READ_CTRL_REG,EPKTCNT);
	set_bank(0x00);
	char meg[70];
	int meglen = sprintf(meg,"receivepacket_pktcnt = %#04x\r\n",megdata);
	HAL_UART_Transmit(&huart2,(uint8_t*)meg,meglen,HAL_MAX_DELAY);
	set_bank(0x00);

	//read buffer
	uint8_t buffer[100];
	read_buffer(buffer,sizeof(buffer));
	char meg2[70];
	int meg2len = sprintf(meg2,"message is %x %x %x %x %x %x\r\n%x %x %x %x %x %x\r\n"
			,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5]
			,buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11]);
	HAL_UART_Transmit(&huart2,(uint8_t*)meg2,meg2len,HAL_MAX_DELAY);

	return buffer[99];
}
void enc_sendARP_deprecated(){
	uint8_t myarp[66] = {
		//destination mac
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		//source mac
		0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
		//ethertype
		0x08, 0x06,
		//arp payload
		0x00, 0x01,
		0x08, 0x00,
		0x06, 0x04,
		0x00, 0x01,
		0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
		192, 168, 1, 200,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		192, 168, 1, 212,
		0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00
	};
	enc_sendpacket(myarp,sizeof(myarp));
};













