To run the code you first need to build ./loadbalancer by doing:

$make

(If you want to delete the binaries run $make spotless)

Next, make sure you have an httpserver binary you can start some servers by doing:

$./httpserver ${port} -N ${num_threads} -l ${logfile_name} &

where ${port} is an interger > 7999
where ${num_threads} is an interger
where ${logfile} is a file to be used as the log

-N and -l my be ommited


Now start the load balancer...

./loadbalancer ${port} -N ${num_concurrent_tasks} -R ${connection} ${portS} &

where ${connections} is the number of connections in between healthchecks
where ${num_concurrent_tasks} is an interger.
where ${portS} is the list of ports of the httpservers

-N and -R my be ommited

This servers will fail with concurrent requests to the same resource.

To stop the server on MACOS/LINUX press ctrl and C.