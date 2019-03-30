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

// #include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "etsd.h"
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
    uint16_t startP = EtsdInfo.extStart + (extS*EtsdInfo.blockIntervals/4);
    float fAddr =  (EtsdInfo.blockIntervals * (extS) + interV-1)/ 4.0;
    uint8_t bPos = (fAddr - (uint8_t)fAddr) * 8;
    uint16_t bAddr = fAddr;   // pete check rules on converting floats to uints
/*    uint8_t bAddr, startP, bPos;
    bAddr =  (EtsdInfo.blockIntervals * (extS) + interV-1)/ 4;
    bPos = ((EtsdInfo.blockIntervals*(extS) + interV - 1)/4.0 - bAddr)*8;
    startP = EtsdInfo.extStart + extS*EtsdInfo.blockIntervals/4;
*/
    return ( (PBlock.byteD[bAddr+startP] >> bPos) & 3 );
}

// If data is invalid, returns zero plus ErrorCode = E_DATA
uint32_t readAutoS(uint8_t interV, uint8_t ASC, QS_SIZE){
    uint8_t currentScaling = (SCALING >> (2*ASC)) & 3;
    uint32_t data=PBlock.data[3 + QS/4 * EtsdInfo.blockIntervals + interV];
    data = data<65535?((data << currentScaling) + currentScaling ):DATA_INVALID;
    if (DATA_INVALID == data){
        data=0;
        ErrorCode |= E_DATA;
    }
    return data; 
}

uint8_t read4(uint8_t  interV, QS_SIZE){
    return (PBlock.byteD[ 7 + QS*(EtsdInfo.blockIntervals/2) + (interV+1)/2 ]>>((interV&01)*4)) & 15;
}
uint8_t read8(uint8_t  interV, QS_SIZE){
    return PBlock.byteD[7 + QS/2 * EtsdInfo.blockIntervals + interV];
}

uint32_t read12(uint8_t interV, uint8_t extS, QS_SIZE){
    uint32_t data;
    if(1&QS){
        data = read8(interV, QS) + read4(interV, QS+2)<<8;
    } else {
        data = read8(interV, QS+1) + read4(interV, QS)<<8;
    }
    if (extS--) {
        data += ( readExtS(interV, extS) << 12 ) ;
        data = data < 16383 ? data : DATA_INVALID;
    } else {
        data = data < 4095 ? data : DATA_INVALID;  
    }
    
    if (DATA_INVALID==data){
        data = 0;
        ErrorCode |= E_DATA;
    }    
    return data;
}

uint16_t read16(uint8_t  interV, QS_SIZE){
    return PBlock.data[3 + QS/4 * EtsdInfo.blockIntervals + interV];
}
uint32_t read20(uint8_t interV, uint8_t extS, QS_SIZE){
    uint32_t data;
    if(1&QS){
        data = read8(interV, QS) + read8(interV, QS+2)<<8 + read4(interV, QS+4)<<16;
    } else {
        data = read8(interV, QS+1) + read8(interV, QS+3)<<8 + read4(interV, QS)<<16;
    }
    if (extS--) {
        data += ( readExtS(interV, extS) << 20 ) ;
        data = data <4194303 ? data : DATA_INVALID;
    } else {
        data = data < 1048575 ? data : DATA_INVALID;  
    }
    
    if (DATA_INVALID==data){
        data = 0;
        ErrorCode |= E_DATA;
    }    
    return data;
}

uint32_t read24(uint8_t interV, uint8_t extS, QS_SIZE){
    uint32_t data = read8(interV, QS+4)<<16 + read16(interV, QS);
    if (0x00FFFFFF==data){
        data = 0;
        ErrorCode |= E_DATA;
    }        
    return data;
}

// 32 bit streams can't be invalid (not possible to save more than 32 bits) so no error checking
uint32_t read32(uint8_t interV, uint8_t extS, QS_SIZE){
    uint32_t data = read16(interV, QS+4)<<16 + read16(interV, QS);
    return data;
}



// If data is invalid, returns zero plus ErrorCode = E_DATA
uint32_t readFS(uint8_t  interV, uint8_t extS, QS_SIZE){  
    uint32_t data = read16(interV, QS);
    if (extS--) {
        data += ( readExtS(interV, extS) << 16 ) ;
        data = data < 262143 ? data : DATA_INVALID;
    } else {
        data = data < 65535 ? data : DATA_INVALID;  
    }
    if (DATA_INVALID == data){
        data=0;
        ErrorCode |= E_DATA;
    }
    return data;
}

//do NOT call when interV = 0, valid intervals are 1 to EtsdInfo.blockIntervals
// HS = 0-??(FS x 2)  only error checking on extended streams
// only doing error checking on FS thru Large Stream (16-24 bits)
uint32_t readHS(uint8_t interV, uint8_t extS, QS_SIZE){  
    uint32_t data = read8(interV, QS);
    if (extS--) {
        data += ( readExtS(interV, extS) << 8 ); 
    }
	return data;
}

// returns stream data in 4 or 6 bits
// no error checking
uint32_t readQS(uint8_t  interV, uint8_t extS, QS_SIZE){  
    uint32_t data = read4(interV, QS);
    if (extS--){
        data += readExtS(interV, extS) << 4;
    } 
	return data;  
}

// reg = 1-??, registers (32bit values) are saved starting at the end of the block and working backwards
// returns saved value, does not check for invalid (all ones)
//uint32_t readReg(uint8_t reg){
//	return PBlock.longD[BLOCKSIZE/4-reg];
//}
// returns "extra data" byte stored at xData 'addr'
//uint8_t readXData(uint16_t addr) {
//    return PBlock.byteD[EtsdInfo.xDataStart + addr];
//}

// Returns value saved to stream, unless stream value == Error Value.
// If data is invalid, returns zero plus ErrorCode = E_DATA
int32_t readChan(uint8_t interV, uint8_t chan){
    int32_t data=0;
    uint8_t lp, AS=0, relCh=0, QS=0, extS=1, reg=1;

    if(!(EtsdInfo.channels)){   // indicates no ETSD initialized
        ErrorCode = E_NO_ETSD;
        ELog(__func__, 1);
        exit(1);
    }
    
    for(lp=0; lp<chan; lp++) {  //determin counters up to this channel
        if(EXTS_BIT(lp))    // Count extended streams
            extS++;
        if(AUTOSC(lp))  // Count AutoScale streams
            AS++;
        if(CNT_BIT(lp))    // Count Counter/Relative streams
            relCh++;
        if(REG_BIT(lp))     // Count saved registers 
            reg++;
        switch(ETSD_TYPE(lp)){     
            case 14:    // reserved for single precision floating point
            case 13:
                QS +=2;
            case 12:
                QS ++;
            case 11:
            case 10:
                QS ++;
            case 15:
            case 9:
            case 8:
                QS++;
            case 7:
            case 6:
                QS++;
            case 5:
            case 4:
                QS++;
            case 3:
            case 2:
                QS++;
        }
    }
    
    ErrorCode &= ~E_DATA; // clear error

    if (interV) {       
        if(!EXTS_BIT(chan)) { // channel doesn't use an extended Stream
            extS = 0;       
        }
 
        switch(ETSD_TYPE(chan)){
            case 15:             // AutoScaling
                data = readAutoS(interV, AS, QS);   
                break;
            case 14:    // not implementing floating point yet
            case 13:    
                data = read32(interV, extS, QS);   
                break;
            case 12:    
                data = read24(interV, extS, QS);   
                break;
            case 11:    
            case 10:
                data = read20(interV, extS, QS);   
                break;
            case 9:     // Extended Full Stream
            case 8:     // Full Stream
                data = readFS(interV, extS, QS);   
                break;
            case 7:     // Extended Half Stream
            case 6:     // Half Stream
                data = read12(interV, extS, QS);
                break;
            case 5:     // Extended Half Stream
            case 4:     // Half Stream
                data = readHS(interV, extS, QS);
                break;
            case 3:     // Extended Quarter Stream
            case 2:     //  Quarter Stream
                data = readQS(interV, extS, QS);
                break;
            case 1:     //  Two bit Stream
                data = readExtS(interV, --extS);
                break;
            default:
                ErrorCode |= (E_ARG | E_DATA);
        }

        if( !(ErrorCode&E_DATA) ){
            if(SIGNED(chan)){
                data = etsdToSigned(2*ETSD_TYPE(chan), data);
            }
            LastReading[chan] += data;
        }    
    } else {  // interV = 0, read registers
        if (REG_BIT(chan)) {
            data = readReg(reg);  // Pete fix lastreading if E_DATA=ErrorCode
            if (DATA_INVALID == data){
                ErrorCode |= E_DATA;
                data=0;
            } else 
                LastReading[chan]=data;
        } // else data already = 0;
    }
    return data;
}  // end of readChan()

// jumps to sector #<seek> of etsd file and returns timestamp
// If returned value = 0 check ErrorCode 
//uint32_t etsdTimeS(uint32_t seek){
//    if(etsdRW("r", seek))
//        return 0;
//    return PBlock.longD[0];
//}

// call with desired target epoch Time,
// returns Positive value that equals the desired sector(Block) that contains data stored during target Time 
// or zero to indicate error, see errorlog.h for list of error codes
uint32_t etsdFindBlock(uint32_t tTime){
    uint32_t sector, timeStamp, blockTime = EtsdInfo.intervalTime * EtsdInfo.blockIntervals;
    uint16_t validIntervals;
    uint8_t back=0, forward=0;
    
    ELog("etsdFindBlock previous errors", 1);  //log any existing errors and zero ErrorCode
    
    if (etsdRW("r",-1))
        return 0; // returns zero to indicate error reading last block of ETSD

    timeStamp = PBlock.longD[0];    // this will be the timestamp of the last block of data saved to ETSD
    if(tTime > timeStamp+(VALID_INTERVALS*EtsdInfo.intervalTime)) {
        ErrorCode |= E_AFTER; // error target time is after ETSD ends
    }
    
    timeStamp = etsdTimeS(1);  // load sector 1 of ETSD (Sector zero = header info)
    if(tTime < timeStamp) {
        ErrorCode |= E_BEFORE; // error target time is BEFORE ETSD starts
    }
    
    sector = (tTime-timeStamp)/blockTime;  // timeStamp currently = beginning of ETSD file
    while(!ErrorCode){
        while (!(timeStamp=etsdTimeS(sector))){
            if(!sector--){
                ErrorCode |= E_CODING; // shouldn't run out of sectors without finding the target time
                return 0;
            }
            ErrorCode =0; // clear errors now that we've found a sector that is part of the file.
        }
        if (timeStamp > tTime) {
            if(forward)
                back ++;
            else 
                back=1;
            if(!--sector)    // move back 1 block until sector = zero
                ErrorCode |= E_CODING; // shouldn't run out of sectors without finding the target time
        } else{
            if (timeStamp+blockTime < tTime){
                if(back)
                    forward ++;
                else 
                    forward=1;
                sector++; //move forward 1 block
            } else {
                return sector; //found it.
            }
        }
        if (back>1 && forward>1){
            ErrorCode |= E_NOT_FOUND;
        }
    }
    ELog(__func__, 1);
    return 0;  // return zero to indicate error
}

