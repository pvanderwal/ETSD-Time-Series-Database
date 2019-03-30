/*************************************************************************
edoSIM.c ETSD plugin to simulate source data

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
//#include <termios.h>
#include <math.h>

#include "edsSIM.h" 
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
	uint32_t longD[16];  //13
	uint16_t data[32];   //26
	uint8_t byteD[64];   //52
    char Char[64];
} DATA_UNION;

DATA_UNION *dataU;

uint8_t Counter;
int shm_fd;         // shared memory file descriptor 
uint8_t LogData;

// return values 0=success, 1 = error, 
// edsECM is compatible with ecmR library, other applications can access realtime data via shared memory using ecmConnect()
uint8_t srcSetup(char *shmAddr, char *tty_port, char *NAconfigFileName, uint16_t NAetsdHeader, uint16_t NAintervalTime){
    ELog(__func__, 1);  //log any existing errors and zero ErrorCode

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
     
    
    
    
	return 0;
}

// timeOut in 1/10 second increments, negative value means to use default/predefined timeout
// returns  DATA_VALID  0 = rx good data, 1 = checksum/CRC error, 2=source reset, 5 = timed out no data, 9 = unspecified error, 128=updating 
uint8_t srcCheckData(uint16_t tO, uint8_t interV) {

    uint32_t tempHold[5];
    
    dataU->data[1] = 120 + (SIN(Counter/20))*4
    tempHold[0] = (SIN(Counter/2)+1)*10;
    tempHold[1] = (SIN(Counter/5)+1)*20;
    tempHold[2] = (SIN(Counter/3)+1)*30;
    tempHold[3] = (SIN(Counter/7)+1)*40;
    tempHold[4] = (SIN(Counter/10)+1)*50;
    
    dataU->longD[9] += tempHold[0];
    dataU->longD[10] += tempHold[1];
    dataU->longD[11] += tempHold[2];
    dataU->longD[12] += tempHold[3];
    dataU->longD[13] += tempHold[4];

    dataU->longD[4] += (tempHold[0] + tempHold[1] + tempHold[2] + tempHold[3] + tempHold[4]);
    
    return 0;
}

// Chan 1=Ch1A, 2=Ch2A, 3=Ch1p, 4=Ch2P}, 5=Aux1, 6=Aux2, 7=Aux3, 8=Aux4, 9=Aux5, 10= AC volts, 11=DC volts, 12&13 N/A, 14=Ch1 Amps, 15=Ch2 Amps
uint32_t srcReadChan (uint8_t chan){
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
