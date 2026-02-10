#include "nfc.h"

// #include "sdk_include.h"

#define SENSITIVITY  0x06
#define PROBE        0x04

unsigned char  KeyType=0;///????
unsigned char  uid_length=0;
unsigned char  CT[2];//???
unsigned char  IDA[10];//??A ?ID
unsigned char  IDB[10];//??B ?ID
unsigned char  LPCDflag=0;
unsigned char  PassWd[6]={0x00,0x00,0x00,0x00,0x00,0x00};


/**
* @brief us????
* @param n:????
* @return ?
*/
void delay_us(unsigned int n)
{
  unsigned int i;
  while(--n)
  {
    for(i=3;i>0;i--)
    {
      ;; 
    }
  }
}
/**
* @brief ms????
* @param n:????
* @return ?
*/
void delay_ms(unsigned int n)
{
#if 0
  unsigned int j,k;
  while(n)
  {
    j=100;
    do{
      k=31;
      do{
        ;;;;;//nop();nop();
      } while(--k);
    } while(--j);
    n--;	
  }
#else 
	vTaskDelay(pdMS_TO_TICKS(n));
#endif
}

void PcdReset(void)
{
#if 0    //hard reset  //
	NFC_RST_LOW;
	delay_ms(1);
	NFC_RST_HIGH;
	delay_ms(3);; // 请客户确认delay函数延时准确, 晶振起振时间要小于此值，并且留有余量。
#endif 
#if 1        /**** 软复位****/
	WriteRawRC(CommandReg,PCD_RESETPHASE);	
	delay_ms(3);
#endif
/********复位读版本号，测通讯是否成功***************/
// 	int temp;
//  temp = ReadRawRC(0x37); 
//  dbg_printf("Version : 0x%02x\n", temp);
}


unsigned char IC_ver(void)
{
	unsigned char status;
	// CLR_NFC_RST;
	// delay_us(500);
	// SET_NFC_RST;
  	// delay_us(800);

	// 使用软复位
	WriteRawRC(CommandReg,PCD_RESETPHASE);	
	delay_ms(3);

	//WriteRawRC(0x37,0x55);
	status=ReadRawRC(0x37);
	CLR_NFC_RST;
	return status;
}


void Card_Check(void)
{
	unsigned char  i;
	unsigned char  statusA;
	statusA = ComReqA();
	if( statusA ==MI_OK)
	{   
		printf("card type : %02X %02X ",CT[0],CT[1]);
		printf("card ID:");
		for(i=0;i<uid_length;i++)
		{
			printf("%02X ",IDA[i]);
		}
		printf("\n");
	}
}

/**
* @brief  ??   ???   ??
* @param  ?
* @return ?
*/
unsigned char ComReqA(void)
{
	unsigned char sak,status,i;
	uint8_t  temp[16];
	PcdConfig('A');
	if(PcdRequest(PICC_REQALL,CT)!=MI_OK) 
			return MI_ERR;
	uid_length = UID_4;	///////?????
	status = PcdAnticoll(PICC_ANTICOLL1,temp);//???
	if (status != MI_OK)
		return MI_ERR;
	status = PcdSelect(PICC_ANTICOLL1, temp, &sak);////??
	if (status != MI_OK)
		return MI_ERR;
	
	if(status == MI_OK && (sak & BIT2))
	{
		uid_length = UID_7;
		status = PcdAnticoll(PICC_ANTICOLL2,&temp[4]);//???
		if(status == MI_OK)
		{
			status = PcdSelect(PICC_ANTICOLL2, &temp[4], &sak);////??
		}
	}

	if (status == MI_OK) 
	{
		if(uid_length == UID_4)
		{
			for(i=0;i<uid_length;i++)
				IDA[i]=temp[i];
		}
		else
		{
			for(i=0;i<uid_length;i++)
				IDA[i]=temp[i+1];
		}
	}
	if(status != MI_OK)
		return MI_ERR;	
	return status;		
}

/**
* @brief ?????A/B??
* @param type:????
* @return ?
*/
void PcdConfig(unsigned char type)
{
	// CLR_NFC_RST;
  	// delay_us(500);
	// SET_NFC_RST;
    // delay_us(900);

	// 使用软复位
	WriteRawRC(CommandReg,PCD_RESETPHASE);
	delay_ms(3);


	WriteRawRC(GsNReg, 0xFF);	//????
	WriteRawRC(CWGsPReg, 0x3F);	// 
		// PcdAntennaOff();
		// delay_us(200);
		if ('A' == type)
		{
				ClearBitMask(Status2Reg, BIT3);
				ClearBitMask(ComIEnReg, BIT7); // ???
				WriteRawRC(ModeReg,0x3D);	// 11 // CRC seed:6363
				WriteRawRC(RxSelReg, 0x86);//RxWait
				WriteRawRC(RFCfgReg, 0x58); // 
				WriteRawRC(TxASKReg, 0x40);//15  //typeA
				WriteRawRC(TxModeReg, 0x00);//12 //Tx Framing A
				WriteRawRC(RxModeReg, 0x00);//13 //Rx framing A
				WriteRawRC(0x0C, 0x10);	//^_^
	
			//????
			{
					unsigned char backup;
					backup = ReadRawRC(0x37);
					WriteRawRC(0x37, 0x00);	
				{
					// ????????????
					WriteRawRC(0x37, 0x5E);
					WriteRawRC(0x26, 0x48);
					WriteRawRC(0x17, 0x88);
					WriteRawRC(0x29, 0x0F);//0x0F,0x12); //????	
					WriteRawRC(0x35, 0xED);
					WriteRawRC(0x3b, 0xA5);
					WriteRawRC(0x37, 0xAE);
					WriteRawRC(0x3b, 0x72);	
				}
				WriteRawRC(0x27, 0xf0);//RxWait
				WriteRawRC(0x28, 0x3f); // 
				WriteRawRC(0x37, backup);
			}		
		}
		else if ('B' == type)
		{
				WriteRawRC(Status2Reg, 0x00);	//?MFCrypto1On
				ClearBitMask(ComIEnReg, BIT7);// ???????
				WriteRawRC(ModeReg, 0x3F);	// CRC seed:FFFF
				WriteRawRC(RxSelReg, 0x85);	//RxWait
				WriteRawRC(RFCfgReg, 0x58);	//	
				//Tx
				WriteRawRC(GsNReg, 0xF8);	//????
				WriteRawRC(CWGsPReg, 0x3F);	// 
				WriteRawRC(ModGsPReg, 0x12);	//????
				//WriteRawRC(ModGsPReg, 0x0E);	//????
				WriteRawRC(AutoTestReg, 0x00);
				WriteRawRC(TxASKReg, 0x00);	// typeB
				WriteRawRC(TypeBReg, 0x13);
				WriteRawRC(TxModeReg, 0x83);	//Tx Framing B
				WriteRawRC(RxModeReg, 0x83);	//Rx framing B
				WriteRawRC(BitFramingReg, 0x00);	//TxLastBits=0
	
			//????
			{
					unsigned char backup;
					backup = ReadRawRC(0x37);
					WriteRawRC(0x37, 0x00);
				{	
						WriteRawRC(0x37, 0x5E);
						WriteRawRC(0x26, 0x48);
						WriteRawRC(0x17, 0x88);
						WriteRawRC(0x29, 0x12);
						WriteRawRC(0x35, 0xED);
						WriteRawRC(0x3b, 0xA5);
						WriteRawRC(0x37, 0xAE);
						WriteRawRC(0x3b, 0x72);
				}
				WriteRawRC(0x37, backup);
			}
		}
		PcdAntennaOn();
		// delay_us(10);
		delay_ms(3);
		
}

/**
* @brief ????
* @param ?
* @return ?
*/
void PcdAntennaOff(void)
{
  WriteRawRC(TxControlReg, ReadRawRC(TxControlReg) & (~0x03));
}

/**
* @brief ????,?????????????????1ms???
* @param ?
* @return ?
*/
void PcdAntennaOn()
{
	WriteRawRC(TxControlReg, ReadRawRC(TxControlReg) | 0x03); //Tx1RFEn=1 Tx2RFEn=1
}

/**
* @brief  ??
* @param  
					req_code[IN]:????
					0x52 = ?????????14443A????
					0x26 = ??????????
					pTagType[OUT]:??????
					0x4400 = Mifare_UltraLight
					0x0400 = Mifare_One(S50)
					0x0200 = Mifare_One(S70)
					0x0800 = Mifare_Pro(X)
					0x4403 = Mifare_DESFire
* @return ????MI_OK
*/
unsigned char PcdRequest(unsigned char req_code,unsigned char *pTagType)
{
   unsigned char  status; 
   unsigned char	i;
   unsigned int   unLen;
   unsigned char  ucComMF522Buf[MAXRLEN]; 

   ClearBitMask(Status2Reg,0x08);
   WriteRawRC(BitFramingReg,0x07);
   SetBitMask(TxControlReg,0x03);
	 i=ReadRawRC(0X37);
	 if(i == 0x12 || i == 0x15)
	 {
			delay_ms(2);
	 }
	
   ucComMF522Buf[0] = req_code;

   status = PcdComMF522(PCD_TRANSCEIVE,ucComMF522Buf,1,ucComMF522Buf,&unLen);
   if ((status == MI_OK) && (unLen == 0x10))
   {    
       *pTagType     = ucComMF522Buf[0];
       *(pTagType+1) = ucComMF522Buf[1];
   }
   else
   {   status = MI_ERR;   }
   
   return status;
}
/**
* @brief  ???
* @param  pSnr[OUT]:?????,4??
* @return ????MI_OK
*/
unsigned char PcdAnticoll(uint8_t mode, uint8_t *pSnr) 
{
    char  status;
    unsigned char i,snr_check=0;
    unsigned int  unLen;
    unsigned char ucComMF522Buf[MAXRLEN]; 
    
    ClearBitMask(Status2Reg,0x08);
    WriteRawRC(BitFramingReg,0x00);
    ClearBitMask(CollReg,0x80);
    ucComMF522Buf[0] = mode;
    ucComMF522Buf[1] = 0x20;
    status = PcdComMF522(PCD_TRANSCEIVE,ucComMF522Buf,2,ucComMF522Buf,&unLen);
    if (status == MI_OK)
    {
    	 for (i=0; i<4; i++)
         {   
             *(pSnr+i)  = ucComMF522Buf[i];
             snr_check ^= ucComMF522Buf[i];
         }
         if (snr_check != ucComMF522Buf[i])
         {   status = MI_ERR;    }
    }
    
    SetBitMask(CollReg,0x80);
    return status;
}

/**
* @brief  ????
* @param  pSnr[IN]:?????,4??
* @return ????MI_OK
*/
unsigned char PcdSelect(uint8_t Mode,uint8_t *pSnr,unsigned char *SAK)
{
    char  status;
    unsigned char  i;
    unsigned int   unLen;
    unsigned char  ucComMF522Buf[MAXRLEN]; 
    
    ucComMF522Buf[0] = Mode;
    ucComMF522Buf[1] = 0x70;
    ucComMF522Buf[6] = 0;
    for (i=0; i<4; i++)
    {
    	ucComMF522Buf[i+2] = *(pSnr+i);
    	ucComMF522Buf[6]  ^= *(pSnr+i);
    }
    CalulateCRC(ucComMF522Buf,7,&ucComMF522Buf[7]);
  
    ClearBitMask(Status2Reg,0x08);

    status = PcdComMF522(PCD_TRANSCEIVE,ucComMF522Buf,9,ucComMF522Buf,&unLen);
    
    if ((status == MI_OK) && (unLen == 0x18))
    {   
      SAK[0]=ucComMF522Buf[0];
      status = MI_OK;  
    
    }
    else
    {   status = MI_ERR;    }

    return status;
}

/**
* @brief  ?MF522??CRC16??
* @param  
* @return 
*/
void CalulateCRC(unsigned char *pIndata,unsigned char len,unsigned char *pOutData)
{
    unsigned char  i,n;
    ClearBitMask(DivIrqReg,0x04);
    WriteRawRC(CommandReg,PCD_IDLE);
    SetBitMask(FIFOLevelReg,0x80);
    for (i=0; i<len; i++)
    {   WriteRawRC(FIFODataReg, *(pIndata+i));   }
    WriteRawRC(CommandReg, PCD_CALCCRC);
    i = 0xFF;
    do 
    {
        n = ReadRawRC(DivIrqReg);
        i--;
    }
    while ((i!=0) && !(n&0x04));
    pOutData[0] = ReadRawRC(CRCResultRegL);
    pOutData[1] = ReadRawRC(CRCResultRegM);
}
/**
* @brief  ?ISO14443???
* @param  Command[IN]:???
					pInData[IN]:????????
					InLenByte[IN]:?????????
					pOutData[OUT]:??????????
					*pOutLenBit[OUT]:????????
* @return status:???????
*/
unsigned char PcdComMF522(signed char Command,
								unsigned char *pInData,
								unsigned char InLenByte,
								unsigned char *pOutData,
								unsigned int *pOutLenBit)
{
	unsigned char  recebyte=0;
	unsigned char  val;
	unsigned char  err=0;	
	unsigned char  irq_inv;
	unsigned char  len_rest=0;
	unsigned char  len=0;
	unsigned char  status;
	unsigned char  irqEn   = 0x00;
	unsigned char  waitFor = 0x00;
	unsigned char  lastBits;
	uint16_t i;

	switch (Command)
	{
	  case PCD_IDLE:
	     irqEn   = 0x00;
	     waitFor = 0x00;
	     break;
	  case PCD_AUTHENT:    
		irqEn = IdleIEn | TimerIEn;
		waitFor = IdleIRq;
		break;
	  case PCD_RECEIVE:
	     irqEn   = RxIEn | IdleIEn;
	     waitFor = RxIRq;
	     recebyte=1;
	     break;
	  case PCD_TRANSMIT:
	     irqEn   = TxIEn | IdleIEn;
	     waitFor = TxIRq;
	     break;
	  case PCD_TRANSCEIVE:   
		 irqEn = RxIEn | IdleIEn | TimerIEn | TxIEn;
	     waitFor = RxIRq;
	     recebyte=1;
//UART0_write(irqEn);
	     break;
	  default:
	     Command = MI_UNKNOWN_COMMAND;
	     break;
	}
   
	if (Command != MI_UNKNOWN_COMMAND
		&& (((Command == PCD_TRANSCEIVE || Command == PCD_TRANSMIT) && InLenByte > 0)
		|| (Command != PCD_TRANSCEIVE && Command != PCD_TRANSMIT))
		)
	{		
		WriteRawRC(CommandReg, PCD_IDLE);
		
		irq_inv = ReadRawRC(ComIEnReg) & BIT7;
		WriteRawRC(ComIEnReg, irq_inv |irqEn | BIT0);//??Timer ?????
		WriteRawRC(ComIrqReg, 0x7F); //Clear INT
		WriteRawRC(DivIrqReg, 0x7F); //Clear INT
		//Flush Fifo
		SetBitMask(FIFOLevelReg, BIT7);
		if (Command == PCD_TRANSCEIVE || Command == PCD_TRANSMIT || Command == PCD_AUTHENT)
		{
			len_rest = InLenByte;
			if (len_rest >= FIFO_SIZE)
			{
				len = FIFO_SIZE;
			}else
			{
				len = len_rest;
			}		
			for (i = 0; i < len; i++)
			{
				WriteRawRC(FIFODataReg, pInData[i]);
			}
			len_rest -= len;//Rest bytes
			if (len_rest != 0)
			{
				WriteRawRC(ComIrqReg, BIT2); // clear LoAlertIRq
				SetBitMask(ComIEnReg, BIT2);// enable LoAlertIRq
			}

			WriteRawRC(CommandReg, Command);
			if (Command == PCD_TRANSCEIVE)
		    {    
				SetBitMask(BitFramingReg,0x80);  
			}
	
			while (len_rest != 0)
			{
			  	delay_ms(2);		
				if (len_rest > (FIFO_SIZE - WATER_LEVEL))
				{
					len = FIFO_SIZE - WATER_LEVEL;
				}
				else
				{
					len = len_rest;
				}
				for (i = 0; i < len; i++)
				{
					WriteRawRC(FIFODataReg, pInData[InLenByte - len_rest + i]);
				}

				WriteRawRC(ComIrqReg, BIT2);//?write fifo??,??????????
				len_rest -= len;//Rest bytes
				if (len_rest == 0)
				{
					ClearBitMask(ComIEnReg, BIT2);// disable LoAlertIRq
				}	
			}
			//Wait TxIRq
			delay_ms(2);
                        //while (INT_PIN == 0);
			val = ReadRawRC(ComIrqReg);
			if (val & TxIRq)
			{
				WriteRawRC(ComIrqReg, TxIRq);
			}
		}
		if (PCD_RECEIVE == Command)
		{	
			SetBitMask(ControlReg, BIT6);// TStartNow
		}
	
		len_rest = 0; // bytes received
		WriteRawRC(ComIrqReg, BIT3); // clear HoAlertIRq
		SetBitMask(ComIEnReg, BIT3); // enable HoAlertIRq
	
		delay_ms(2);
	
		while(1)
		{
      delay_ms(2);
			val = ReadRawRC(ComIrqReg);
				
			if ((val & BIT3) && !(val & BIT5))
			{
				if (len_rest + FIFO_SIZE - WATER_LEVEL > 255)
				{
					break;
				}
		    for (i = 0; i <FIFO_SIZE - WATER_LEVEL; i++)
		    {
					pOutData[len_rest + i] = ReadRawRC(FIFODataReg);
		    }
				WriteRawRC(ComIrqReg, BIT3);//?read fifo??,??????????
				len_rest += FIFO_SIZE - WATER_LEVEL; 
			}
			else
			{
				ClearBitMask(ComIEnReg, BIT3);//disable HoAlertIRq
				break;
			}			
		}

		val = ReadRawRC(ComIrqReg);

		
		
		WriteRawRC(ComIrqReg, val);// ???
		//val = ReadRawRC(ComIrqReg);
		
		if (val & BIT0)
		{//????
			status = MI_NOTAGERR;
//			UART0_write(0X22);
		}
		else			//20190911??
		{
//			UART0_write(0X11);
//			err = ReadRawRC(ErrorReg);
			
			status = MI_COM_ERR;
			if ((val & waitFor) && (val & irqEn))
			{
				if (!(val & ErrIRq))
				 {//??????
				    status = MI_OK;

				    if (recebyte)
				    {
						val = 0x7F & ReadRawRC(FIFOLevelReg);
				      	lastBits = ReadRawRC(ControlReg) & 0x07;
						if (len_rest + val > MAX_TRX_BUF_SIZE)
						{//????????
							status = MI_COM_ERR;
						}
						else
						{	
							if (lastBits && val) //??spi??? val-1????
							{
								*pOutLenBit = (val-1)*8 + lastBits;
							}
							else
							{
								*pOutLenBit = val*8;
							}
							*pOutLenBit += len_rest*8;


							if (val == 0)
							{
								val = 1;
							}
							for (i = 0; i < val; i++)
							{
								pOutData[len_rest + i] = ReadRawRC(FIFODataReg);
							}					
						}
				   }
				 }					
				 else if ((err & CollErr) && (!(ReadRawRC(CollReg) & BIT5)))
				 {//a bit-collision is detected				 	
				    status = MI_COLLERR;
				    if (recebyte)
				    {
								val = 0x7F & ReadRawRC(FIFOLevelReg);
				      	lastBits = ReadRawRC(ControlReg) & 0x07;
						if (len_rest + val > MAX_TRX_BUF_SIZE)
						{//????????
							;
						}
						else
						{
					     if (lastBits && val) //??spi??? val-1????
					     {
					        *pOutLenBit = (val-1)*8 + lastBits;
					     }
					     else
					     {
					        *pOutLenBit = val*8;
					     }		
							*pOutLenBit += len_rest*8;
					     if (val == 0)
					     {
					        val = 1;
					     }
							for (i = 0; i < val; i++)
					    {
								pOutData[len_rest + i +1] = ReadRawRC(FIFODataReg);				
					    }				
						}
				    }
					pOutData[0] = (ReadRawRC(CollReg) & CollPos);
					if (pOutData[0] == 0)
					{
						pOutData[0] = 32;
					}
				
					pOutData[0]--;// ???????????,?????????,??????;

				}
				else if ((err & CollErr) && (ReadRawRC(CollReg) & BIT5))
				{
					;		
				}
				//else if (err & (CrcErr | ParityErr | ProtocolErr))
				else if (err & (ProtocolErr))
				{
					status = MI_FRAMINGERR;				
				}
				else if ((err & (CrcErr | ParityErr)) && !(err &ProtocolErr))
				{
					//EMV  parity err EMV 307.2.3.4		
					val = 0x7F & ReadRawRC(FIFOLevelReg);
			      	lastBits = ReadRawRC(ControlReg) & 0x07;
					if (len_rest + val > MAX_TRX_BUF_SIZE)
					{//????????
						status = MI_COM_ERR;
					}
					else
					{
				        if (lastBits && val)
				        {
				           *pOutLenBit = (val-1)*8 + lastBits;
				        }
				        else
				        {
				           *pOutLenBit = val*8;
				        }
						*pOutLenBit += len_rest*8;
					}
					status = MI_INTEGRITY_ERR;
				}				
				else
				{
					status = MI_INTEGRITY_ERR;
				}
			}
			else
			{   
				status = MI_COM_ERR;
			}
		}	
 		SetBitMask(ControlReg, BIT7);// TStopNow =1,???;
		WriteRawRC(ComIrqReg, 0x7F);// ???0
		WriteRawRC(DivIrqReg, 0x7F);// ???1
		ClearBitMask(ComIEnReg, 0x7F);//?????,???????
		ClearBitMask(DivIEnReg, 0x7F);//?????,???????
		WriteRawRC(CommandReg, PCD_IDLE);
	}
	else
	{
		status = USER_ERROR;
	}
	return status;
}

void SetLPCDMode(void)////LPCD??
{
	printf("退出LPCD模式\n");
	CLR_NFC_RST;
	delay_us(500);	
	SET_NFC_RST;
	delay_us(500);
	pcd_lpcd_application();
	CLR_NFC_RST;
	delay_us(500);
	SET_NFC_RST;
	delay_us(500); 
	printf("进入LPCD模式\n");
	pcd_lpcd_start(SENSITIVITY,PROBE);//??LPCD??
	return;
}

void pcd_lpcd_start(unsigned char delta,unsigned char swingscnt)
{
	WriteRawRC(0x01,0x0F); 	//
	WriteRawRC(0x14, 0x23);	// Tx2CW = 1 ,使能TX1，TX2的13.56MHz的能量载波信号
	WriteRawRC(0x37, 0x5e);	//  
	WriteRawRC(0x3c,0X30|delta);//Delta[3:0]
	WriteRawRC(0x3d, 0x0d);	//休眠时间	100MS
	WriteRawRC(0x3e, 0x90|swingscnt);//探测时间
	WriteRawRC(0x37, 0x00);	// 关闭私有寄存器保护开关

	WriteRawRC(0x37, 0x5a);//打开私有寄存器保护开关
	WriteRawRC(0x38, 0x30);//设置LPCD 发射功率
	WriteRawRC(0x39, 0x1c);//设置LPCD 发射功率
	WriteRawRC(0x31, 0xA1);//设置LPCD 检测参考值
	WriteRawRC(0x33, 0x60);//调整步长,20,60,A0,E0
	WriteRawRC(0x36, 0x80);
	WriteRawRC(0x37, 0x00);//关闭私有寄存器保护开关

	ClearBitMask(0x02, 0x80); //配置IRQ为高电平中断
//  WriteRawRC(0x02, 0x00);//配置为低电平中断   与Status1Reg的IRq位相反
	WriteRawRC(0x03, 0xA0);	//打开卡探测中断,IRQ 为CMOS 输出
	WriteRawRC(0x01, 0x10);	//PCD soft powerdown	

}


void pcd_lpcd_application(void)
{
	Card_Check();		
}

//******WS1850结束LPCD模式*******************
void pcd_lpcd_end(void)
{
	WriteRawRC(0x01,0x0f); //?????????lpcd
}
