# apib: API Bench

This is a tool that makes it easy to test API servers. It is supported for Unix
systems that include the Apache Portable Runtime and OpenSSL. That includes implementations
of Linux, Windows under Cygwin, and Apple OS X.

## Status

apib has been fairly stable for a little while now. Please submit issues
or pull requests if you find that you'd like it to do more within reason!

## Current Version

1.0.

## Usage:

Running apib can be as simple as:

    apib -c 100 -d 60 http://test.example.com

The command above will hammer "test.example.com" as fast as it can for up to
60 seconds using 100 concurrent network connections. 

See additional documentation for more:

* [Running](./doc/RUNNING.md): How to run apib
* [Building](./doc/BUILDING.md): How to build it from source
* [Remote Montitoring](./doc/REMOTE-MONITORING.md): How to remotely monitor servers under test

## Design

apib is intended
to have many of the features of Apache Bench (ab), but is also intended as
a more modern replacement. In particular, it will support:

* Proper HTTP 1.1 support including keep-alives
* Ability to spawn multiple I/O threads to take advantage of multiple
  CPU cores
* Support for POST and PUT of large objects
* Support for templates so that requests may vary
* Support for OAuth 1.0 and 2.0 signatures
* Ability to output results to a file so they may be automated
* Possibily a "graphical mode" using curses so it's sexy

However, like "ab," it will also support:

* A very simple command-line interface
* Very few dependencies, so it may be easily built and deployed
* Non-blocking I/O for high concurrency

## Dependencies:

* apr -- makes it more portable to Linux, Solaris, Macintosh I think
* openssl -- because we need SSL

Any reasonable platform will have both of these.

## Implementation:

1. Spawn one I/O thread per CPU (configured by user)
2. Allocate a subset of connections to each, and in each:
3. Start event loop
4. If total number of connections < C, spawn a connection
5. Execute HTTP state machine -- connecting, sending, receiving, sending
6. When connections close, replace them to maintain C connections open
7. Record results in a shared area
8. Back in main thread, report on shared results periodically
9. Time in main thread and signal workers to stop eventually
10. Report to screen and to file
