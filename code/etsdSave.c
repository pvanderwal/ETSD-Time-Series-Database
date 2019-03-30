/*************************************************************************
 etsdSave.c library for saving data to an ETSD time series database 

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
#include "etsdSave.h"
#include "errorlog.h"

//PBLOCK PBlock;
//ETSD_INFO EtsdInfo;
//uint32_t *LastReading;  // already defined in etsd.c
//uint8_t *MissedUpdate;


void etsdBlockClear(uint16_t val){
	uint_fast8_t lp, cnt=0;
    for (lp=4; lp>0; lp++){
	    PBlock.data[lp] = val;
	}
    PBlock.data[3] = 0;  // clear autoscalling to zero
/*
    for (lp=0;lp<EtsdInfo.channels;lp++){
        cnt += EXTS_BIT(lp);
    }
    cnt=(cnt+3)/4
    for(lp=0;lp<cnt;lp++){
        PBlock.byteD[EtsdInfo.extStart+lp]=0;
    }
*/
}

void etsdBlockStart(){
    PBlock.longD[0] = ETSD_NOW();  
	PBlock.data[2] = EtsdInfo.header;
}

// write etsd block to disk
// returns zero on success, -1(DATA_INVALID) if can't rotate files, exits if can't save to current file
int32_t etsdCommit(uint8_t interV){  // write etsd block to disk
    PBlock.data[2] |= interV; // pete
    if (EtsdInfo.fileName != NULL){
        if(etsdRW("a", 0)){   // if we can't write to etsd File, error and exit
            ELog(__func__, 1);
            LogBlock(&PBlock.byteD, "ETSD", BLOCKSIZE); // try to save current ETSD block to the error log
            exit(1);
        }
        if(RotateEtsd){
            if (etsdRotate()){
                ELog(__func__, 1);
                return DATA_INVALID;
            }
            RotateEtsd = 0;
        }
    }
    return 0;   
}

// backs up current etsd file and opens a new one with the current name.  Copies the first sector (db info) to new file
// to avoid losing data, execute etsdCommit() before etsdRotate()
// returns zero on success or error code
int32_t etsdRotate() {
    char *backup;
    ELog(__func__, 1); // print out any existing errors before proceeding
    
    if (etsdRW("r", 0)){   // load pBlock with first sector (Header) of etsd file.
        return DATA_INVALID;      // if we can't read current header, then we can't create new file properly
    }
    backup = (char*)malloc(strlen(EtsdInfo.fileName)+13);
    sprintf(backup,"%s.%d", EtsdInfo.fileName, ETSD_NOW() );
    rename(EtsdInfo.fileName, backup);

    if(etsdRW("w", 0)){ // open new etsd file and write header 
        ELog(__func__, 1); // log error and exit if we can't open new file
        exit(1);
    }        
    etsdBlockClear(0xffff);
    etsdBlockStart();
    free(backup);
    return 0;       
}

// src= ETSD source type that was Reset, interv is last good interval before source was reset
// returns zero.  On return, calling program should reset interval to zero.  i.e. Interval = etsdSrcReset( type, Interval);
uint8_t etsdSrcReset(uint8_t src, uint8_t interV){
    uint8_t lp;
    SRC_RESET(src);
    etsdCommit(interV);
    for(lp=0; lp<EtsdInfo.channels; lp++){
        if (SRC_TYPE(lp)==src){
            LastReading[lp]=DATA_INVALID;
        }
    }
    return 0;
}


// Convert signed value to <bits> size etsd format 
// returns 0xFFFFFFFF(DATA_INVALID) if data is too large to fit in the given number of bits
uint32_t etsdFromSigned(uint8_t bits, int32_t data) {
    int32_t negative = 1 << (bits-1);
    int32_t maxV = negative - 1;
    if (0 > data) {
        if (0-data > maxV){
            ErrorCode |= E_DATA;
            data = DATA_INVALID;
        } else {
            data = negative & (1-data); 
        }
    } else {
        if (maxV < data){
            ErrorCode |= E_DATA;
            data = DATA_INVALID;
        }
    }
    return data;
}

// pete fix to use xDataSize? save "extra data"
void saveXData(uint16_t addr, uint8_t data ) {
    PBlock.byteD[EtsdInfo.xDataStart + addr] = data;
}


uint8_t etsdReadByte(uint16_t addr) {
    return PBlock.byteD[addr];
}


//reg = 1-??,  registers (32bit values) are saved starting at end of Block, working back towards front.
void saveReg(uint8_t reg, uint32_t data){
	PBlock.longD[BLOCKSIZE/4-reg] = data;
}

// Auto-Scaling works on full streams only. Can handle any value up to 524,287, larger values save as all '1's (65535)
// do NOT call when interV = 0
// ASC = Auto-Scaling channel 0-7, QS = quarter stream ID.   
void saveAutoS(uint8_t interV, uint8_t ASC, QS_SIZE, uint32_t data){
    uint8_t currentScaling = (SCALING >> (2*ASC)) & 3;  // SCALING = PBlock.data[3]
    uint8_t excess = data >> (16+currentScaling) ;  
    uint8_t streamStart = 3 + QS/4 * EtsdInfo.blockIntervals;  // points to PBlock.data so 8 bits is enough
    uint8_t lp;
    uint16_t preVal;
    if( 524287 > data ){    // check if data will fit
        if (excess) {   // if excess then new scaling factor is larger than currentScaling and we need to rescale any previously stored data
            excess >>=1;    //convert excess bits to scaling factor 
            if(excess<3){    //second step
                excess++;
            }
            for (lp=1; lp<interV; lp++) {  
                preVal = PBlock.data[streamStart+lp];
                if (preVal < 65535){    //only rescale valid data
                    PBlock.data[streamStart+lp] = (preVal>>excess);
                }
            }
            currentScaling += excess;   // update currentScaling
            SCALING += excess<<(ASC*2); // update stored scaling factor
            
        } 
        data >>= currentScaling;
        if ( 65535 == data){
            data--;                         // avoid saving valid data as all ones (invalid)
        }
        PBlock.data[streamStart + interV ] = data;
    } else {
        ErrorCode |= E_DATA;
Log("saveAutoS data = %u \n", data);  // Pete
    }
}

// normally used to extended (add 2 bits to) a data stream, but can be used to store 2 bit data streams.  Only LSbx2 of data is stored
// extS values 0 to (number of extended channels)  
// 'dummy' variable to make function parameters the same as the other stream functions
// pete test this
void saveExtS(uint8_t interV, uint8_t extS, uint8_t dummy, uint32_t data){
    uint16_t startP = EtsdInfo.extStart + (extS*EtsdInfo.blockIntervals/4);
//    float fAddr =  (EtsdInfo.blockIntervals * (extS-1) + interV-1)/ 4.0;
    float fAddr =  (EtsdInfo.blockIntervals * (extS) + interV-1)/ 4.0;
    uint8_t bPos = (fAddr - (uint8_t)fAddr) * 8;
    uint16_t bAddr = fAddr;   // pete check rules on converting floats to uints
    if (data > 3){
        ErrorCode |= E_DATA;
        data = 3;
    }
    PBlock.byteD[bAddr+startP] = ( PBlock.byteD[bAddr+startP] & (uint8_t)(~(3<<bPos)) ) | ( (data&3) <<bPos );  // can't depend on current bits being zero
}

void save16(uint8_t interV, QS_SIZE, uint16_t data){
    PBlock.data[3 + QS/4 * EtsdInfo.blockIntervals + interV ] = data;
}
void save8(uint8_t interV, QS_SIZE, uint8_t data){
    PBlock.byteD[7 + QS/2 * EtsdInfo.blockIntervals + interV] = data;
}
void save4(uint8_t interV, QS_SIZE, uint8_t data){
    uint16_t  addr = 7 + QS*(EtsdInfo.blockIntervals/2) + (uint16_t)((interV+1)/2);  //pete check this, does it need blockintervals/2.0?
	uint8_t  shft = (interV&01)*4;
	PBlock.byteD[addr] = (PBlock.byteD[addr]&(240>>shft)) + ((data&15)<<shft);
}
// 32 bit streams can't have invalid data since they can handle all 32 bits of 'data'
// do NOT call when interV = 0, valid intervals are 1 to (EtsdInfo.blockIntervals)
void save32(uint8_t interV, uint8_t extS, QS_SIZE, uint32_t data){
    save16(interV, QS, data);
    save16(interV, QS+4, data>>16);
}

// QS = 0 - ??.  Valid Data = 0-16,777,214
// do NOT call when interV = 0, valid intervals are 1 to (EtsdInfo.blockIntervals)
void save24(uint8_t interV, uint8_t extS, QS_SIZE, uint32_t data){
    if (16777215<data)
        data=16777215;
    save8(interV, QS, data);
    save8(interV, QS+2, data>>8);
    save8(interV, QS+4, data>>16);
}

// QS = 0 - ??.  Valid Data = 0-16,777,214
// do NOT call when interV = 0, valid intervals are 1 to (EtsdInfo.blockIntervals)
void save20(uint8_t interV, uint8_t extS, QS_SIZE, uint32_t data){
    if (extS--) {  
        if (4194302 < data) { // 262142 is largest (valid) value that can be saved with extended data
            ErrorCode |= E_DATA;
            data = 4194303; 
        } 
        saveExtS(interV, extS, 0, data>>16);    
    } else {
        if (1048574<data){
            ErrorCode |= E_DATA;
            data=1048575;
        }
    }
    
    if(1&QS){
        save8(interV, QS, data);
        save8(interV, QS+2, data>>8);
        save4(interV, QS+4, data>>16);
    } else {
        save4(interV, QS, data>>16);
        save8(interV, QS+1, data);
        save8(interV, QS+3, data>>8);
    }
}

// QS = 0 - ??.  Valid Data = 0-16,777,214
// do NOT call when interV = 0, valid intervals are 1 to (EtsdInfo.blockIntervals)
void save12(uint8_t interV, uint8_t extS, QS_SIZE, uint32_t data){
    if (extS--) {  
        if (16382 < data) { // 262142 is largest (valid) value that can be saved with extended data
            ErrorCode |= E_DATA;
            data = 16383; 
        } 
        saveExtS(interV, extS, 0, (data>>16));    
    } else {
        if (4094<data){
            ErrorCode |= E_DATA;
            data=4095;
        }
    }
    if(1&QS){
        save8(interV, QS, data);
        save4(interV, QS+2, data>>8);
    } else {
        save4(interV, QS, data>>8);
        save8(interV, QS+1, data);
    }
}

// extS: 0=don't save extended data, otherwise indicates which extS stream to use
// QS = 0 - ??.  Valid Data = 0-65534 without extS, 0-262142 with extS
// do NOT call when interV = 0, valid intervals are 1 to (EtsdInfo.blockIntervals)
void saveFS(uint8_t interV, uint8_t extS, QS_SIZE, uint32_t data){
    if (extS--) {  
        if (262142 < data) { // 262142 is largest (valid) value that can be saved with extended data
            ErrorCode |= E_DATA;
            data = 262143; 
        } 
        saveExtS(interV, extS, 0, (data>>16));    
    } else {
        if (65534< data){ // 65535 is largest value that can be saved without extended data
            ErrorCode |= E_DATA;
            data = 65535;
        }
    }
    save16(interV, QS, data);
}

//do NOT call when interV = 0, valid intervals are 1 to EtsdInfo.blockIntervals
// QS = 0-??(QS x 2)
// only doing error checking on FS or larger, truncate data to fit
void saveHS(uint8_t  interV, uint8_t extS, QS_SIZE, uint32_t data){
    if (extS--) {   
        saveExtS(interV, extS, 0, data>>8); // & 3;        
    } 
    save8(interV, QS, data);
}


//pete this needs to be tested
//interval = 1-24, Quarter Stream (QS) = 0-?? (FS x 4)  QS 2 = HS1 = FS1, QS 38 = FS10
void saveQS(uint8_t  interV, uint8_t extS, QS_SIZE, uint32_t data){
    if (extS--) {
        if (63 < data){ // 63 is the largest value that can be stored using extended data
            ErrorCode |= E_DATA;
            data = 63;
        }
        saveExtS(interV, extS, 0, data>>4); // & 3;        
    } else 
        if (15< data){
            ErrorCode |= E_DATA;
            data = 15;
        }
    save4(interV, QS, data);
}

// saveChan() automagically determines the right type of stream to save data to based on header block info.  
// Also tracks previous values on "counter" streams 
// chan = 0 thru (EtsdInfo.channels-1)
// call with interV = 0 to save registers and set counter variables.
// call with interV > 0 to save data as either counter or gauge based on header block info.
// dataInvalid 1=checksum, timeout,etc.  2 = source reset.
// Pete: declaring data as 'int' should allows passing pointers to floats if needed for future upgrades??
void saveChan(uint8_t interV, uint8_t chan, uint8_t dataInvalid, uint32_t data){
    uint32_t etsdData;
    uint8_t lp, missed, extS=1, reg=1, AS=0, QS=0;  
    void (*funct_ptr)(uint8_t  interV, uint8_t extS, QS_SIZE, uint32_t data);  // any float data needs to be converted BEFORE calling function_pointer
    
    if(!(EtsdInfo.channels)){
        ErrorCode = E_NO_ETSD;
        ELog(__func__, 1);
        exit(1);
    }
    for(lp=0; lp<chan; lp++) {
        if(AUTOSC(lp))  // Count AutoScale streams up to this channel
            AS++;
        if(REG_BIT(lp))     // count saved registers up to this channel
            reg++;
/*        
        if(10 > ETSD_TYPE(lp)){
            extS += EXTS_BIT(lp);   // Count extended streams up to this channel
            QS += (ETSD_TYPE(lp)&14)/2;
        }else{
            QS += 4;  // for AutoScale streams
        }
*/        

//pete when adding extra types:
        if(13 > ETSD_TYPE(lp)){
            extS += EXTS_BIT(lp);   // Count extended streams up to this channel
            QS += (ETSD_TYPE(lp)&14)/2;
        }else{
            if (15==(ETSD_TYPE(lp)))
                QS += 4;  // for AutoScale streams
            else 
                QS +=8; // for 32 bit or single precission float
        }
         
    }

    if (interV) {   
//Log("saveChan Interval: %d - Channel #: %d - dataInvalid: %d  data = %u ", interV, chan, dataInvalid, data);
        if(!EXTS_BIT(chan)) { // channel doesn't use an extended Stream
            extS = 0;       
        }

        if(!CNT_BIT(chan) || dataInvalid){    // Gauge channel or invalid data
            if(SIGNED(chan)){
                etsdData = etsdFromSigned(2*ETSD_TYPE(chan), data);
            } else {
                etsdData = data; 
            }
            //goodUpdate = 0; // ignored if gauge
            if (2&dataInvalid) {    // source reset
                LastReading[chan] = 0xffffffff;
                MissedUpdate[chan] = 0;
            }
            missed = 0; // don't update previous values when Absolute
        } else {        // Counter value with good data
            //goodUpdate = 1;
            if (  0xffffffff != LastReading[chan]){ // valid last reading
                missed = MissedUpdate[chan] < interV ? MissedUpdate[chan]:(interV-1);
                etsdData = (data - LastReading[chan])/(1+MissedUpdate[chan]);
            } else {   // last reading has never been valid, nothing to compare to att
                etsdData = 0xffffffff;
                missed = 0;
            }    
        }                
//Log("etsdData: %d\n", etsdData); 
        switch(ETSD_TYPE(chan)){
            case 15:     // AutoScaling
                funct_ptr=saveAutoS;
                extS = AS;
                break;
            case 14:
                // Pete can't think of a way to make sure platform is using 32 bit floats.
                // need to work on this  for now assume user will convert it before sending it to saveChan()
            case 13:     // Double Stream
                funct_ptr=save32;
                break;
            case 12:     // Full Stream
                funct_ptr=save24;
                break;
            case 11:     // Full Stream
            case 10:  
                funct_ptr=save20;
                break;
            case 9:     // Extended Full Stream
            case 8:     // Full Stream
                funct_ptr=saveFS;
                break;
            case 7:     // Extended Full Stream
            case 6:     // Full Stream
                funct_ptr=save12;
                break;
            case 5:     // Extended Half Stream
            case 4:     // Half Stream
                funct_ptr=saveHS;
                break;
            case 3:     // Extended Quarter Stream
            case 2:     //  Quarter Stream
                funct_ptr=saveQS;  
                break;
            case 1:     //  Two bit Stream
                extS--;
                funct_ptr=saveExtS;
                break;
        }
//Log("saveChan calculating missed intervals.  Interval: %d  Missed: %d  QuarterStream: %d\n", interV, missed, QS); 
        for (lp=interV-missed; lp<=interV;lp++){  //update missing intervals 
            funct_ptr(lp, extS, QS, etsdData);   
        }
        if (CNT_BIT(chan)){
//            if(goodUpdate){
            if(dataInvalid){
                MissedUpdate[chan]++;
                if(!MissedUpdate[chan]){     //if missed more than 255 intervals, give up and start over
                    LastReading[chan] = 0xffffffff;
                }
            }else{
                MissedUpdate[chan] = 0;
                LastReading[chan] = data; // if valid data = 0xffffffff then LastReading would indicate we've never seen good data
            }
        }
    } else {  // interV = 0, save registers
        if (!dataInvalid && REG_BIT(chan)) {
            if( 0xffffffff == data && CNT_BIT(chan)) //all ones indicate error values
                data++;
            saveReg(reg, data);
            if (  0xffffffff == LastReading[chan]){  //first valid reading
                LastReading[chan] = data;
                MissedUpdate[chan] = 0;
            }
        }
    }    
    ELog(__func__, 1);
} // saveChan()
