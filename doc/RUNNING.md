# How to run apib.

## Introduction

apib is a small tool for rapidly testing HTTP workloads. It is especially useful when testing REST-style APIs, and was intended to be similar in many ways to the popular tool "ab," hence the name.

### Differences from ab

"ab" (Apache Bench) is a widely-used and readily-available benchmarking tool for HTTP. It is fast and easy to use. apib works in a similar way, but takes a few different approaches for, we think, better results:

  * apib supports HTTP 1.1 only, with configurable keep-alive (ab supports only 1.0). Today all clients and servers used on the Internet support HTTP 1.1.
  * apib can output to the screen or to a CSV file, suitable for writing automated tests.
  * apib can monitor CPU usage locally and remotely on Linux and Linux-like platforms.
  * apib has built-in support for OAuth 1.0 signatures.
  * apib does only what is needed to be done to test the HTTP 1.1 protocol and nothing else. It does not look at or validate the HTTP responses, for instance. In general it is fast and can handle many concurrent connections, so a single client can drive a server to the breaking point.
  * apib is scheduled by time, not number of requests -- we feel this is a much easier way to write automated tests.
  * apib does local and remote monitoring of CPU and memory usage, allowing a series of tests to be automated, including usage statistics.
  * apib creates one thread per CPU, with very little shared state (a few counters) between them. This means that if a workload is CPU-intensive, such as HTTPS benchmarking with large request and response bodies, apib can take advantage of all the CPUs without overhead.
  * like "ab", apib uses non-blocking I/O so it can handle many concurrent connections.

## Simple Example

Like many programs, "apib" prints out a help message, which you can also retrieve using "-h".

To test the performance of a single URL:

    apib http://api.foo.com/bars/1

This will query the URL above using an HTTP GET over and over, one at a time, for 60 seconds.

While the test runs, every five seconds apib wil output the run time so far, the throughput over the last five seconds, and the CPU usage on the client (if apib is able to figure it out). At the end of the test, apib will output more information such as latency calculations, error counts, number of sockets opened, and other data.

In order to test more load, we will want to send more concurrent requests and control the duration of the test. For instance, this command will run the test for only 30 seconds, but it will open 100 connections to the server and use them concurrently:

    apib -d 30 -c 100 http://api.foo.com/bars/1

From there, we use other options as described below.

For instance, this command tests the same API, but using an HTTP PUT rather than a GET, sending the contents of the file "test.xml", setting the Content-Type header to indicate that the content is XML, running for 60 seconds, "warming up" for 60 seconds before that (during which time tests are run but no measurements are taken towards the final result) and disabling HTTP keep-alive:

    apib -x PUT -f test.xml -t application/xml \
      -w 60 -d 60 -k 0 -c 100 http://api.foo.com/bars/1

apib can also read a file of URLs and randomly test each URL in the file. This is accomplished by using the notation "@filename" instead of a URL. For example, the following will randomly test all the urls in the file "urls" below for 30 seconds.

    apib -d 30 -c 100 @urls

## Parameters

### Controlling the amount of load

By default apib tests using one connection at a time with no break in between, using HTTP 1.1 and "keep-alive". These parameters control that.

-c: Number of concurrent requests to make. Defaults to 1. Your client system will need to be able to open at least this many connections to the server, so see "Notes" below on how to tune this. Defaults to 1.

-W: Think time, in milliseconds. By default apib uses no think time and hits the API without relief. For certain types of workloads, it makes sense to introduce a small think time. This will of course reduce the amount of throughput reported but is helpful for measuring latency over a long time period.

-k: Control the amount of time that connections are kept alive by the client. By default apib never closes client connections until the end of the test. This switch controls how long apib wil keep the connection open, but currently the only supported switch is "0". So, "-k 0" disables keep-alive entirely, and any other setting leaves it enabled. In addition, apib follows the HTTP 1.1 protocol, and will automatically re-establish connections with the server if the server closes the connection prematurely, and apib will follow the HTTP "Connection" header and close the connection if the server requests it.

-K: Control the number of I/O threads that apib wil use. This is *not* the same as the "-c" argument that controls test concurrency. This should be set to the number of CPU cores on the test client machine. On Linux platforms apib uses the /proc/cpuinfo file to count CPUs, and on other platforms it defaults to 1.

### Controlling the length of the test

By default an apib test runs for 60 seconds. These parameters control the duration.

-d: Duration fo the test, in seconds. Defaults to 60 seconds.

-w: Warm up time, in seconds. During the warm up time, the test is run and the throughput is printed to the screen every five seconds, but no data is accumulated towards the final set of statistics. Defaults to no warm-up time.

-1: Just send one request. This is useful for getting the test off the ground and ensuring it will work. This overrides the "-d" and "-w" flags.

### Controlling the Request Content

By default apib performs a GET on the URL or URLs provided. These parameters control what is sent.

-f: Send the contents of the file in the body of each request. Unless the -x parameter is set, apib will send POST requests rather than GET requests if this parameter is set.

-H: Add an HTTP header line. The argument must be in the format {{{ Header Name: Header Value }}} as used in the HTTP spec. Multiple -H options may be specified.

-O: Add an OAuth 1.0 signature to each request. The arguments to this parameter must be in the format {{{ consumer key:consumer secret:access token:token secret }}}. The constructed signature will take into account both key / secret pairs. In addition, if only the first pair (consumer key / secret) is specified then apib will only construct the signature using them.

-t: Set the "Content-Type" header. {{{ -T text/foo }}} is equivalent to the argument {{{ -H "Content-Type: text/foo" }}} The default is "application/octet-stream".

-u: Add an "Authorization" header based on the HTTP Basic authentication scheme. The value of this parameter must be in the format {{{ username:password }}}.

-x: Set the HTTP verb ("method") for the request. The default is "GET" unless the -f argument is used, in which case the default is "POST".

### Controlling Output

-S: Specify CSV output. The result of the entire test run will be a single line of CSV. Use the -T argument to see the header fields.

-T: Output a single CSV header line that corresponds to the CSV output from the "-S" option, and then exit. Using this argument, a test script can first run {{{ apib -T }}} to write the CSV header, then run additional apib runs with the -S option included in order to fill out the test results.

-N: Specify the name of the test, which will be included in the CSV output. The default is to have no name.

### Remote Monitoring

-M: Gather remote CPU and memory usage statistics from a remote host running "apibmon". The argument must be in the format {{{ host:port }}} describing the host name and port number of a host running "apibmon".

-X: The same as -M, but it supports a second host so that remote monitoring of two hosts may be included in the test output.

## Notes

### Connection Handling

In order to test with many concurrent clients, apib must be able to open a sufficient number of file descriptors. apib will attempt to check the current limits and if they are insufficient it will print an error message and exit.

On modern Linux systems, file descriptor limits are set in the file "/etc/security/limits.conf". For extremely large workloads (hundreds of thousands of descriptors) it may also be necessary to change some of the kernel parameters.

### Connection Handling and Keep-Alive

When keep-alive is disabled using the "-k 0" flag, many client systems will rapidly run out of file descriptors as sockets in "timed wait" state pile up. This means that with keep-alive is disabled and many concurrent connections are opened, the test will quickly begin to fail because the system is opening new file descriptors faster than they are timing out.

On Linux, the timed-wait behavior may be modified, which allows apib to test many thousands of concurrent connections with keep-alive disabled. This is done by changing the kernel parameters in "/proc/sys/net/ipv4/tcp_tw_reuse" and "/proc/sys/net/ipv4/tcp_tw_recycle". To change them permanently, you must edit /etc/sysconfig.conf and reboot, or you may change it manually like this:

    echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse
    echo 1 > /proc/sys/net/ipv4/tcp_tw_recycle

## CPU Monitoring

CPU and memory usage is monitored using the /proc/stat and /proc/meminfo virtual files. It works on Linux and also on systems like Cygwin that support these files. 

On the Mac and other platforms that do not support these files, CPU usage will be reported as zero.

