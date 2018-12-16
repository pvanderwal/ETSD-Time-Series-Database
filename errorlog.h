/*************************************************************************
 errorLog.h library ETSD time series database 

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


#ifndef __errorlog_h__
#define __errorlog_h__

#ifdef __cplusplus
extern "C" {
#endif

// Error Codes
#define E_EOF       1           // End of file reached
#define E_CHECKSUM  2           // Check Sum error on ECM packet
#define E_TIMEOUT   4           // Timed out waiting on data from ECM-1240
#define E_NOT_TTY   8           // Device specified is not a TTY device
#define E_TTY_STAT  16          // Can't get status of tty device
#define E_SHM       32          // Can't open/create shared memory object
#define E_NOT_ETSD  64          // File header block(0) not from ETSD file
#define E_BEFORE   128          // Target time is BEFORE ETSD starts
#define E_AFTER         256     // Target  time is AFTER ETSD ends
#define E_CANT_READ     512     // Can't open file for reading
#define E_CANT_WRITE    1024    // Can't open file for writing
#define E_SEEK          2048    // Error seeking sector
#define E_CODING        4096    // Unspecified coding error, program shouldn't get to the point that stores this error code
#define E_DATE          8192    // Invalid Date format


extern int8_t LogLvl;
extern uint32_t ErrorCode;

// only need to call if using a logfile instead of syslog.
void LogSetup(const char *fName);

// cfnm = calling function name + format string
// Clear 1 = zero ErrorCode before returning
// Returns current timestamp
void ELog(const char *cfnm, uint8_t clear);

// uses printf() formating
void Log(const char *format, ...);

void LogBlock(void *array, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif