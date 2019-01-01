/*************************************************************************
etsdSave.c library for reading, searching, and parsing data from an ETSD time series database 

Copyright 2018 Peter VanDerWal 
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2.0 as published by
    the Free Software Foundation
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*********************************************************************************/

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "etsdSave.h"
#include "etsdRead.h"
#include "errorlog.h"

// Convert <bits> size etsd format to signed value
int32_t etsdToSigned(uint8_t bits, uint32_t data) {
    int32_t negative = data & ( 1<< (bits-1) );
    if (negative) 
        data = -1-(data-negative);
    return (int32_t)data;
}

// extS values 0 to (number of extended channels)
// pete test this
uint8_t readExtS(uint8_t interV, uint8_t extS){
    uint8_t bAddr, startP, bPos;
    bAddr =  (EtsdInfo.blockIntervals * (extS-1) + interV-1)/ 4;
    bPos = ((EtsdInfo.blockIntervals*(extS-1) + interV - 1)/4.0 - bAddr)*8;
    startP = EtsdInfo.extStart + extS*EtsdInfo.blockIntervals/4;
    return (etsdReadByte(bAddr+startP) >> bPos) & 3;
}

// If data is invalid, returns zero plus ErrorCode = E_DATA
uint32_t readAutoS(uint8_t interV, uint8_t ASC, uint8_t QS){
    uint8_t currentScaling = (SCALING >> (2*ASC)) & 3;
    uint8_t streamStart = 3 + QS/4 * EtsdInfo.blockIntervals;  // points to PBlock.data so 8 bits is enough
    uint32_t data=PBlock.data[streamStart + interV];
    data = data<65535?(data << currentScaling) + currentScaling :0xFFFFFFFF;
    if (0xFFFFFFFF == data){
        data=0;
        ErrorCode |= E_DATA;
    }
    return data; 
}

// If data is invalid, returns zero plus ErrorCode = E_DATA
uint32_t readFS(uint8_t  interV, uint8_t extS, uint8_t QS){  
    uint32_t data = PBlock.data[3 + QS/4 * EtsdInfo.blockIntervals + interV];
    if (extS) {
        data += ( readExtS(interV, extS) << 16 ) ;
        data = data < 262143 ? data : 0xFFFFFFFF;
    } else {
        data = data < 65535 ? data : 0xFFFFFFFF;
    }
    if (0xFFFFFFFF == data){
        data=0;
        ErrorCode |= E_DATA;
    }
    return data;
}

//do NOT call when interV = 0, valid intervals are 1 to EtsdInfo.blockIntervals
// HS = 0-??(FS x 2)
// If data is invalid, returns zero plus ErrorCode = E_DATA
uint32_t readHS(uint8_t interV, uint8_t extS, uint8_t QS){  
    uint32_t data = PBlock.byteD[7 + QS/2 * EtsdInfo.blockIntervals + interV];
    if (extS) {
        data += ( readExtS(interV, extS) << 8 ); 
        data = data < 1023 ? data : 0xFFFFFFFF;
    } else
        data = data < 1023 ? data : 0xFFFFFFFF;
    if (0xFFFFFFFF == data){
        data=0;
        ErrorCode |= E_DATA;
    }
	return data;
}

// returns stream data in 4 or 6 bits
// If data is invalid, returns zero plus ErrorCode = E_DATA
uint32_t readQS(uint8_t  interV, uint8_t extS, uint8_t QS){  
    uint32_t data = (PBlock.byteD[ 7 + QS*(EtsdInfo.blockIntervals/2) + (interV+1)/2 ]>>((interV&01)*4)) & 15;
    if (extS){
        data += readExtS(interV, extS) << 4;
        data = data < 63 ? data : 0xFFFFFFFF;
    } else
        data = data < 15 ? data : 0xFFFFFFFF;
    if (0xFFFFFFFF == data){
        ErrorCode |= E_DATA;
        data=0;
    }
	return data;  
}

// reg = 1-??, registers (32bit values) are saved starting at the end of the block and working backwards
// returns saved value, does not check for invalid (all ones)
uint32_t readReg(uint8_t reg){
	return PBlock.longD[BLOCKSIZE/4-reg];
}

// returns "extra data" byte
// returns saved value, does not check for invalid (all ones)
uint8_t readXData(uint16_t addr) {
    return PBlock.byteD[EtsdInfo.dataStart + addr];
}

// Returns value saved to stream, unless stream value == Error Value.
// If data is invalid, returns zero plus ErrorCode = E_DATA
int32_t readChan(uint8_t interV, uint8_t chan){
    int32_t data=0;
    uint8_t lp, extS, extSCh=0, AS=0, relCh=0, reg=0, QS=0;

    for(lp=0; lp<chan; lp++) {  //determin counters up to this channel
        if(EXTS_bit(lp))    // Count extended streams
            extSCh++;
        if(AUTOSC_bit(lp))  // Count AutoScale streams
            AS++;
        if(CNT_bit(lp))    // Count Counter/Relative streams
            relCh++;
        if(REG_bit(lp))     // Count saved registers 
            reg++;
        QS += (ETSD_Type(lp)&14)/2;
        if(10==ETSD_Type(lp))
            QS--;
 /*
 switch (ETSD_Type(lp)){  //COunt number of quarter streams used 
            case 8:
            case 7:
            case 6:
                QS+=2;
            case 5:
            case 4:
                QS+=2;
                break:
            case 3:
            case 2:
                QS++;
        }
        */
    }

    if (interV) {       
        if(EXTS_bit(chan)) { // channel uses an extended Stream
            extS = extSCh;
        } else {
            extS = 0;       
        }
 
        switch(ETSD_Type(chan)){
            case 10:             // AutoScaling
                data = readAutoS(lp, AS, QS); 
                break;
            case 9:     // Extended Full Stream
            case 8:     // Full Stream
                data = readFS(lp, extS, QS);   
                break;
            case 5:     // Extended Half Stream
            case 4:     // Half Stream
                data = readHS(lp, extS, QS);
                break;
            case 3:     // Extended Quarter Stream
            case 2:     //  Quarter Stream
                data = readQS(lp, extS, QS);
                break;
            case 1:     //  Two bit Stream
                data = readExtS(lp, extS);
                break;
            default:
                ErrorCode |= E_CODING | E_DATA;
         }
        if(CNT_bit(chan)){
            if(!ErrorCode&E_DATA){
                lastReading[chan] += data;
            }
        } else if(INT_bit(chan)){
            if(!ErrorCode&E_DATA){
//            if(ErrorCode&E_DATA){
//                data = -2147483648;  // most negative int32_t value
//            } else {
                data = etsdToSigned(2*ETSD_Type(chan), data);
            }
 //       } else {
        }
        
    } else {  // interV = 0, read registers
        if (REG_bit(chan)) {
            data = readReg(reg);  //pete fix lastreading if E_DATA=ErrorCode
            if (0xFFFFFFFF == data){
                ErrorCode |= E_DATA;
                data=0;
            } else 
                lastReading[chan]=data;
        }
    }
    return data;
}

// jumps to sector #<seek> of etsd file and returns timestamp
// If returned value = 0 check ErrorCode 
uint32_t etsdTimeS(uint32_t seek){
    if(etsdRW("r", seek))
        return 0;
    return PBlock.longD[0];
}

// call with desired target epoch Time,
// returns Positive value that equals the desired sector(Block) that contains data stored during target Time 
// or zero to indicate error, see errorlog.h for list of error codes
uint32_t etsdFindBlock(uint32_t tTime){
    uint32_t sector, timeStamp, blockTime = EtsdInfo.intervalTime * EtsdInfo.blockIntervals;
    uint16_t validIntervals;
    ELog(__func__, 1);  //log any existing errors and zero ErrorCode
    if (etsdRW("r",-1))
        return 0; // returns zero to indicate error reading last block of ETSD
    timeStamp = PBlock.longD[0];    // this will be the timestamp of the last block of data saved to ETSD
    if(tTime > timeStamp+(VALID_INTERVALS*EtsdInfo.intervalTime)) 
        ErrorCode |= E_AFTER; // error target time is after ETSD ends  
    timeStamp = etsdTimeS(1);  // load sector 1 of ETSD (Sector zero = header info)
    if(!ErrorCode && tTime < timeStamp)
        ErrorCode |= E_BEFORE; // error target time is BEFORE ETSD starts
    sector = (tTime-timeStamp)/blockTime;
    while(!ErrorCode){
        timeStamp=etsdTimeS(sector);
        if (timeStamp > tTime) {
            if(!--sector)    // move back 1 block until sector = zero
                ErrorCode |= E_CODING; // shouldn't run out of sectors without finding the target time
        } else{
            if (timeStamp+blockTime < tTime){
                sector++; //move forward 1 block
            } else {
                return sector; //found it.
            }
        }
    }
    ELog(__func__, 1);
    return 0;  // return zero to indicate error
}

