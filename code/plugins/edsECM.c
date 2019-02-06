/*************************************************************************
edsECM.c ETSD plugin to receive and parse serial data from an Brultech ECM-1240

Copyright 2018 Peter VanDerWal 

    Note: this code was writen using information from Brultech's document: ECM1240_Packet_format_ver9.pdf
    Brultech has kindly agreed to let me release this code as 'opensource' software.
    However, I am not a lawyer and since this code was derived from Brultech's proprietary information, 
    and I don't have the right to 'give away' their proprietary information, if you intend to use 
    or distribute this code in a commercial application, you should probably contact Brultech first.  
    https://www.brultech.com/contact/
    
    This code is free: with the above stipulation you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2.0 as published by
    the Free Software Foundation.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*********************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>		//usleep
#include <sys/stat.h> /* For mode constants */ 
#include <sys/mman.h>
#include <fcntl.h>
#include <termios.h>

#include "edsECM.h" 
#include "errorlog.h"

#define INTERVAL dataU->byteD[56]
#define DATA_VALID dataU->byteD[57]   //zero = valid, 1 = updating, 2=initializing, other numbers = error code
#define PACKET_POSITION dataU->byteD[58]
#define RX_DATA dataU->byteD[59]

// in the USA Utilization voltage range is from 108V to 126V, an AC_OFFSET of 1040 allows measuring from 104.1V to 129.4V
// and marking undervoltage (0x01), overvoltage (0xFE), zero voltage (0x00), and invalid reading (0xFF)
const uint16_t AC_OFFSET=1040;

const uint32_t SHM_SIZE = 64; /* the size (in bytes) of shared memory object */

struct termios tio;
typedef union {
	uint32_t longD[16];  
	uint16_t data[32];   
	uint8_t byteD[64];   
//    char Char[64];
} DATA_UNION;

DATA_UNION *dataU;

int tty_fd;  		// Serial port file descriptor
int shm_fd;         // shared memory file descriptor 
uint8_t LogData;    // 0 = don't log data

// return values 0=success, 1 = error, 
// edsECM is compatible with ecmR library, other applications can access realtime data via shared memory using ecmConnect()
uint8_t edsSetup(char* tty_port, char* shmAddr, uint8_t cmd){
    ELog(__func__, 1);  //log any existing errors and zero ErrorCode
    if(cmd){
        if (1==cmd)
            LogData = 1;
        else
            LogData = (LogLvl>3?1:0);
    }
    if (*shmAddr == '\0') { //not using shared memory
        dataU = malloc(sizeof(DATA_UNION));
    } else {
        shm_fd = shm_open(shmAddr, O_RDWR, 0666); /* open the shared memory object */
        if (shm_fd < 0) {       // error opening shared memory object, probably doesn't exist
            //perror("shm_open()");
            shm_fd = shm_open(shmAddr, O_CREAT | O_EXCL| O_RDWR , 0666);
            if (shm_fd < 0) {
                perror("edsSetup()");
                ErrorCode |= E_SHM;  
                return -1;
            }
            ftruncate(shm_fd, SHM_SIZE);
        }
        dataU = (DATA_UNION *)mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        DATA_VALID = 1 ; // 2 = initializing/reset, data not valid
    }
     
    if (NULL == tty_port) {
        Log("<3> No tty port specified, exiting.\n");
        return 1;
    }
    tty_fd = open(tty_port, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    
    if(!isatty(tty_fd)) { 	
        ErrorCode |= E_NOT_TTY;
		return 1;
    }
    if(tcgetattr(tty_fd, &tio) < 0) {
        ErrorCode |= E_TTY_STAT;
		return 1;
    }
	 
    cfmakeraw(&tio); 
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    cfsetospeed(&tio,B19200);            // set baud, normally 19200 on ECM1240
    cfsetispeed(&tio,B19200);            
           
    tcsetattr(tty_fd,TCSANOW,&tio);
    tcflush(tty_fd,TCIOFLUSH);              //clear any old data out of buffer, don't know when it arrived so it would screw up start time. 
    
    // Might not need this with the above
    while (4>edsCheckData(10, 0));          // Clear out any data in buffers, on a restart may result in a ECM timed out error (not a problem)
    ErrorCode &= ~(E_CHECKSUM & E_TIMEOUT & E_SRC_RESET);  // clear errors from checkdata()

	return 0;
}

// timeOut in 1/10 second increments, negative value means to use default/predefined timeout
// returns  DATA_VALID  0 = rx good data, 1 = checksum/CRC error, 2=source reset, 5 = timed out no data, 9 = unspecified error, 128=updating 
uint8_t edsCheckData(int16_t tO, uint8_t interV) {
    uint_fast16_t loop=0, timeOut=tO, lastRx=0; 
    uint_fast8_t mp=2;
    time_t now;
    uint8_t header = 0;
    uint8_t checksum;

    DATA_VALID = 128;
    INTERVAL = interV;
    PACKET_POSITION = 0;
    dataU->byteD[35] = 0; 

    ErrorCode &= ~(E_CHECKSUM & E_TIMEOUT & E_SRC_RESET);  // clear previous errors
    
    while (1) {
        loop++;
        while (read(tty_fd,&RX_DATA,1)>0) {
            if (!PACKET_POSITION) {  // in header or lost.
                if (0xFE == RX_DATA) {
                    header = 1;
                } else {
                    if (header == 1) {
                        if (0xFF == RX_DATA) {
                            header = 2;
                        } else header = 0;
                    } else if (header == 2) {  //header = 2
                        if (0x03 == RX_DATA) {
                            PACKET_POSITION = 4;  // AC voltage is first actual value
                            checksum = 0; // 0 == (0xFE + 0xFF + 0x03)&0xFF ;
                        } 
                    } else
                        header = 0;
                }
            } else {
                if (65 == PACKET_POSITION) {
                    break;
                } 
                if (30 == PACKET_POSITION) {
                    dataU->data[1] = dataU->byteD[2] << 8 | dataU->byteD[3] ; // make AC Voltage little endian, Brultech sends this value bigendian
                    mp=24;
                }
                else if (41 == PACKET_POSITION) {
                    mp++;
                } else if (61 == PACKET_POSITION)
                    mp = 0;
                if (63 > PACKET_POSITION) 
                    dataU->byteD[mp++] = RX_DATA;
                checksum += RX_DATA;
                PACKET_POSITION++;
            }
            lastRx=loop;
        } // end of while(read )
            
        if (65 == PACKET_POSITION) { //rxed full packet
            PACKET_POSITION = checksum;
            time(&now);
            dataU->longD[15] = now;
            if (checksum == RX_DATA) {
                DATA_VALID = 0; // good packet      
            } else {
                if(0xFE== RX_DATA){
                    PACKET_POSITION = 64; //try getting one more byte
                    continue;
                } else {
                    ErrorCode |= E_CHECKSUM;  //checksum error
                    DATA_VALID = 1;
                }
            }
            if (!DATA_VALID){
                if(!dataU->data[1]){            // if AC voltage is zero, then ECM was just power cycled
                    ErrorCode |= E_SRC_RESET;
                    DATA_VALID = 2;
                }
            }
            if (LogData){
                LogBlock(&dataU->byteD, "ECM", 64);
            }
            return DATA_VALID; 
        }
        if (loop > timeOut ) {
            ErrorCode |= E_TIMEOUT;
            DATA_VALID = 5;
            if (LogData){
                LogBlock(&dataU->byteD, "ECM", 64);
            }
            return DATA_VALID; 
        } 
        usleep(100000);  //100,000 microseconds = 1/10 second
    } //end while(1)
    ErrorCode |= E_CODING; // Coding error, program should never reach this point
    DATA_VALID = 9;
    return DATA_VALID;
}

// Chan 1=Ch1A, 2=Ch2A, 3=Ch1p, 4=Ch2P}, 5=Aux1, 6=Aux2, 7=Aux3, 8=Aux4, 9=Aux5, 10=DC volts, 11= AC volts, 12&13 N/A, 14=Ch1 Amps, 15=Ch2 Amps
uint32_t edsReadChan (uint8_t chan, uint8_t interV){
    uint32_t data;
    if (5 > chan){                          // Ch1A, Ch2A, Ch1P, Ch2P
        uint8_t lp;
        union {
            uint32_t val;
            uint8_t cpy[4];
        } rval;
        chan = chan*5-1;
//      data = dataU->byteD[chan++];
//      data += dataU->byteD[chan++]<<8;
//      data += dataU->byteD[chan++]<<16;
//      data += dataU->byteD[chan+3]<<24;
        for (lp = 0; 4 > lp; lp++) {
            rval.cpy[lp] = dataU->byteD[chan++];
        }
        data = rval.val;
    } else if (10 > chan) {                 // Aux 1-5
        data = dataU->longD[ chan + 4];
    } else {                                // 10=DC volts, 11= AC volts, 12-23 N/A, 24 = Ch1 Amps, 25 = Ch2 Amps     16 bits
        data = dataU->data[chan - 10];
        if(11==chan){
            if (data) {  // zero = power outage during interval
                if (data < AC_OFFSET)
                    data = 1;  // 1 = Brownout
                else {
                    data -= AC_OFFSET;
                    if(0xFE < data)
                        data = 0xFE; // 0xFE = over voltage because 0xFF = invalid data
                }
            }
//        } else {
            // do something with DC voltage??
        }
    }
    return data;
}

uint8_t edsCheckReset(void) {
    return 0==dataU->data[1];
}
