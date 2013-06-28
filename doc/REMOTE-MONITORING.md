# Using apibmon for remote monitoring

## Introduction

apib is capable of gathering remote CPU and memory usage information from a host and including it in the test output. This allows a series of automated tests to include information about resource consumption.

The monitoring is accomplished by the "apibmon" program. 

apibmon takes one argument, which is the TCP port for it to listen on.

## Example

For example, to test the host "test.foo.com", first run apibmon there:

    nohup apibmon 10001 &

Next, run apib with the -M flag to pick up the data:

    apib -d 30 -M test.foo.com:10001 http://test.foo.com/bars/baz

## Testing

apibmon supports a simple text-based protocol, so you may test it using "telnet". It supports three commands:

  * cpu: Display the average CPU usage since the last "cpu" command, or since the session was established, as a real number between 0 and 1.
  * mem: Display the amount of memory currently used as a real number between 0 and 1.
  * bye: Disconnect.

For example:

    $ telnet test.foo.com 10001
    Trying test.foo.com...
    Connected to test.foo.com.
    Escape character is '^]'.
    cpu
    0.00
    cpu
    0.00
    mem
    0.03
    bye
    BYE
    Connection closed by foreign host.

## Notes

apibmon uses the /proc/stat and /proc/meminfo special files to gather statistics. This means that it works on Linux and Linux-like systems such as Cygwin, but not on the Mac.
