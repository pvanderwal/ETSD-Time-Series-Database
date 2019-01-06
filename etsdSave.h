/*************************************************************************
etsdSave.h library for saving data to an ETSD time series database 

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

#ifndef __etsdsave_h__
#define __etsdsave_h__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BLOCKSIZE
#define BLOCKSIZE 512
#endif

const uint32_t ETSD_HEADER = 1146311749 ;   // decimal value of "ETSD"  
// Note: ^epoch time equivalent is April 29, 2006 11:55:49 AM GMT, avoid saving data before that date :-)
 
const uint32_t DATA_INVALID = 0xFFFFFFFF ; // all ones


#define SRC_TYPE(a) (EtsdInfo.source[(a)]&192)
#define SRC_PRIMARY(a)  (!(EtsdInfo.source[(a)]&192)) 
#define SRC_SHM(a)  (64==(EtsdInfo.source[(a)]&192))
#define SRC_CHAN(a) (EtsdInfo.source[(a)]&63)

#define RRD_bit(a) (EtsdInfo.destination[(a)]&128)
#define CNT_bit(a) (EtsdInfo.destination[(a)]&64)
#define REG_bit(a) (EtsdInfo.destination[(a)]&32)
#define INT_bit(a) (EtsdInfo.destination[(a)]&16)  
#define AUTOSC_bit(a) (EtsdInfo.destination[(a)]&8)
#define EXTS_bit(a) (EtsdInfo.destination[(a)]&1)

#define ETSD_Type(a) (EtsdInfo.destination[(a)]&15) 

#define VALID_INTERVALS (PBlock.data[2] & 127 )
extern uint32_t *lastReading;

typedef struct { 
    uint16_t header;
    uint16_t dataStart;     
    uint16_t extStart;
    uint16_t intervalTime;
    uint8_t blockIntervals; // total number of intervals per Block Note: number of bytes per (Full) Stream = BlockIntervals x 2
    uint8_t dataBytes;
    uint8_t channels;       // total channels in ETSD 
    uint8_t relCnt;         // count of "relative" streams    
    uint8_t rrdCnt;
    uint8_t registers;      // number of 'registers' saved to ETSD
    uint8_t labelSize;
    uint8_t *source;        // allocated array of source channels
    uint8_t *destination;   // allocated array of destination channels
    char *fileName;         // ETSD filename
    char *labelBlob;           // blob of labels, allocated if needed
    uint8_t **label;        // allocated array of pointers to channel labels, points to individual label in labels blob, allocated if needed
} ETSD_INFO;

extern ETSD_INFO EtsdInfo;

typedef union {
    uint32_t longD[128];
    uint16_t data[256];
    uint8_t byteD[512];
} PBLOCK ;

extern PBLOCK PBlock;
#define SCALING PBlock.data[3]
#define TIME_STAMP PBlock.longD[0]


//etsdInit returns zero on success or error code.  -11 can't open fName, -10= file header not etsd.
int32_t etsdInit(char *fName, uint8_t loadLabels);

// If saving 'Relative' data to ETSD, you must initialize arrays first
//void etsdInitArrays();

// set ETSD block to 'val'
void etsdBlockClear(uint16_t val);

// set current timestamp on block
void etsdBlockStart();

// write etsd block to disk
// returns zero on success, -1 if can't rotate files, exits if can't save to current file
int32_t etsdCommit(uint8_t interV);

// backs up current etsd file and opens a new one with the current name. Copies the first sector (db info) to new file
// to avoid losing data, execute etsdCommit() before etsdRotate()
// returns zero on success or error code (see above)
int32_t etsdRotate();

// mode 1=Read from beginning, 2=Write from beginning, 3=Append, 4=read from endk, 5=write from end
// returns zero on success or error code (see above)
int32_t etsdRW(char *mode, int32_t sector);

//Convert signed value to 'bits' size etsd format  
uint32_t etsdFromSigned(uint8_t bits, int32_t data);

//--negative address used to address any byte, 0 and positive save "extra data"
void saveBlockData(uint16_t addr, uint8_t data ) ;
// returns "extra data"
uint8_t readBlockData(uint16_t addr);

// Returns a byte from anywhere in the block addressed by addr
uint8_t etsdReadByte(uint16_t addr);

//reg = 1-??,  registers are saved starting at end of Block, working back
void saveReg(uint8_t reg, uint32_t data);

// Auto-Scaling (currently) works on full streams only. Can handle any value up to 524272, any larger values is saved as invalid (all 1's)
// do NOT call when interV = 0
// ASC = Auto-Scaling channel 0-7
void saveAutoS(uint8_t interV, uint8_t ASC, uint8_t QS, uint32_t data);

// For FS/HS/QS if extS = 0: don't save extended data, otherwise indicates which extS stream to use
// do NOT call saveFS/saveHS/saveQS when interV = 0, valid intervals are 1 to (BlockIntervals-1)
// Note: Each FS = 2x HS = 4x QS.   FS#5 = {HS#9, HS#10} = {QS#17, #18, #19, #20}

// Valid Data = 0-65534, or 0-262142 with extS
// Least Significant 16 bits contain stored data, 18 bits with extS
void saveFS(uint8_t interV, uint8_t extS, uint8_t QS, uint32_t data);

// Valid Data = 0-254, or 0-1022 with extS
// Least Significant 8 bits contain stored data, 10 bits with extS
void saveHS(uint8_t  interV, uint8_t extS, uint8_t QS, uint32_t data);

// Valid Data = 0-14, or 0-62 with extS
// Least Significant 4 bits contain stored data, 6 bits with extS
void saveQS(uint8_t  interV, uint8_t extS, uint8_t  QS, uint32_t data);

// normally used to extended (add 2 bits to) a data stream, but can be used alone to store 2 bit data streams.  Only LSbx2 of data is saved
// extS values 1 to (number of extended channels)  
void saveExtS(uint8_t interV, uint8_t extS, uint8_t dummy, uint32_t data);

// saveChan() automagically determines the right type of stream to save data to based on header block info.  
// Also tracks previous values on "relative" streams (make sure to call etsdInitArrays() on program startup)
// valid chan = 0 thru (EtsdInfo.channels-1)
// call with interV = 0 to save registers and reset counter variables.
// call with interV > 0 to save data as either Relative or Absolute based on header block info.
void saveChan(uint8_t interV, uint8_t chan, uint8_t dataInvalid, int data);

#ifdef __cplusplus
}
#endif

#endif
