/*************************************************************************
 etsdSave library for saving data to an ETSD time series database 

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
#include <signal.h>

#include "etsdSave.h"
#include "errorlog.h"

PBLOCK PBlock;
ETSD_INFO EtsdInfo;
uint32_t *lastReading;
uint8_t *missedUpdate;

// send 'kill -SIGUSR1 <process id>' to rotate ETSD File
volatile sig_atomic_t RotateEtsd;

void etsdSigHandler(int signum) {
    RotateEtsd = 1;
}

// etsdInit returns zero on success or -1 on error  See errorlog.h for error codes. 
int32_t etsdInit(char *fName, uint8_t loadLabels) {
    //float streams=0.0;
    uint16_t lp, idx=0, streams=0;
    int8_t error, extSCnt=0;
    
    signal(SIGUSR1, etsdSigHandler);   // Rotate etsd file on signal from user app
    
    EtsdInfo.fileName = (char*) malloc(strlen(fName)+1);
    strcpy(EtsdInfo.fileName,fName); 
    if (etsdRW("r", 0)){  // opens EtsdInfo.fileName for reading and reads first sector into Pblock;
        ELog(__func__, 0);
        return -1; // error can't open etsdFile for reading
    }
    if (ETSD_HEADER != PBlock.longD[0]){ // check to make sure block starts with "CfgE"
        ErrorCode |= E_NOT_ETSD; //error not etsd file
        ELog(__func__, 0);
        return -1;
    }
    EtsdInfo.header = PBlock.data[2] & 65408;  	//grab the MSbx9
    EtsdInfo.blockIntervals = (PBlock.data[2] >> 7) & 127;
    EtsdInfo.channels = PBlock.data[2] & 127;    // etsd ver 1.0 supports 127 channels max
    EtsdInfo.intervalTime = PBlock.data[3];     // ~18.2 hours maximum interval.
    EtsdInfo.labelSize = PBlock.byteD[8];
    EtsdInfo.dataBytes = PBlock.byteD[9];
    
    EtsdInfo.source=(uint8_t*)malloc(EtsdInfo.channels);
    EtsdInfo.destination=(uint8_t*)malloc(EtsdInfo.channels);
    
    for(lp=0;lp<EtsdInfo.channels; lp++){
        EtsdInfo.source[lp] = PBlock.byteD[lp*2 + 10];  
        EtsdInfo.destination[lp] = PBlock.byteD[lp*2 + 11];  
        if (ETSD_Type(lp)){  // if saving to etsd
            streams += ETSD_Type(lp)&14; //drop the last bit
            if(10==(ETSD_Type(lp)&14))
                streams -=2;
            if (EXTS_bit(lp)){
                extSCnt++;
            }
            if (REG_bit(lp)){
                EtsdInfo.registers++;
            }
        }
        if (RRD_bit(lp)){  // If saving to RRD
            EtsdInfo.rrdCnt++;
        }
    }
    if (loadLabels) {
        EtsdInfo.labelBlob = (char*)calloc(EtsdInfo.labelSize*2, sizeof(char));  // allocate blob of space to hold all the labels
        EtsdInfo.label = malloc(EtsdInfo.channels * sizeof(char*)); // allocate an array of pointers to individual labels in blob

        memcpy(EtsdInfo.labelBlob, PBlock.byteD+10+2*EtsdInfo.channels, EtsdInfo.labelSize*2);
        EtsdInfo.label[0]=EtsdInfo.labelBlob;      // point to first label
        for (lp=1; lp<EtsdInfo.channels;lp++){
            while (EtsdInfo.labelBlob[idx++]); //search for next null at the end of each lable
            EtsdInfo.label[lp] = EtsdInfo.labelBlob+idx;
        }
    }

    EtsdInfo.extStart = 8.75 + EtsdInfo.blockIntervals * streams/4.0; 
    EtsdInfo.dataStart =  EtsdInfo.extStart + extSCnt/4.0 + 0.75;
    etsdBlockClear(0xFFFF);

// initialize lastReading and missedUpdate arrays
    if (EtsdInfo.channels){
        lastReading = (uint32_t*)malloc(EtsdInfo.channels*sizeof(uint32_t));
        missedUpdate = (uint8_t*)malloc(EtsdInfo.channels*sizeof(uint8_t));
        for(lp=0;lp<EtsdInfo.channels;lp++){
            lastReading[lp] = DATA_INVALID;   // set all old readings to DATA_INVALID (0xFFFFFFFF)
            missedUpdate[lp] = 0;       // 0 = no missed updates
        }
    } 
    return 0;
}

void etsdBlockClear(uint16_t val){
	uint_fast8_t lp, cnt=0;
    for (lp=4; lp>0; lp++){
	    PBlock.data[lp] = val;
	}
    PBlock.data[3] = 0;  // clear autoscalling to zero
/*
    for (lp=0;lp<EtsdInfo.channels;lp++){
        cnt += EXTS_bit(lp);
    }
    cnt=(cnt+3)/4
    for(lp=0;lp<cnt;lp++){
        PBlock.byteD[EtsdInfo.extStart+lp]=0;
    }
*/
}

void etsdBlockStart(){
    time_t now;
    time(&now);  // pete  On a 64 bit OS this should work until until Feb 2106, might have some problems on a 32 bit OS after 2038 (or not)
    PBlock.longD[0] = now;  
	PBlock.data[2] = EtsdInfo.header;
}

// mode r=read, w=write, a=append.  For read, sector = which sector to read, negative sectors are relative to end of file
// returns zero on success, or  -1(DATA_INVALID) on failure and sets ErrorCode , see errorlog.h for error codes
int32_t etsdRW(char *mode, int32_t sector){
    FILE *etsd;

    if (mode[0] < 97){
        mode[0] += 32; //convert upper case to lower case
    }

    etsd = fopen(EtsdInfo.fileName, mode);

    switch (mode[0]){
        case 'r':     
            if (etsd > 0) {
                if(sector){
                    if(fseek(etsd, sector*BLOCKSIZE, sector<0?SEEK_END:SEEK_SET)){ 
                        ErrorCode |= E_SEEK;
                    }
                }
                if(1 != fread(&PBlock, BLOCKSIZE, 1, etsd)){ 
                    ErrorCode |= E_EOF;
                    return DATA_INVALID;
                }
            } else {
                ErrorCode |= E_CANT_READ;
                return DATA_INVALID;
            }
            break;
        case 'a':
        case 'w':
            if (etsd > 0) {
                fwrite(&PBlock, BLOCKSIZE, 1, etsd);
            } else {
                ErrorCode |= E_CANT_WRITE;
                return DATA_INVALID;
            }
            break;
    }
    fclose(etsd);
    return 0;
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
    time_t now;
    ELog(__func__, 1); // print out any existing errors before proceeding
    
    if (etsdRW("r", 0)){   // load pBlock with first sector (Header) of etsd file.
        return DATA_INVALID;      // if we can't read current header, then we can't create new file properly
    }
    time(&now);
    backup = (char*)malloc(strlen(EtsdInfo.fileName)+13);
    sprintf(backup,"%s.%d", EtsdInfo.fileName, now);
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

// save "extra data"
void saveXData(uint16_t addr, uint8_t data ) {
    PBlock.byteD[EtsdInfo.dataStart + addr] = data;
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
void saveAutoS(uint8_t interV, uint8_t ASC, uint8_t QS, uint32_t data){
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
Log("Autosave data = %u \n", data);  // Pete
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
    if (data > 3)
        data = 3;
    PBlock.byteD[bAddr+startP] = ( PBlock.byteD[bAddr+startP] & (uint8_t)(~(3<<bPos)) ) | ( (data&3) <<bPos );  // can't depend on current bits being zero
}

// extS: 0=don't save extended data, otherwise indicates which extS stream to use
// QS = 0 - ??.  Valid Data = 0-65534 without extS, 0-262142 with extS
// do NOT call when interV = 0, valid intervals are 1 to (EtsdInfo.blockIntervals)
void saveFS(uint8_t interV, uint8_t extS, uint8_t QS, uint32_t data){
    if (extS--) {  
        if (262143 < data) { // 262142 is largest (valid) value that can be saved with extended data
            ErrorCode |= E_DATA;
            data = 262143; 
        } 
        saveExtS(interV, extS, 0, (data>>16));    
        data &= 65535;
    } else {
        if (65535< data){ // 65535 is largest value that can be saved without extended data
            ErrorCode |= E_DATA;
            data = 65535;
        }
    }
    PBlock.data[3 + QS/4 * EtsdInfo.blockIntervals + interV ] = data;
}

//do NOT call when interV = 0, valid intervals are 1 to EtsdInfo.blockIntervals
// QS = 0-??(QS x 2)
void saveHS(uint8_t  interV, uint8_t extS, uint8_t QS, uint32_t data){
    if (extS--) {   
        if (1023 < data){ // 1023 is the largest value that can be stored using extended data
            ErrorCode |= E_DATA;
            data = 1023;
        }
        saveExtS(interV, extS, 0, data>>8); // & 3;        
    } else 
        if (255< data){
            ErrorCode |= E_DATA;
            data = 255;
        }
	PBlock.byteD[7 + QS/2 * EtsdInfo.blockIntervals + interV] = data;
}


//pete this needs to be tested
//interval = 1-24, Quarter Stream (QS) = 0-?? (FS x 4)  QS 2 = HS1 = FS1, QS 38 = FS10
void saveQS(uint8_t  interV, uint8_t extS, uint8_t  QS, uint32_t data){
	uint16_t  addr = 7 + QS*(EtsdInfo.blockIntervals/2) + (uint_fast8_t)((interV+1)/2);  //pete check this, does it need blockintervals/2.0?
	uint8_t  shft = (interV&01)*4;
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
	PBlock.byteD[addr] = (PBlock.byteD[addr]&(240>>shft)) + ((data&15)<<shft);
}

// saveChan() automagically determines the right type of stream to save data to based on header block info.  
// Also tracks previous values on "counter" streams 
// chan = 0 thru (EtsdInfo.channels-1)
// call with interV = 0 to save registers and set counter variables.
// call with interV > 0 to save data as either counter or gauge based on header block info.
// Note: declaring data as 'int' should allows passing pointers to floats if needed for future upgrades
void saveChan(uint8_t interV, uint8_t chan, uint8_t dataInvalid, int data){
    uint32_t etsdData;
    uint8_t lp, missed, goodUpdate, extS=1, reg=1, AS=0, QS=0;  
    void (*funct_ptr)(uint8_t  interV, uint8_t extS, uint8_t  QS, uint32_t data);  // any float data needs to be converted BEFORE calling function_pointer
    
    if(!(EtsdInfo.channels)){
        ErrorCode = E_NO_ETSD;
        ELog(__func__, 1);
        exit(1);
    }
    for(lp=0; lp<chan; lp++) {
        extS += EXTS_bit(lp);   // Count extended streams up to this channel
        if(AUTOSC_bit(lp))  // Count AutoScale streams up to this channel
            AS++;
        if(REG_bit(lp))     // count saved registers up to this channel
            reg++;
        
        if(10 > ETSD_Type(lp))
            QS += (ETSD_Type(lp)&14)/2;
        else
            QS += 4;  // for AutoScale streams
         
    }
    if( 0xffffffff == data) //all ones indicate error values when saved to anything
        data--;
    if (interV) {   
//Log("saveChan Interval: %d - Channel #: %d - dataInvalid: %d  data = %u ", interV, chan, dataInvalid, data);
        if(!EXTS_bit(chan)) { // channel doesn't use an extended Stream
            extS = 0;       
        }

        if(!CNT_bit(chan) || dataInvalid){    // Gauge channel or invalid data
            if(INT_bit(chan)){
                etsdData = etsdFromSigned(2*ETSD_Type(chan), data);
            } else {
                etsdData = data; 
            }
            //goodUpdate = 0; // ignored if gauge
            missed = 0; // don't update previous values when Absolute
        } else {        // Counter value with good data
            //goodUpdate = 1;
            if (  0xffffffff != lastReading[chan]){ // valid last reading
                missed = missedUpdate[chan] < interV ? missedUpdate[chan]:(interV-1);
                etsdData = (data - lastReading[chan])/(1+missedUpdate[chan]);
            } else {   // last reading has never been valid, nothing to compare to att
                etsdData = 0xffffffff;
                missed = 0;
            }                    
        }
                
//Log("etsdData: %d\n", etsdData); 
        switch(ETSD_Type(chan)){
            case 10:             // AutoScaling
                funct_ptr=saveAutoS;
                extS = AS;
                break;
            case 9:     // Extended Full Stream
            case 8:     // Full Stream
                funct_ptr=saveFS;
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
        if (CNT_bit(chan)){
//            if(goodUpdate){
            if(dataInvalid){
                missedUpdate[chan]++;
                if(!missedUpdate[chan]){     //if missed more than 255 intervals, give up and start over
                    lastReading[chan] = 0xffffffff;
                }
            }else{
                missedUpdate[chan] = 0;
                lastReading[chan] = data; // if valid data = 0xffffffff then lastReading would indicate we've never seen good data
            }
        }
    } else {  // interV = 0, save registers
        if (!dataInvalid && REG_bit(chan)) {
            saveReg(reg, data);
            if (  0xffffffff == lastReading[chan]){  //first valid reading
                lastReading[chan] = data;
                missedUpdate[chan] = 0;
            }
        }
    }    
    ELog(__func__, 0);
}
