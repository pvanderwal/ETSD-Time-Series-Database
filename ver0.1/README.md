# ETSD-Time-Series-Database
A simple and efficient Time-Series database written in C designed for storing sensor data.  Originally designed to store watt-second data at 10 second intervals, but is much more flexible now.

Preliminary upload, not finished yet, but functional.  etsdSave & etsdRead libraries are working.  You can use etsdCmd to create, examine, and recover ETSD files. Simple queries (Average,Minimum, Maximum and Total) functions work, but total values may start/end before/after the specified times (working on it).  
The ETSD Data Director(edd) daemon works with the ECM-1240 data collector, I'm currently using it to log data on my system. 

ETSD is designed to offer high reliability, and reduce wear on flash memory drives (SD cards, thumb drives, etc.)
Each 'Block' of data occupies 1x 512 byte sector on hard drive, thumb drive or SD card.
Each Block is self contained, so even if directory table is damaged you might be able to recover data by scanning Sectors.

Each block is only writen once which should reduce wear on flash memory drives.

Note: A 'Channel' is the source of the data, a 'Stream' is the data coming from the Channel

Supports 1 - 100 streams of data in any combination of FullStreams(FS) 16 bits per interval, HalfStreams(HS)8 bits, or QuarterStreams(QS) 4 bits.  Additionally any stream can be extended by 2 additional bits.
However, storage efficiency is poor if using less than 4 fullstreams.
1024/2048 byte blocks could be supported in the future and would allow up to 127 channels.

To improve long term accuracy, at the begining of each block you can also store a 32 bit value (counter) for any or all channels.
 
Originally designed to store data from an Brultech ECM-1240 at 10 second intervals, but redesign to allow flexible stream size and variable intervals.  Currently supports interval times from 1 second to 65,535 seconds (approx 18hrs) 
Currently designed to handle storing unsigned integers, but can store signed integers using a special format and could be extended to handle 24bit and 32bit wide streams as well as floating point values.

If used to store energy data at 10 second intervals, a Full Stream can accurately store Watt-Second data for average power levels up to approx 6.5 kilo-watts, an extended full stream (18 bits) can handle average power levels up to approx 26kw.

You can also designate up to 8 autoscaling streams (16 bits only) that can store power levels up to 52kw, but can have small error rates in a given data block, these errors are can be corrected by saving the 32 bit register in the next block.  
Auto-Scaling is set for the entire block on a stream by stream basis and will automatically select the scaling needed to store the maximum power level that occurs during that block.
<pre>
no scaling can handle 0 - ~6.5kW (240V @ ~27A)  zero storage error
2x scaling can handle 13.1kW (240V @ ~54.5A)    up to 1 watt second error per interval, corrected on next block
4x scaling can handle 26.2kW (240V @ ~109A)     up to 2 watt second error per interval, corrected on next block
8x scaling can handle 52kW (240V @ ~218 amps)   up to 4 watt second error per interval, corrected on next block
</pre>

Currently supports the following stream sizes:
<pre>  Type                          Bits   Notes
   10 = AutoScale               (16)   up to 8 channels are available and automatically allocated      
    9 = Extended Full Stream    (18)               (Autoscaling ONLY works with unsigned Ints!!!)
    8 = Full Stream             (16)  
    5 = Extended Half Stream    (10)
    4 = Half Stream             (8)
    3 = Extended Quarter Stream (6)
    2 = Quarter Stream          (4)
    1 = Two bit stream          (2)    
    0 = don't save to ETSD
 </pre>
 
 Note:  I'm planning on adding 20 bit, 24 bit, and 32 bit streams soon. 
