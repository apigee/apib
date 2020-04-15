# apib: API Bench

This is a tool that makes it easy to run performance tests of HTTP API servers. It 
can be built on most Linux platforms, plus Mac OS X and FreeBSD.

## Status

apib has been fairly stable for a little while now. Please submit issues
or pull requests if you find that you'd like it to do more within reason!

## Current Version

1.2.1.

## Usage

Running apib can be as simple as:

    apib -c 100 -d 60 http://test.example.com

The command above will hammer "test.example.com" as fast as it can for up to
60 seconds using 100 concurrent network connections. 

## Installation

On the Mac, you can now install via [Homebrew](http://brew.sh/):

    brew install apib
    
Otherwise, you can [build it yourself from source](./doc/BUILDING.md).

See additional documentation for more:

* [Running](./doc/RUNNING.md): How to run apib
* [Building](./doc/BUILDING.md): How to build it from source
* [Remote Montitoring](./doc/REMOTE-MONITORING.md): How to remotely monitor servers under test

## Design

apib has most of the features of Apache Bench (ab), but is also intended as
a more modern replacement. In particular, it supports:

* Proper HTTP 1.1 support including keep-alives and chunked encoding
* Ability to spawn multiple I/O threads to take advantage of multiple
  CPU cores
* Support for POST and PUT of large objects
* Support for OAuth 1.0 signatures
* Ability to output results to a file so they may be automated
* Remote CPU monitoring

In addition, like "ab," it also supports:

* A simple command-line interface
* Few dependencies, so it may be easily built and deployed
* Non-blocking I/O for high concurrency

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
