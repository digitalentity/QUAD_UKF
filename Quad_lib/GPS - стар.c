

#include "stm32f4xx.h"
#include "stm32f4xx_syscfg.h"
#include "Config.h"

#include "GPS.h"


#include "MPU6500.h"
#include "IMU_INS.h"
#include "I2C_BARO_MAG.h"
#include "USART_TELEMETRY.h"
#include "PWM_PPM.h"
#include "UKF_lib.h"

//��� �������� ������� � ��������, � ������ ��������� ��� �� � ������
#define GPS_FRAME_POS_X		(float)(-10.5f*0.01f)
#define GPS_FRAME_POS_Y		(float)(11.0f*0.01f)
#define GPS_FRAME_RADIUS	(float)(sqrtf(GPS_FRAME_POS_X*GPS_FRAME_POS_X+GPS_FRAME_POS_Y*GPS_FRAME_POS_Y))

//#define USE_GPS_FRAME_POS_CMPNST
//#define USE_GPS_FRAME_VEL_CMPNST
//***************************************************************************************************//

//������ ��� ��������� ���
const uint8_t command_ubx_38400[]=		{0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x91, 0x84};
const uint8_t command_ubx_answ[]=		{0xB5, 0x62, 0x06, 0x00, 0x01, 0x00, 0x01, 0x08, 0x22};
const uint8_t command_nav_pvt[]=		{0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0x01, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x18, 0xE1};
const uint8_t command_nav_answ[]=		{0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x00, 0x01, 0x07, 0x11, 0x3A};
const uint8_t command_5hz[]=			{0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xC8, 0x00, 0x01, 0x00, 0x01, 0x00, 0xDE, 0x6A};
const uint8_t command_5hz_answ[]=		{0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};
const uint8_t command_nav_dop[]=		{0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0x01, 0x04, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x19, 0xE0};
const uint8_t command_nav_dop_answ[]=	{0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x00, 0x01, 0x04, 0x0F, 0x45};
uint8_t chksumA, chksumB;
//***************************************************************************************************//

//����������� �����
uint8_t Recieve_buf_GPS[200];
uint16_t Recieve_W_GPS = 0;
//****************************************************//
//��� ��
TaskHandle_t gps_handle;
uint32_t ovf_gps_stack;
SemaphoreHandle_t xGPS_Semaphore = NULL;
//****************************************************//



//�������� �����
uint32_t cnt_cycle_gps, cnt_ticks_gps;
//****************************************************//
//������������ ��������� ������, ����� ��������� ������
uint32_t GPS_Micros_Update;
uint8_t flag_start_home = 0, flag_get_pvt = 0;
//****************************************************//
//��������������� ������
uint16_t GPS_year;
uint8_t GPS_month, GPS_day, GPS_hours, GPS_minutes, GPS_seconds;
uint8_t GPS_satellits;
uint8_t GPS_valid_invalid;
uint8_t GPS_fix_type;
//****************************************************//
//������ �� ��� ��������, ������ ��� ���������
float GPS_LA, GPS_LO;
int32_t GPS_lon, GPS_lat;
uint32_t GPS_hAcc, GPS_vAcc, GPS_sAcc;
uint16_t GPS_vDop, GPS_nDop, GPS_eDop, GPS_tDop;
//****************************************************//


//*******************������� ������ ��� ��������*******************************//
//������� � �, ������������ ������
float GPS_X, GPS_Y;
float GPS_alt;
//��������, ����
float GPS_Y_speed, GPS_X_speed, GPS_Z_speed;
float GPS_vel, GPS_course;
//���������� ��������
float GPS_hAccf = 99.99f, GPS_vAccf = 99.99f, GPS_sAccf = 99.99f;
float GPS_pDopf = 99.99f, GPS_eDopf = 99.99f, GPS_vDopf = 99.99f, GPS_nDopf = 99.99f, GPS_tDopf = 99.99f;
//****************************************************//

//������ ��� ������� ����������� ��������� ���
float k_rot_gps_frame = 1.0f;
float x_gps_ppp, y_gps_ppp;
float x_gps_frame, y_gps_frame;
float x_gps_local, y_gps_local, x_gps_start, y_gps_start;
//****************************************************//



static void ubloxTxChecksum(uint8_t c) {
	chksumA += c;
	chksumB += chksumA;
}
static void ubloxWriteU1(unsigned char c) {
    while(!(UART_GPS->SR & USART_SR_TC));
    USART_SendData(UART_GPS, c);
    ubloxTxChecksum(c);
}
void GPS_INIT(void)
{
	uint8_t cnt_wr=0;
	uint32_t cnt_delay;
	USART_InitTypeDef USART_Init_GPS;

	for (cnt_delay = 0; cnt_delay < 336000; cnt_delay++); cnt_delay = 0;

	for (cnt_wr=0; cnt_wr<sizeof(command_ubx_38400);cnt_wr++)
	    {
		while(!(UART_GPS->SR & USART_SR_TC));
		UART_GPS->DR = command_ubx_38400[cnt_wr];
	    }

	for (cnt_delay = 0; cnt_delay < 336000; cnt_delay++); cnt_delay = 0;

	for (cnt_wr=0; cnt_wr<sizeof(command_ubx_answ);cnt_wr++)
	    {
		while(!(UART_GPS->SR & USART_SR_TC));
		USART_SendData(UART_GPS, command_ubx_answ[cnt_wr]);
	    }

	USART_Init_GPS.USART_BaudRate = 38400;
	USART_Init_GPS.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_Init_GPS.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init_GPS.USART_Parity = USART_Parity_No;
	USART_Init_GPS.USART_StopBits = USART_StopBits_1;
	USART_Init_GPS.USART_WordLength = USART_WordLength_8b;
	USART_Init(UART_GPS, &USART_Init_GPS);

	for (cnt_delay = 0; cnt_delay < 40000; cnt_delay++); cnt_delay = 0;

	/////////////////////////////////////////////////////////////////
	while(!(UART_GPS->SR & USART_SR_TC));
	USART_SendData(UART_GPS, 0xB5);
	while(!(UART_GPS->SR & USART_SR_TC));
	USART_SendData(UART_GPS, 0x62);
	ubloxWriteU1(0x06);
	ubloxWriteU1(0x24);

	ubloxWriteU1(0x24);                 // length lsb
	ubloxWriteU1(0x00);                 // length msb

	ubloxWriteU1(0b0000101);            // mask LSB (fixMode, dyn)
	ubloxWriteU1(0x00);                 // mask MSB (reserved)
	ubloxWriteU1(0x06);                 // dynModel (6 == airborne < 1g, 8 == airborne < 4g)
	ubloxWriteU1(0x02);                 // fixMode (2 == 3D only)

	// the rest of the packet is ignored due to the above mask
	uint8_t i;
	for (i = 0; i < 32; i++)
	    ubloxWriteU1(0x00);

	while(!(UART_GPS->SR & USART_SR_TC));
	USART_SendData(UART_GPS, chksumA);
	while(!(UART_GPS->SR & USART_SR_TC));
	USART_SendData(UART_GPS, chksumB);


	//////////////////////////////////////////////////////////////////
	for (cnt_delay = 0; cnt_delay < 40000; cnt_delay++); cnt_delay = 0;


	for (cnt_wr=0; cnt_wr<sizeof(command_5hz);cnt_wr++)
	    {
		while(!(UART_GPS->SR & USART_SR_TC));
		USART_SendData(UART_GPS, command_5hz[cnt_wr]);
	    }
	for (cnt_delay = 0; cnt_delay < 300000; cnt_delay++); cnt_delay = 0;

	for (cnt_wr=0; cnt_wr<sizeof(command_5hz_answ);cnt_wr++)
	    {
		while(!(UART_GPS->SR & USART_SR_TC));
		USART_SendData(UART_GPS, command_5hz_answ[cnt_wr]);
	    }
	for (cnt_delay = 0; cnt_delay < 536000; cnt_delay++); cnt_delay = 0;



	///////////////////////////////////////////////////
		for (cnt_wr=0; cnt_wr<sizeof(command_nav_dop);cnt_wr++)
		    {
			while(!(UART_GPS->SR & USART_SR_TC));
			USART_SendData(UART_GPS, command_nav_dop[cnt_wr]);
		    }
		for (cnt_delay = 0; cnt_delay < 536000; cnt_delay++); cnt_delay = 0;

		for (cnt_wr=0; cnt_wr<sizeof(command_nav_dop_answ);cnt_wr++)
		    {
			while(!(UART_GPS->SR & USART_SR_TC));
			USART_SendData(UART_GPS, command_nav_dop_answ[cnt_wr]);
		    }
		for (cnt_delay = 0; cnt_delay < 536000; cnt_delay++); cnt_delay = 0;
	////////////////////////////////////////////////////


	for (cnt_wr=0; cnt_wr<sizeof(command_nav_pvt);cnt_wr++)
	    {
		while(!(UART_GPS->SR & USART_SR_TC));
		USART_SendData(UART_GPS, command_nav_pvt[cnt_wr]);
	    }

	for (cnt_delay = 0; cnt_delay < 336000; cnt_delay++); cnt_delay = 0;

//	for (cnt_wr=0; cnt_wr<sizeof(command_nav_answ);cnt_wr++)
//	    {
//		while(!(UART_GPS->SR & USART_SR_TC));
//		USART_SendData(UART_GPS, command_nav_answ[cnt_wr]);
//	    }
}
void USART_INIT_GPS(void)
{
	vSemaphoreCreateBinary(xGPS_Semaphore);
	xTaskCreate(prvGPS_Processing,(signed char*)"GPS", 150, NULL, PRIORITET_TASK_GPS, gps_handle);

	//............................................................//
	GPIO_InitTypeDef GPIO_Init_USART_GPS;
	USART_InitTypeDef USART_Init_GPS;

	//RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	UART_GPS_RCC_Periph_Pin();

	GPIO_Init_USART_GPS.GPIO_Pin =	UART_GPS_Pin_TX|UART_GPS_Pin_RX;
	GPIO_Init_USART_GPS.GPIO_Mode = GPIO_Mode_AF;
	GPIO_Init_USART_GPS.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init_USART_GPS.GPIO_OType = GPIO_OType_PP;
	GPIO_Init_USART_GPS.GPIO_PuPd = GPIO_PuPd_UP;

	GPIO_Init(UART_GPS_GPIO, &GPIO_Init_USART_GPS);

	GPIO_PinAFConfig(UART_GPS_GPIO, UART_GPS_Pin_Soucre_TX, UART_GPS_Pin_AF);
	GPIO_PinAFConfig(UART_GPS_GPIO, UART_GPS_Pin_Soucre_RX, UART_GPS_Pin_AF);
	//...........................................................//

	//RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	UART_GPS_RCC_Periph();

	USART_Init_GPS.USART_BaudRate = 9600;
	USART_Init_GPS.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_Init_GPS.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init_GPS.USART_Parity = USART_Parity_No;
	USART_Init_GPS.USART_StopBits = USART_StopBits_1;
	USART_Init_GPS.USART_WordLength = USART_WordLength_8b;

	USART_Init(UART_GPS, &USART_Init_GPS);
	USART_Cmd(UART_GPS, ENABLE);

	GPS_INIT();

	NVIC_EnableIRQ(UART_GPS_IRQ);

	NVIC_SetPriority(UART_GPS_IRQ, PRIORITET_UART_GPS);//����� ������ � ���

	USART_ITConfig(UART_GPS, USART_IT_RXNE, ENABLE);

}

void USART1_IRQHandler(void)
{
	static portBASE_TYPE xHigherPriorityTaskWoken_GPS = pdFALSE;
	static uint8_t start;

	if(USART_GetITStatus(UART_GPS, USART_IT_ORE_RX) == SET)
	    {
		USART_GetFlagStatus(UART_GPS, USART_IT_ORE_RX);
		USART_ReceiveData(UART_GPS);
		Recieve_W_GPS = 0;
		start = 0;
	    }

	if (USART_GetITStatus(UART_GPS, USART_IT_RXNE) == SET)
	{
		USART_ClearITPendingBit(UART_GPS, USART_IT_RXNE);
		Recieve_buf_GPS[Recieve_W_GPS] = USART_ReceiveData(UART_GPS);

		if((Recieve_W_GPS >= 5)&&(start==0))
		    {
			if((Recieve_buf_GPS[Recieve_W_GPS - 5]==0xB5)&&(Recieve_buf_GPS[Recieve_W_GPS - 4]==0x62)&&(Recieve_buf_GPS[Recieve_W_GPS-3]==0x01)&&(Recieve_buf_GPS[Recieve_W_GPS-2]==0x07)&&(Recieve_buf_GPS[Recieve_W_GPS-1]==0x5C)&&(Recieve_buf_GPS[Recieve_W_GPS]==0x00))
			    {
				start=1;
				//Recieve_W_GPS = 0;
				return;
			    }
			if((Recieve_buf_GPS[Recieve_W_GPS - 5]==0xB5)&&(Recieve_buf_GPS[Recieve_W_GPS - 4]==0x62)&&(Recieve_buf_GPS[Recieve_W_GPS-3]==0x01)&&(Recieve_buf_GPS[Recieve_W_GPS-2]==0x04)&&(Recieve_buf_GPS[Recieve_W_GPS-1]==0x12)&&(Recieve_buf_GPS[Recieve_W_GPS]==0x00))
			    {
				start=2;
				//Recieve_W_GPS = 0;
				return;
			    }
		    }
		if (start == 1)
		    {
			//������� ����������� �����
			if (Recieve_W_GPS == 98)
			    {
				start = 0;
				Recieve_W_GPS = 0;
				//�������
				xSemaphoreGiveFromISR(xGPS_Semaphore, &xHigherPriorityTaskWoken_GPS);
			        //�������������� ������������ ��������� ��� ���������� ������� ������� �� ����������
			        if( xHigherPriorityTaskWoken_GPS != pdFALSE )
			        {
			        	taskYIELD();
			        }
			        return;
			    }
		    }
		if (start == 2)
		    {
			//������� ����������� �����
			if (Recieve_W_GPS == 24)
			    {
				start = 0;
				Recieve_W_GPS = 0;
				//�������
				xSemaphoreGiveFromISR(xGPS_Semaphore, &xHigherPriorityTaskWoken_GPS);
			        //�������������� ������������ ��������� ��� ���������� ������� ������� �� ����������
			        if( xHigherPriorityTaskWoken_GPS != pdFALSE )
			        {
			        	taskYIELD();
			        }
			        return;
			    }
		    }
		if (Recieve_W_GPS < 200)Recieve_W_GPS++;

	}


}

static void navUkfCalcEarthRadius(double lat, float *k_x, float *k_y) {
    double sinLat2;

    sinLat2 = sin(lat * (double)DEG2RAD);
    sinLat2 = sinLat2 * sinLat2;

    *k_y = (double)NAV_EQUATORIAL_RADIUS * (double)DEG2RAD * ((double)1.0 - (double)NAV_E_2) / pow((double)1.0 - ((double)NAV_E_2 * sinLat2), ((double)3.0 / (double)2.0));
    *k_x = (double)NAV_EQUATORIAL_RADIUS * (double)DEG2RAD / sqrt((double)1.0 - ((double)NAV_E_2 * sinLat2)) * cos(lat * (double)DEG2RAD);
    *k_y /=10000000.0f;
    *k_x /=10000000.0f;
}
void pvt (char *buf)
{
	uint8_t cnt;
	cnt = 4;
	static float k_x, KX, KY;
	static int32_t GPS_lon_start, GPS_lat_start, GPS_height_start;
	int32_t  GPS_height, GPS_V_N, GPS_V_E, GPS_V_D, GPS_V_G, GPS_head;
	uint8_t GPS_flags;
	uint8_t GPS_NS;
	uint8_t GPS_EW;

	uint16_t GPS_pDop;




	GPS_year = buf[cnt]|(buf[cnt+1]<<8);cnt+=2;
	GPS_month = buf[cnt];cnt++;
	GPS_day = buf[cnt];cnt++;

	GPS_hours = buf[cnt];cnt++;
	GPS_minutes = buf[cnt]; cnt++;
	GPS_seconds = buf[cnt];cnt++;
	cnt++;
	cnt+=8;
	GPS_fix_type = buf[cnt];cnt++;
	cnt++;
	cnt++;
	GPS_satellits = buf[cnt];cnt++;
	GPS_lon = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;
	GPS_lat = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;
	cnt+=4;
	GPS_height = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;

	GPS_hAcc = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;
	GPS_vAcc = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;

	GPS_V_N = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;
	GPS_V_E = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;
	GPS_V_D = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;
	GPS_V_G = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;

	GPS_head = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;

	GPS_sAcc = buf[cnt]|(buf[cnt+1]<<8)|(buf[cnt+2]<<16)|(buf[cnt+3]<<24);cnt+=4;

	cnt+=4;

	GPS_pDop = buf[cnt]|(buf[cnt+1]<<8);
	//������� � ��
	GPS_pDopf = (float)GPS_pDop/100.0f;
	GPS_course = (float)GPS_head/100000.0f;
	GPS_LA = ((float)(GPS_lat))/10000000.0f;
	GPS_LO = ((float)(GPS_lon))/10000000.0f;
	GPS_Y_speed = ((float)GPS_V_N)*0.001f;
	GPS_X_speed = ((float)GPS_V_E)*0.001f;
	GPS_Z_speed = -((float)GPS_V_D)*0.001f;
	GPS_vel = ((float)GPS_V_G)*0.001f;

	#ifdef USE_GPS_FRAME_VEL_CMPNST
	float sn, cn;
	sn = sinf(axisz);
	cn = cosf(axisz);


	GPS_X_speed -= -GPS_FRAME_RADIUS*g_mix_z*cn;
	GPS_Y_speed -= -GPS_FRAME_RADIUS*g_mix_z*sn;
	#endif
	GPS_hAccf = (float)GPS_hAcc/1000.0f;
	GPS_vAccf = (float)GPS_vAcc/1000.0f;
	GPS_sAccf = (float)GPS_sAcc/1000.0f;


	if ((GPS_satellits > 8)&&(GPS_pDop<175))
	    {
		if (flag_start_home == 0)
		    {
			double LAT;
			flag_start_home = 1;
			GPS_height_start = GPS_height;
			GPS_lon_start = GPS_lon;
			GPS_lat_start = GPS_lat;
			LAT = (double)GPS_lat/10000000.0f;
			navUkfCalcEarthRadius(LAT, &KX, &KY);
			//k_x=(float)cos(((double)(GPS_lat))/10000000.0f*DEG2RAD);

			#ifdef USE_GPS_FRAME_POS_CMPNST
			x_gps_start = ((float)(GPS_lon-GPS_lon_start))*KX;
			y_gps_start = ((float)(GPS_lat-GPS_lat_start))*KY;

			x_gps_frame = GPS_FRAME_POS_X*cn - k_rot_gps_frame*GPS_FRAME_POS_Y*sn;
			y_gps_frame = GPS_FRAME_POS_Y*cn + k_rot_gps_frame*GPS_FRAME_POS_X*sn;

			x_gps_start -= x_gps_frame;
			y_gps_start -= y_gps_frame;
			#endif

		    }
#ifdef USE_GPS_FRAME_POS_CMPNST
		y_gps_local = ((float)(GPS_lat-GPS_lat_start))*KY - y_gps_start;
		x_gps_local = ((float)(GPS_lon-GPS_lon_start))*KX - x_gps_start;

		y_gps_ppp = ((float)(GPS_lat-GPS_lat_start))*KY;
		x_gps_ppp = ((float)(GPS_lon-GPS_lon_start))*KX;


		x_gps_frame = GPS_FRAME_POS_X*cn - GPS_FRAME_POS_Y*sn;
		y_gps_frame = GPS_FRAME_POS_Y*cn + GPS_FRAME_POS_X*sn;

		GPS_X = x_gps_local - x_gps_frame;
		GPS_Y = y_gps_local - y_gps_frame;
#else
//		GPS_X = ((float)(GPS_lon-GPS_lon_start))*K_Y_GPS*k_x;
//		GPS_Y = ((float)(GPS_lat-GPS_lat_start))*K_Y_GPS;
		GPS_X = ((float)(GPS_lon-GPS_lon_start))*KX;
		GPS_Y = ((float)(GPS_lat-GPS_lat_start))*KY;
#endif
		GPS_alt = ((float)(GPS_height-GPS_height_start))*0.001f;

		flag_get_pvt = 1;
	    }

	xSemaphoreGive(xSD_GPS_collect_Semaphore);

	GPS_Micros_Update = timerMicros() - GPS_LATENCY;

	cnt_cycle_gps = (uint32_t)DWT->CYCCNT - cnt_ticks_gps;
	cnt_ticks_gps = (uint32_t)DWT->CYCCNT;
}
void dop (char *buf)
{
	uint8_t cnt = 8;



	GPS_tDop = buf[cnt]|(buf[cnt+1]<<8);cnt+=2;
	GPS_vDop = buf[cnt]|(buf[cnt+1]<<8);cnt+=4;
	GPS_nDop = buf[cnt]|(buf[cnt+1]<<8);cnt+=2;
	GPS_eDop = buf[cnt]|(buf[cnt+1]<<8);

	GPS_tDopf = (float)GPS_tDop/100.0f;
	GPS_vDopf = (float)GPS_vDop/100.0f;
	GPS_nDopf = (float)GPS_nDop/100.0f;
	GPS_eDopf = (float)GPS_eDop/100.0f;



}
void obr_UBX(char *buf)
    {
	if((buf[0]==0xB5)&&(buf[1]==0x62)&&(buf[2]==0x01)&&(buf[3]==0x07)&&(buf[4]==0x5C))
		pvt(&buf[5]);
	if((buf[0]==0xB5)&&(buf[1]==0x62)&&(buf[2]==0x01)&&(buf[3]==0x04)&&(buf[4]==0x12))
		dop(&buf[5]);

    }
void prvGPS_Processing(void *pvParameters)
    {
	while(1)
	    {
		xSemaphoreTake(xGPS_Semaphore, portMAX_DELAY);
		obr_UBX(Recieve_buf_GPS);
		ovf_gps_stack = uxTaskGetStackHighWaterMark(gps_handle);
	    }
    }
