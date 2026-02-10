#ifndef __NFC_H
#define __NFC_H

#include <stdio.h>
#include "ws1850_iic.h"


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// #define SPEAK_ON() gpio_set(GPIOD_Pin1)
// #define SPEAK_OFF() gpio_reset(GPIOD_Pin1)

// #define LED2_ON() gpio_set(GPIOD_Pin7)
// #define LED2_OFF() gpio_reset(GPIOD_Pin7)

// #define LED1_ON() gpio_set(GPIOD_Pin6)
// #define LED1_OFF() gpio_reset(GPIOD_Pin6)

#define READ 0
#define WRITE 1

#define PICC_CID 0x00 // 0~14 ????
#define COM_PKT_CMD_REQB 0x30
#define HEAD 0x68

#define WATER_LEVEL 16 //
#define FIFO_SIZE 64
#define FSD 256 // Frame Size for proximity coupling Device

#define READ_REG_CTRL 0x80
#define TP_FWT_302us 2048
#define TP_dFWT 192

#define MAX_RX_REQ_WAIT_MS 5000 // ????????100ms

// #define BIT7  0X80
// #define BIT6  0X40
// #define BIT5  0X20
// #define BIT4  0X10
// #define BIT3  0X08
// #define BIT2  0X04
// #define BIT1  0X02
// #define BIT0  0X01

/*
 * DEFINES Registers bits
 ****************************************************************
 */
#define TxIEn BIT6
#define RxIEn BIT5
#define IdleIEn BIT4
#define ErrIEn BIT1
#define TimerIEn BIT0
#define TxIRq BIT6
#define RxIRq BIT5
#define IdleIRq BIT4
#define ErrIRq BIT1
#define TimerIRq BIT0

#define CollErr BIT3
#define CrcErr BIT2
#define ParityErr BIT1
#define ProtocolErr BIT0

#define CollPos (BIT0 | BIT1 | BIT2 | BIT3 | BIT4)

#define RxAlign (BIT4 | BIT5 | BIT6)
#define TxLastBits (BIT0 | BIT1 | BIT2)

/**
 * Mifare Error Codes
 * Each function returns a status value, which corresponds to
 * the mifare error
 * codes.
 ****************************************************************
 */

#define MI_OK 0
#define MI_ERR 2
#define MI_CHK_OK 0
#define MI_CRC_ZERO 0
#define NOT_Pay 3
#define OVER_money 3
#define KEY_err 4

#define MI_CRC_NOTZERO 1

#define MI_NOTAGERR (2)
#define MI_CHK_FAILED (2)
#define MI_CRCERR (2)
#define MI_CHK_COMPERR (2)
#define MI_EMPTY (3)
#define MI_AUTHERR (4)
#define MI_PARITYERR (5)
#define MI_CODEERR (6)
#define MI_SERNRERR (8)
#define MI_KEYERR (9)
#define MI_NOTAUTHERR (10)
#define MI_BITCOUNTERR (11)
#define MI_BYTECOUNTERR (12)
#define MI_IDLE (13)
#define MI_TRANSERR (14)
#define MI_WRITEERR (15)
#define MI_INCRERR (16)
#define MI_DECRERR (17)
#define MI_READERR (18)
#define MI_OVFLERR (19)
#define MI_POLLING (20)
#define MI_FRAMINGERR (21)
#define MI_ACCESSERR (22)
#define MI_UNKNOWN_COMMAND (23)
#define MI_COLLERR (24)
#define MI_RESETERR (25)
#define MI_INITERR (25)
#define MI_INTERFACEERR (26)
#define MI_ACCESSTIMEOUT (27)
#define MI_NOBITWISEANTICOLL (28)
#define MI_QUIT (30)
#define MI_INTEGRITY_ERR (35) //?????(crc/parity/protocol)
#define MI_RECBUF_OVERFLOW (50)
#define MI_SENDBYTENR (51)
#define MI_SENDBUF_OVERFLOW (53)
#define MI_BAUDRATE_NOT_SUPPORTED (54)
#define MI_SAME_BAUDRATE_REQUIRED (55)
#define MI_WRONG_PARAMETER_VALUE (60)
#define MI_BREAK (99)
#define MI_NY_IMPLEMENTED (100)
#define MI_NO_MFRC (101)
#define MI_MFRC_NOTAUTH (102)
#define MI_WRONG_DES_MODE (103)
#define MI_HOST_AUTH_FAILED (104)
#define MI_WRONG_LOAD_MODE (106)
#define MI_WRONG_DESKEY (107)
#define MI_MKLOAD_FAILED (108)
#define MI_FIFOERR (109)
#define MI_WRONG_ADDR (110)
#define MI_DESKEYLOAD_FAILED (111)
#define MI_WRONG_SEL_CNT (114)
#define MI_WRONG_TEST_MODE (117)
#define MI_TEST_FAILED (118)
#define MI_TOC_ERROR (119)
#define MI_COMM_ABORT (120)
#define MI_INVALID_BASE (121)
#define MI_MFRC_RESET (122)
#define MI_WRONG_VALUE (123)
#define MI_VALERR (124)
#define MI_COM_ERR (125)
#define PROTOCOL_ERR (126)

///??????
#define USER_ERROR (127)
#define MAX_TRX_BUF_SIZE 255

#define UID_4 4
#define UID_7 7
#define FSDI 8 // Frame Size for proximity coupling Device, in EMV test. ?????FSDI = 8

// WS1850???
#define PCD_IDLE 0x00		//??????
#define PCD_AUTHENT 0x0E	//????
#define PCD_RECEIVE 0x08	//????
#define PCD_TRANSMIT 0x04	//????
#define PCD_TRANSCEIVE 0x0C //???????
#define PCD_RESETPHASE 0x0F //??
#define PCD_CALCCRC 0x03	// CRC??

// Mifare_One?????
#define PICC_REQIDL 0x26	//????????????
#define PICC_REQALL 0x52	//????????
#define PICC_ANTICOLL1 0x93 //???
#define PICC_ANTICOLL2 0x95 //???
#define PICC_AUTHENT1A 0x60 //??A??
#define PICC_AUTHENT1B 0x61 //??B??
#define PICC_READ 0x30		//??
#define PICC_WRITE 0xA0		//??
#define PICC_DECREMENT 0xC0 //??
#define PICC_INCREMENT 0xC1 //??
#define PICC_RESTORE 0xC2	//????????
#define PICC_TRANSFER 0xB0	//????????
#define PICC_HALT 0x50		//??

// FIFO????
// #define DEF_FIFO_LENGTH         64                 //FIFO size=64byte
#define MAXRLEN 18

//?????
// PAGE 0
#define RFU00 0x00
#define CommandReg 0x01
#define ComIEnReg 0x02
#define DivIEnReg 0x03
#define ComIrqReg 0x04
#define DivIrqReg 0x05
#define ErrorReg 0x06
#define Status1Reg 0x07
#define Status2Reg 0x08
#define FIFODataReg 0x09
#define FIFOLevelReg 0x0A
#define WaterLevelReg 0x0B
#define ControlReg 0x0C
#define BitFramingReg 0x0D
#define CollReg 0x0E
#define RFU0F 0x0F
// PAGE 1
#define RFU10 0x10
#define ModeReg 0x11
#define TxModeReg 0x12
#define RxModeReg 0x13
#define TxControlReg 0x14
#define TxASKReg 0x15
#define TxSelReg 0x16
#define RxSelReg 0x17
#define RxThresholdReg 0x18
#define DemodReg 0x19
#define RFU1A 0x1A
#define RFU1B 0x1B
#define MfTxReg 0x1C
#define MfRxReg 0x1D
#define TypeBReg 0x1E
#define SerialSpeedReg 0x1F
// PAGE 2
#define RFU20 0x20
#define CRCResultRegM 0x21
#define CRCResultRegL 0x22
#define RFU23 0x23
#define ModWidthReg 0x24
#define RFU25 0x25
#define RFCfgReg 0x26
#define GsNReg 0x27
#define CWGsPReg 0x28
#define ModGsPReg 0x29
#define TModeReg 0x2A
#define TPrescalerReg 0x2B
#define TReloadRegH 0x2C
#define TReloadRegL 0x2D
#define TCounterValueRegH 0x2E
#define TCounterValueRegL 0x2F
// PAGE 		3
#define RFU30 0x30
#define TestSel1Reg 0x31
#define TestSel2Reg 0x32
#define TestPinEnReg 0x33
#define TestPinValueReg 0x34
#define TestBusReg 0x35
#define AutoTestReg 0x36
#define VersionReg 0x37
#define AnalogTestReg 0x38
#define TestDAC1Reg 0x39
#define TestDAC2Reg 0x3A
#define TestADCReg 0x3B
#define RFU3C 0x3C
#define RFU3D 0x3D
#define RFU3E 0x3E
#define RFU3F 0x3F

typedef struct Brod_Mess_1
{
	unsigned char BrodAdr;		///???
	unsigned char BrodBit;		///???
	unsigned char UartData[32]; ////??
} Brod_Mess;

typedef struct LPCDANDB_param_s
{
	unsigned char Modulation;  //????  29
	unsigned char PowerH;	   //????H  38
	unsigned char PowerL;	   //????L   39
	unsigned char Sensitivity; //???   3c
	unsigned char Sleep;	   //????  3d
	unsigned char Probe;	   //????3e
} LPCDANDB_param;

unsigned char Test_Communication(unsigned char cnt);
void PcdAntennaOn(void);
void PcdAntennaOff(void);
void PcdConfig(unsigned char type);

unsigned char PcdComMF522(signed char Command,
						  unsigned char *pInData,
						  unsigned char InLenByte,
						  unsigned char *pOutData,
						  unsigned int *pOutLenBit);
void PcdSetTmo(unsigned char fwi);
unsigned char ComReqA(void);
unsigned char ComReqB(void);
void delay_us(unsigned int n);
void delay_ms(unsigned int n);
unsigned char PcdRequest(unsigned char req_code, unsigned char *pTagType);
unsigned char PcdAnticoll(uint8_t mode, uint8_t *pSnr);
unsigned char PcdSelect(uint8_t Mode, uint8_t *pSnr, unsigned char *SAK);

unsigned char ReadCard(unsigned char addr, unsigned char *pwd, unsigned char mode);
void CalulateCRC(unsigned char *pIndata, unsigned char len, unsigned char *pOutData);
void Card_Check(void);

unsigned char IC_ver(void);
///< LPCD????
void SetLPCDMode(void); ////LPCD??
void pcd_lpcd_start(unsigned char delta, unsigned char swingscnt);
void pcd_lpcd_application(void);
void pcd_lpcd_end(void);

///<????
void ReadSector(void);
unsigned char ComReqA_Block(unsigned char rw, unsigned char Block); ////??A????  ????0
unsigned char PcdAuthState(unsigned char auth_mode, unsigned char addr, unsigned char *pKey, unsigned char *pSnr);
unsigned char PcdRead(unsigned char addr, unsigned char *pData);
uint8_t Read_Block(unsigned char mode, unsigned char Block);

///<???
void WriteSector(void); /////???
unsigned char PcdWrite(unsigned char addr, unsigned char *pData);

// hsf
void PcdReset(void);

#endif
