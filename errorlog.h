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
// Error Codes 1-16 are Info, 32-512 are warnings are 'Notices' not displayed if LogLvl is less than 2
// error levels between 1024 and 4096 are 'Warnings', generally operator error, 2048 and up are hard errors
#define E_DATA            1     // Data is invalid
#define E_EOF             2     // End of file reached
#define E_BEFORE          4     // Target time is BEFORE ETSD starts
#define E_AFTER           8     // Target time is AFTER ETSD ends
#define E_NOT_FOUND      16     // Target time is not in ETSD
#define E_STARTSTOP     32     // Invalid start/stop time
#define E_DATE           64     // Invalid date format
#define E_ARG           128     // Invalid arguement
//#define E_UNK           256  
//#define E_UNK           512  
#define E_NOT_ETSD     1024     // File header block(0) not from ETSD file
#define E_NOT_TTY      2048     // Device specified is not a TTY device
//#define E_UNK          4096
#define E_TTY_STAT     8192     // Can't get status of tty device
#define E_SHM         16384     // Can't open/create shared memory object
#define E_CANT_WRITE  32768     // Can't open file for writing
#define E_CANT_READ   65536     // Can't open file for reading
#define E_SEEK       131072     // Error seeking sector
#define E_CHECKSUM   262144     // Check Sum error on ECM packet
#define E_TIMEOUT    524288     // Timed out waiting on data from ECM-1240
//#define E_UN        1048576
//#define E_UN        2097152
//#define E_UNK       4194304
//#define E_UNK       8388608
//#define E_UNK      16777216
//#define E_UNK      33554432
//#define E_UNK      67108864   // Unknown Error
//#define E_UNK     134217728     
//#define E_UNK     268435456
//#define E_UNK     536870912
//#define E_UNK     1073741824
#define E_CODING  2147483648    // Unspecified coding error, program shouldn't get to the point that generated this error

//LogLvl  0 = no logging, 1 = minimal error logging, 2 = detailed error logging, 3 = log data output, 4 log data input
extern int8_t LogLvl;
extern uint32_t ErrorCode;

// only call if using a logfile instead of syslog.
void LogSetup(const char *fName);

// cfnm = calling function name + optional text
// Clear = clear ErrorCode before returning unless zero
void ELog(const char *cfnm, uint8_t clear);

// uses printf() formating
void Log(const char *format, ...);

// Logs an array of data in hex format, 16 bytes per line
// "type" is a short (<5 char) description, size is the size of the array in bytes.
void LogBlock(void *array, char *type, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif 
