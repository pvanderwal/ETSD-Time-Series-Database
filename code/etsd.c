/*************************************************************************
 etsd.c base library with common functions used throughout the ETSD time series database 

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
    
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "etsd.h"
#include "errorlog.h"

PBLOCK PBlock;
ETSD_INFO EtsdInfo;
uint32_t *LastReading;
uint8_t *MissedUpdate;;

// send 'kill -SIGUSR1 <process id>' to rotate ETSD File at the end of the current block (when saving data)
// send 'kill -SIGUSR2 <process id>' to reload configuration file after current 'interval'
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
    if (ETSD_HEADER != PBlock.longD[0]){ // check to make sure block starts with "ETSD"
        ErrorCode |= E_NOT_ETSD; //error not etsd file
        ELog(__func__, 0);
        return -1;
    }
    EtsdInfo.header = PBlock.data[2] & 65408;  	//grab the MSbx9
    EtsdInfo.blockIntervals = (PBlock.data[2] >> 7) & 127;
    EtsdInfo.channels = PBlock.data[2] & 127;    // etsd ver 1.0 supports 127 channels max
    EtsdInfo.intervalTime = PBlock.data[3];     // ~18.2 hours maximum interval.
    EtsdInfo.labelSize = PBlock.byteD[8];
    EtsdInfo.xDataSize = PBlock.byteD[9];
    
    EtsdInfo.source=(uint8_t*)malloc(EtsdInfo.channels);
    EtsdInfo.destination=(uint8_t*)malloc(EtsdInfo.channels);

    
    for(lp=0;lp<EtsdInfo.channels; lp++){
        EtsdInfo.source[lp] = PBlock.byteD[lp*2 + 10];  
        EtsdInfo.destination[lp] = PBlock.byteD[lp*2 + 11];  
        if (ETSD_TYPE(lp)){  // if saving to etsd
            if (13> ETSD_TYPE(lp)){
                streams += ETSD_TYPE(lp)&14; //drop the last bit
                if (EXTS_BIT(lp)){  // is this an EXTended Stream (+2bits) ?
                    extSCnt++;
                }
            } else {
                if (13== ETSD_TYPE(lp)){
                    streams +=16;
                } else {
                    streams +=8;
                }
            }
            if (REG_BIT(lp)){
                EtsdInfo.registers++;
            }
        }
        if (EDO_BIT(lp)){  // If saving to RRD
            EtsdInfo.edoCnt++;
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
    } else {
        EtsdInfo.label=NULL;
    }

    EtsdInfo.extStart = 8.75 + EtsdInfo.blockIntervals * streams/4.0; 
    EtsdInfo.xDataStart =  EtsdInfo.extStart + extSCnt/4.0 + 0.75;
//    etsdBlockClear(0xFFFF);

// initialize LastReading and MissedUpdate; arrays
    if (EtsdInfo.channels){
        LastReading = (uint32_t*)malloc(EtsdInfo.channels*sizeof(uint32_t));
        MissedUpdate = (uint8_t*)malloc(EtsdInfo.channels*sizeof(uint8_t));
        for(lp=0;lp<EtsdInfo.channels;lp++){
            LastReading[lp] = DATA_INVALID;   // set all old readings to DATA_INVALID (0xFFFFFFFF)
            MissedUpdate[lp] = 0;       // 0 = no missed updates
        }
    } 
    return 0;
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


