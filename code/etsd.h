/*************************************************************************
etsd.h basic functions and common definitions
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

#ifndef __etsd_h__
#define __etsd_h__

// #ifdef _WINDOWS
// #define strcasecmp stricmp
// #endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BLOCKSIZE
#define BLOCKSIZE 512
#endif

#define SRC_TYPE(a) ((EtsdInfo.source[(a)]>>6)&3)
//#define SRC_PRIMARY(a)  (!(EtsdInfo.source[(a)]&192)) 
#define SRC_SHM(a)  (64==(EtsdInfo.source[(a)]&192))
#define SRC_CHAN(a) (EtsdInfo.source[(a)]&63)
#define SRC_RESET(a) (PBlock.data[3] |= 1<<(15-a))  // steal an autoscale channel for reset indicator on ECM & SHM, steal 2 to handle 4 sources
#define BLOCK_RESET ((PBlock.data[3] >> 14) & 3)    // Pete only checking two sources right now

#define CHK_RESET (PBlock.data[3]>>14&3)

#define EXT_DB_bit(a) (EtsdInfo.destination[(a)]&128)  // External DB
#define CNT_bit(a) (EtsdInfo.destination[(a)]&64)
#define REG_bit(a) (EtsdInfo.destination[(a)]&32)
#define SIGNED(a) (EtsdInfo.destination[(a)]&16)  
#define AUTOSC(a) (10==EtsdInfo.destination[(a)]&15?1:0)
// Pete #define AUTOSC(a) (15==EtsdInfo.destination[(a)]&15?1:0)
//#define EXTS_bit(a) (EtsdInfo.destination[(a)]&1)
#define EXTS_bit(a) (EtsdInfo.destination[(a)]&1 && 13>(EtsdInfo.destination[(a)]&15))

#define ETSD_Type(a) (EtsdInfo.destination[(a)]&15) 

#define VALID_INTERVALS (PBlock.data[2] & 127 )

#if BLOCKSIZE==512
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 63
#endif
#define QS_SIZE uint8_t QS
#else
#if MAX_CHANNELS>127
#undefine MAX_CHANNELS
#endif
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 127
#define QS_SIZE uint16_t QS
#endif    
#endif

#include <signal.h>

const uint32_t ETSD_HEADER = 1146311749 ;   // decimal value of "ETSD"  
                                            // Note: ^epoch time: April 29, 2006 11:55:49 AM GMT, avoid saving data before that date :-)
 
const uint32_t DATA_INVALID = 0xFFFFFFFF ; // all ones

extern uint32_t *lastReading;

extern volatile sig_atomic_t RotateEtsd;

typedef struct { 
    uint16_t header;
    uint16_t extStart;
    uint16_t intervalTime;
    uint16_t xDataStart;     
    uint8_t xData;          // size of extended data in bytes
    uint8_t blockIntervals; // total number of intervals per Block Note: number of bytes per (Full) Stream = BlockIntervals x 2
    uint8_t channels;       // total # of channels in ETSD 
    uint8_t relCnt;         // count of "relative" streams    
    uint8_t extDBcnt;
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

// mode 1=Read from beginning, 2=Write from beginning, 3=Append, 4=read from endk, 5=write from end
// returns zero on success or error code (see above)
int32_t etsdRW(char *mode, int32_t sector);

#ifdef __cplusplus
}
#endif

#endif
