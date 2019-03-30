# ETSD-Time-Series-Database
A simple and efficient Time-Series database written in C designed for storing energy/IOT sensor data.  Originally designed to store watt-second data at 10 second intervals, but is much more flexible now.

Updated to use 'plugin' shared objects for data sources (up to 4 separate source plugins), external database, and source for "extra data".  See plugin_api.txt for details.

ETSD is designed to offer high reliability and reduce wear on flash memory drives (SD cards, thumb drives, etc.)
Each 'Block' of data occupies 1x 512 byte sector on hard drive, thumb drive or SD card.  
Data blocks are only writen once and never changed by ETSD, this should reduce wear on flash memory drives.
Each Block is individually time stamped and self contained, so even if directory table is damaged, if the drive is still readable you should be able to recover the database by scanning individual sectors.

Originally designed to store data from an Brultech ECM-1240 at 10 second intervals, but redesign to allow flexible stream size and variable intervals.  Currently supports interval times from 1 second to 65,535 seconds (approx 18hrs) 
Currently designed to handle storing unsigned integers, but can store signed integers using a special storage format and could be extended to handle 24bit and 32bit wide streams as well as floating point values.

Note: A 'Channel' is the source of the data, a 'Stream' is the data coming from the Channel

Supports 1 - 100 streams of data in any combination of stream widths (2-24 bits and/or 32 bits)

Note: storage efficiency is poor if using less than 4 streams with 16bits each (less than 64 bits total per interval).

Streams types can either be 'counter' or 'gage'.  Counter streams increment and eventually roll over, like an odometer on a car.  Gage type streams are like a speedometer where there is not necessarily any relationship between one reading and the next.
ETSD offers several data reduction storage formats for counter streams depending on how much you expect the value to change between one interval and the next.  ETSD only works with counter streams that continuously increment, it can't handle counter streams that increment/decrement.  If you need to handle data that can decrement as well as increment, then store it as a 'gage' type.

Counter streams use a 32bit source value and can reduce it to any value between 2 and 24 bits depending on the expected difference between one interval and the next

To improve long term accuracy with counter streams, at the begining of each block you can also store the current 32 bit source value.
 

If used to store energy data at 10 second intervals, a FullStream(16 bits) can accurately store Watt-Second data for average power levels up to approx 6.5 kilowatts, an extended full stream (18 bits) can handle average power levels up to approx 26kw.

You can also designate up to 8 autoscaling streams (16 bits only) that can store power levels up to 52kw with small error rates in a given data block, these errors are can be corrected by saving the full 32 bit register once block.  
Auto-Scaling is set for the entire block on a stream by stream basis and will automatically select the scaling needed to store the maximum power level that occurs for each stream during that block.
<pre>
no scaling can handle 0 - ~6.5kW (240V @ ~27A)  zero storage error
2x scaling can handle 13.1kW (240V @ ~54.5A)    up to 1 watt second error per interval, corrected on next block
4x scaling can handle 26.2kW (240V @ ~109A)     up to 2 watt second error per interval, corrected on next block
8x scaling can handle 52kW (240V @ ~218 amps)   up to 4 watt second error per interval, corrected on next block
</pre>

Currently supports the following stream sizes:
<pre>
   Stream Type                  Bits   Notes
   15 = AutoScale               (16)   up to 7 channels are available and automatically allocated 
                                      ^(ONLY works with unsigned Ints!!!)  
   14 = Double Stream           (32)   single/double precision floats can be converted by user and saved to double stream (32bit) 
   13 = 1/2 Precision float     (16)  (planned but not yet implemented)
   12 = Large Stream            (24) 
  *11 = Extended 20bit stream   (22) 
  *10 = 20bit stream            (20) 
    9 = Extended Full Stream    (18) 
    8 = Full Stream             (16) 
   *7 = Extended Short stream   (14) 
   *6 = Short stream            (12) 
    5 = Extended Half Stream    (10) 
    4 = Half Stream             ( 8) 
   *3 = Extended Quarter Stream ( 6) 
   *2 = Quarter Stream          ( 4) 
    1 = Two bit stream          ( 2) 
    0 = don't save to ETSD
*These types combined a Half or Full stream with a Quarter stream.  To conserve space, 'quarter' streams are placed after all other types    

 </pre>
