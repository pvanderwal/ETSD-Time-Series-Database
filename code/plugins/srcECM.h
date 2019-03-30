/*************************************************************************
esrECM.h ETSD plugin used to receive and parse serial data from an Brultech ECM-1240

Copyright 2018 Peter VanDerWal 

    Note: this code was writen using information from Brultech's document: ECM1240_Packet_format_ver9.pdf
    Brultech has kindly agreed to let me release this code as 'opensource' software.
    However, I am not a lawyer and since this code was derived from Brultech's proprietary information,  
    if you intend to use or distribute this code in a commercial application, just to be on the safe side,
    you should probably contact Brultech first.  
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
#ifndef __edsECM_h__
#define __edsECM_h__

#ifdef __cplusplus
extern "C" {
#endif

// return values 0=success, 1 = error, 
// edsECM is compatible with ecmR library, other applications can access realtime data via shared memory using ecmConnect()
// cmd = 1 log all data, if cmd=3 then only log data if LogLvl > 3
uint8_t srcSetup(char *config, char *port, char *configFileName, uint16_t etsdHeader, uint16_t intervalTime);

// timeOut in 1/10 second increments
// returns  DATA_VALID  0 = rx good data, 1 = checksum/CRC error, 2=source reset, 5 = timed out no data, 9 = unspecified error, 128=updating 
uint8_t srcCheckData(uint16_t timeOut, uint8_t interV);

// Chan 1=Ch1A, 2=Ch2A, 3=Ch1p, 4=Ch2P}, 5=Aux1, 6=Aux2, 7=Aux3, 8=Aux4, 9=Aux5, 10= AC volts, 11=DC volts, 12&13 N/A, 14=Ch1 Amps, 15=Ch2 Amps
// returns data from specified channel
uint32_t edsReadChan (uint8_t chan);

// uint8_t edsCheckReset(void);

#ifdef __cplusplus
}
#endif

#endif
