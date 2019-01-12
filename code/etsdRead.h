/*************************************************************************
etsdSave.h library for reading, searching, and parsing data from an ETSD time series database 

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

#ifndef __etsdread_h__
#define __etsdread_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "etsdSave.h"


// saveChan() automagically determines the right type of stream to save data to based on header block info.  
// Also tracks previous values on "relatvie" streams (make sure to call etsdInitArrays() on program startup)

// Returns value saved to stream, unless stream value == all ones (Data Invalid).
// If data is invalid, returns zero plus ErrorCode = E_DATA (zero might be valid)
int32_t readChan(uint8_t interV, uint8_t chan);

//Convert 'bits' size etsd format to signed value
int32_t etsdToSigned(uint8_t bits, uint32_t data);

// jumps to sector #<seek> of etsd file and returns timestamp
// If returned value = 0 check ErrorCode 
uint32_t etsdTimeS(uint32_t seek);

// called with epoch Time, returns sector.
// returns Positive value that equals the desired sector(Block) that contains data stored during target Time 
// or zero to indicate error, see errorlog.h for list of error codes
// Note:  Potential problems with this code on 32bit OS starting in the year 2038 (epoch date bug)
uint32_t etsdFindBlock(uint32_t tTime);

#ifdef ALL_SYMBOLS

// normally used to extended (add 2 bits to) a data stream, but can be used alone to store 2 bit data streams.  Only LSbx2 of data is saved
// extS values 1 to (number of extended channels)  
// pete test this
uint8_t readExtS(uint8_t interV, uint8_t extS);

//reg = 1-??,  registers are saved starting at end of Block, working back
uint32_t readReg(uint8_t reg);

// Auto-Scaling (currently) works on full streams only. Can handle any value up to 524272, any larger values is saved as invalid (all 1's)
// ASC = Auto-Scaling channel 0-7
uint32_t readAutoS(uint8_t interV, uint8_t ASC, uint8_t FS);

// extS is ignored on read20(), read24(), and read32()
uint32_t read20(uint8_t interV, uint8_t extS, QS_SIZE);
uint32_t read24(uint8_t interV, uint8_t extS, QS_SIZE);
uint32_t read32(uint8_t interV, uint8_t extS, QS_SIZE);

// For FS/HS/QS extS: 0=don't save extended data, otherwise indicates which extS stream to use
// do NOT call saveFS/saveHS/saveQS when interV = 0, valid intervals are 1 to (BlockIntervals-1)
// Note: Each FS = 2x HS = 4x QS.   FS#5 = {HS#9, HS#10} = {QS#17, #18, #19, #20}

// Valid Data = 0-65534, or 0-262142 with extS
// Least Significant 16 bits contain stored data, 18 bits with extS
uint32_t readFS(uint8_t  interV, uint8_t extS, uint8_t FS);

// Valid Data = 0-254, or 0-1022 with extS
// Least Significant 8 bits contain stored data, 10 bits with extS
uint32_t readHS(uint8_t interV, uint8_t extS, uint8_t HS);

// Valid Data = 0-14, or 0-62 with extS
// Least Significant 4 bits contain stored data, 6 bits with extS
uint32_t readQS(uint8_t  interV, uint8_t extS, QS_SIZE);
#endif

#ifdef __cplusplus
}
#endif

#endif

