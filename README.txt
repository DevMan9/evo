Objective: This project is focused on using evolution as a 
possible method for software development.

Getting Started: It's a little rough around the edges so so I'll
attempt to walk you through getting started.

From the directory this README is found in, run "make"
This will compile the sentinel. 
I have tested this on modern OSX and hardware (12/28/24)
as well as Raspberry Pi OS (32-bit on a Pi Zero W) (same date).

You'll have to manually compile the specimen using:
"gcc src2/sum.c -o specimens/progenitor"
It's important to put the compiled specimen in IT'S OWN directory.
The sentinel wants a directory to work in and,
due to the nature of evolution, saftey of other files 
in the directory cannot be guaranteed.

Once you have everything compiled, 
you can verify that things are working correctly.

running "./specimens/progenitor" will appear to lock up your terminal.
But in reality it's simply waiting for input which has been redirected to stdin.
If you type in 3 or more characters and hit enter,
you should expect to see the third byte that you
submitted returned through stdout.

If you run "./specimens/progenitor [any additional argument]"
It will instead produce a copy of itself with the name "^[0-9A-F]{8}$"
Or in layman's terms, something that looks like this: "61F0AC2B"

To run the sentinel use:
"./bin/main /full/path/to/specimens [number]"
This runs the exe that "make" produced.  
It will work in the directory you specify.
I reccomend using the "specimens" directory.
[number] is the number of iterations the sentinel will run.
The sentinel will keep you updated with time elapsed for specified operations.
When it is complete, it will report the stats of the final iteration.

Rate is the success rate of the specimen for the given test.
Right now it's hard coded to test for addition.

Rep is an ordered cumulative weighted list used to figure out which specimen
should be reproduced. Basically a random number is picked between [0,1]
and the specimen which is closest without going over is selected.

Kill is the same, but for killing.
