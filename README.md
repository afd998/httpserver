To run the code you first need to build ./httpserver by doing:

$make

(If you want to delete the binaries run $make clean)

Next, you can start the server by doing:

$./httpserver ${port} -N ${num_threads} -l ${logfile_name}


where ${port} is an interger > 7999
where ${num_threads} is an interger
where ${logfile} is a file to be used as the log

-N ${num_threads} and -l ${logfile_name} my be ommited


This server will fail with concurrent requests to the same resource.

To stop the server on MACOS/LINUX press ctrl and C.