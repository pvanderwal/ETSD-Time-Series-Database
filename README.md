# ETSD-Time-Series-Database
A simple and efficient Time-Series database written in C designed for storing sensor data

Preliminary upload.  NOT ready for use yet.

Designed to offer high reliability, each 'Block' of data occupies 1 512byte sector on hard drive, thumb drive or SD card.
Each Block is self contained, so even if directory table is damaged you might be able to recover data by scanning Sectors.

Each block is only writen once which should reduce wear on flash memory.

Note: A 'Channel' is the source of the data, a 'Stream' is the data coming from the Channel

Supports 1 - 63 streams of data in any combination of FullStreams(FS) 16 bits per interval, HalfStreams(HS)8 bits, or QuarterStreams(QS) 4 bits.  Additionally any stream can be extended by 2 additional bits.

At the begining of each block you can also store a 32 bit 'register' on any or all channels
 
Originalluy designed to store data from an Brultech ECM-1240 at 10 second intervals, but redesign to allow flexible stream size and variable intervals.  Currently supports interval times from 1 second to 65,535 seconds (approx 18hrs) 
Currently designed to handle storing unsigned integers, but can store signed integers using a special format and could be extended to handle 24bit and 32bit wide streams as well as floating point values.

If used to store energy data at 10 second intervals, a Full Stream can accurately store Watt-Second data for average power levels up to approx 6,500 watts, an extended full stream (18 bits) can handle average power levels up to approx 26kw.

You can also designate up to 8 autoscaling streams (16 bits only) that can store power levels up to 52kw, but can have small error rates in a given data block, these errors are corrected by saving the 32 bit register in the next block.  
Auto-Scaling is set for the entire block on a stream by stream bsis and will automatically select the scaling needed to store the maximum power level that occurs during that block.
Scaling not required on any channel less than ~6.5kW (240V @ ~27A), zero storage error
2x scaling can handle 13.1kW (240V @ ~54.5A)	up to 1 watt second error per interval`
4x scaling can handle 26.2kW (240V @ ~109A)     up to 2 second error per interval
8x scaling can handle 52kW (240V @ ~218 amps)   up to 4 watt second error interval
