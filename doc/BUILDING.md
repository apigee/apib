# How to Build apib

Sample build instructions for CentOS:

  1. Install git
  2. Download the source
  3. Install dependencies
  4. ./configure
  5. make

## Example

*git*: First, install if you don't already have it in order to get source from Google Code:

    yum install git

*Get Source*: Pull the source from Google Code

    git clone https://code.google.com/p/apib/

*Install Dependencies*: Install dependent packages required to build

*Compiler*

To build, you will need gcc and make -- these are not always installed
by default -- you may need to use "yum" or "apt-get" to install them.

*Libraries*

There are three dependencies:

  * APR (Apache Portable Runtime)
  * APR Utils (Additional APR libraries)
  * OpenSSL (for SSL support, which currently is always built)

You need the "devel" versions of these libraries in order to build.

On CentOS, you can install them like this:

    sudo yum install apr-devel apr-util-devel openssl-devel

On Ubuntu, you would do it like this:

    sudo apt-get install libapr1-dev libaprutil1-dev openssl

*Configure*: Run "configure" to generate build scripts for the platform

./configure

This should work -- if you get any errors you should email me (greg@brail.org)

*Make*: Build the code

make

*Done*: You are done and it should work now.
