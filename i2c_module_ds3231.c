/*
Copyright (C) 2015  Marcel Langner (langner.marcel@myiq.de)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef I2C_MODULE_DS3231_C
#define I2C_MODULE_DS3231_C
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "clock.h"

unsigned char DS3231_messageBuf[25];

volatile unsigned char DS3231_cmd=0;
volatile unsigned char DS3231_state=0;
#define DS3231_get_time 1
#define DS3231_set_time 2
#define DS3231_get_temp 3
#define DS3231_done 200
#define DS3231_error 230
volatile unsigned char DS3231_cmd;


unsigned char DS3231_detect(void){
	unsigned int i=0;
	//unsigned char messageBuf[25];

	DS3231_messageBuf[0]=(0x68<<TWI_ADR_BITS) | (FALSE<<TWI_READ_BIT);
	DS3231_messageBuf[1]=0x0F;
	DS3231_messageBuf[2]=0x00;
	TWI_Start_Transceiver_With_Data( DS3231_messageBuf, 3 );
	do{sleep_mode();i++;if(i==0){break;}}while( TWI_Transceiver_Busy() );
	if ((!( TWI_statusReg.lastTransOK ))||(i==0)){
		//error occured
		//TWI_Get_State_Info( );   //check the error value/last TWI state and act accordingly, error codes are defined in the header
		TWI_Master_Stop();
		return 0;
	}else{
		return 1;
	}
}

unsigned int DS3231_temp;

unsigned char DS3231_getTemp(signed int *temp){
	*temp=DS3231_temp;
	return 1;
}



char DS3231_getTime(void){
	if(DS3231_cmd==0){
		DS3231_cmd=DS3231_get_time;
	}else if(DS3231_cmd==DS3231_get_time+DS3231_done){
		DS3231_cmd=0;
		return 1;
	}else if(DS3231_cmd>DS3231_error){
		DS3231_cmd=0;
	}else if(DS3231_cmd>DS3231_done){
		DS3231_cmd=DS3231_get_time;
	}
	return 0;
}

char DS3231_setTime(void){
	if(DS3231_cmd==0){
		DS3231_cmd=DS3231_set_time;
	}else if(DS3231_cmd==DS3231_set_time+DS3231_done){
		DS3231_cmd=DS3231_set_time;
		return 1;
	}else if(DS3231_cmd>DS3231_error){
		DS3231_cmd=0;
	}else if(DS3231_cmd>DS3231_done){
		DS3231_cmd=DS3231_set_time;
	}
	return 0;
}

signed char last_RTC_second=-1;

#define DS3231_I2C_ERROR_CNT_MAX 3
unsigned char DS3231_i2c_error_cnt=0;
#define DS3231_I2C_WAIT_CNT_MAX 15000
unsigned int DS3231_i2c_wait_cnt=0;

char DS3231_check_i2c_state_machine(void){
	unsigned char c_temp=0;
	unsigned char min,hour,second,day,month,year,dow;
	switch(DS3231_state){
		case 0:	DS3231_i2c_wait_cnt=0;
				if((DS3231_cmd<DS3231_done)&&(DS3231_cmd>0)){
					DS3231_state=1;
				}
				break;
		case 1:	DS3231_i2c_wait_cnt=0;
				if(DS3231_cmd==DS3231_get_time){
					DS3231_messageBuf[0]=(0x68<<TWI_ADR_BITS) | (FALSE<<TWI_READ_BIT);
					DS3231_messageBuf[1]=0x00;//setup reading from register 00 means seconds
					TWI_Start_Transceiver_With_Data( &DS3231_messageBuf[0], 2 );
					DS3231_state=20;
				}else if(DS3231_cmd==DS3231_set_time){
					clock_get_time(&min,&hour,&second,&day,&month,&year,&dow);
					DS3231_messageBuf[0]=(0x68<<TWI_ADR_BITS) | (FALSE<<TWI_READ_BIT);
					DS3231_messageBuf[1]=0x00;//start writing from register 00 means seconds
					DS3231_messageBuf[2]=((second/10)<<4)|(second%10);
					DS3231_messageBuf[3]=((min/10)<<4)|(min%10);
					DS3231_messageBuf[4]=((hour/10)<<4)|(hour%10);
					DS3231_messageBuf[5]=(dow%10);
					DS3231_messageBuf[6]=((day/10)<<4)|(day%10);
					DS3231_messageBuf[7]=((month/10)<<4)|(month%10);
					DS3231_messageBuf[8]=((year/10)<<4)|(year%10);
					TWI_Start_Transceiver_With_Data( &DS3231_messageBuf[0], 9);
					DS3231_state=40;
				}
				break;
		case 20:if(!(TWI_Transceiver_Busy() )){
					if ( TWI_statusReg.lastTransOK ){
						DS3231_messageBuf[0]=(0x68<<TWI_ADR_BITS) | (TRUE<<TWI_READ_BIT);
						TWI_Start_Transceiver_With_Data( &DS3231_messageBuf[0], 20);
						DS3231_state++;
						DS3231_i2c_error_cnt=0;
					}else{
						DS3231_i2c_error_cnt++;
						TWI_Master_Stop();TWI_MasterSlave_Initialise();I2CErrorCount++; //reset I2C module
						if(DS3231_i2c_error_cnt>DS3231_I2C_ERROR_CNT_MAX){//ok give up
							DS3231_cmd+=DS3231_done;
							DS3231_state=0;
							DS3231_i2c_error_cnt=0;
						}else{//retry same command from scratch
							DS3231_state=1;
						}
					}
				}else{
					DS3231_i2c_wait_cnt++;
					if(DS3231_i2c_wait_cnt>DS3231_I2C_WAIT_CNT_MAX){
						DS3231_i2c_error_cnt++;
						TWI_Master_Stop();TWI_MasterSlave_Initialise();I2CErrorCount++; //reset I2C module
						if(DS3231_i2c_error_cnt>DS3231_I2C_ERROR_CNT_MAX){//ok give up
							DS3231_cmd+=DS3231_done;
							DS3231_state=0;
							DS3231_i2c_error_cnt=0;
						}else{//retry same command from scratch
							DS3231_state=1;
						}
					}
				}
				break;

		case 21:if(!(TWI_Transceiver_Busy() )){
					if ( TWI_statusReg.lastTransOK ){
						TWI_Get_Data_From_Transceiver( &DS3231_messageBuf[0], 20 );
						if(last_RTC_second<0){
							last_RTC_second=(DS3231_messageBuf[1]>>4)*10+(DS3231_messageBuf[1]&0x0F);
							DS3231_state=1;//reread the time
							break;
						}else{
							if(last_RTC_second==(DS3231_messageBuf[1]>>4)*10+(DS3231_messageBuf[1]&0x0F)){
								DS3231_state=1;//reread the time
								break;
							}
						}
						//cli();
						//timer2_counter=0;
						if(DS3231_messageBuf[3]&0x40){//12 hour mode
							c_temp=((DS3231_messageBuf[3]>>4)&0x01)*10+(DS3231_messageBuf[3]&0x0F);
							if(DS3231_messageBuf[3]&0x20){
								c_temp+=12;
							}
						}else{//24 hour mode
							c_temp=((DS3231_messageBuf[3]>>4)&0x03)*10+(DS3231_messageBuf[3]&0x0F);
						}
						clock_set_time((DS3231_messageBuf[2]>>4)*10+(DS3231_messageBuf[2]&0x0F),c_temp,(DS3231_messageBuf[1]>>4)*10+(DS3231_messageBuf[1]&0x0F),(DS3231_messageBuf[5]>>4)*10+(DS3231_messageBuf[5]&0x0F),((DS3231_messageBuf[6]>>4)&0x01)*10+(DS3231_messageBuf[6]&0x0F),((DS3231_messageBuf[7]>>4)&0x0F)*10+(DS3231_messageBuf[7]&0x0F),(DS3231_messageBuf[4]&0x0F),CLOCK_UPDATE_SOURCE_RTC);
						
						/*INT_second=(DS3231_messageBuf[1]>>4)*10+(DS3231_messageBuf[1]&0x0F);
						INT_minute=(DS3231_messageBuf[2]>>4)*10+(DS3231_messageBuf[2]&0x0F);
						INT_dow=(DS3231_messageBuf[4]&0x0F);
						INT_day=(DS3231_messageBuf[5]>>4)*10+(DS3231_messageBuf[5]&0x0F);
						INT_month=((DS3231_messageBuf[6]>>4)&0x01)*10+(DS3231_messageBuf[6]&0x0F);
						INT_year=((DS3231_messageBuf[7]>>4)&0x0F)*10+(DS3231_messageBuf[7]&0x0F);*/
						
						
						last_RTC_second=-1;
						//sei();
						DS3231_temp=DS3231_messageBuf[0x12]*10;
						switch((DS3231_messageBuf[0x13]>>6)&0x03){
							case 1:DS3231_temp+=3;
									break;
							case 2:DS3231_temp+=5;
									break;
							case 3:DS3231_temp+=8;
									break;
						}
						DS3231_cmd+=DS3231_done;
						DS3231_state=0;
						DS3231_i2c_error_cnt=0;
					}else{
						DS3231_i2c_error_cnt++;
						TWI_Master_Stop();TWI_MasterSlave_Initialise();I2CErrorCount++; //reset I2C module
						if(DS3231_i2c_error_cnt>DS3231_I2C_ERROR_CNT_MAX){//ok give up
							DS3231_cmd+=DS3231_done;
							DS3231_state=0;
							DS3231_i2c_error_cnt=0;
						}else{//retry same command from scratch
							DS3231_state=1;
						}
					}
				}else{
					DS3231_i2c_wait_cnt++;
					if(DS3231_i2c_wait_cnt>DS3231_I2C_WAIT_CNT_MAX){
						DS3231_i2c_error_cnt++;
						TWI_Master_Stop();TWI_MasterSlave_Initialise();I2CErrorCount++; //reset I2C module
						if(DS3231_i2c_error_cnt>DS3231_I2C_ERROR_CNT_MAX){//ok give up
							DS3231_cmd+=DS3231_done;
							DS3231_state=0;
							DS3231_i2c_error_cnt=0;
						}else{//retry same command from scratch
							DS3231_state=1;
						}
					}
				}
				break;
		case 40:if(!(TWI_Transceiver_Busy() )){
					if ( TWI_statusReg.lastTransOK ){
						DS3231_cmd+=DS3231_done;
						DS3231_state=0;
						DS3231_i2c_error_cnt=0;
					}else{
						DS3231_i2c_error_cnt++;
						TWI_Master_Stop();TWI_MasterSlave_Initialise();I2CErrorCount++; //reset I2C module
						if(DS3231_i2c_error_cnt>DS3231_I2C_ERROR_CNT_MAX){//ok give up
							DS3231_cmd+=DS3231_done;
							DS3231_state=0;
							DS3231_i2c_error_cnt=0;
						}else{//retry same command from scratch
							DS3231_state=1;
						}
					}
				}else{
					DS3231_i2c_wait_cnt++;
					if(DS3231_i2c_wait_cnt>DS3231_I2C_WAIT_CNT_MAX){
						DS3231_i2c_error_cnt++;
						TWI_Master_Stop();TWI_MasterSlave_Initialise();I2CErrorCount++; //reset I2C module
						if(DS3231_i2c_error_cnt>DS3231_I2C_ERROR_CNT_MAX){//ok give up
							DS3231_cmd+=DS3231_done;
							DS3231_state=0;
							DS3231_i2c_error_cnt=0;
						}else{//retry same command from scratch
							DS3231_state=1;
						}
					}
				}
				break;
	}
	return DS3231_state;
}
#endif
