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

There are three dependencies:

  * APR (Apache Portable Runtime)
  * APR Utils (Additional APR libraries)
  * OpenSSL (for SSL support, which currently is always built)

You need the "devel" versions of these libraries in order to build.

On CentOS, you can install them like this:

yum install apr-devel apr-util-devel openssl-devel

*Configure*: Run "configure" to generate build scripts for the platform

./configure

This should work -- if you get any errors you should email me (greg@brail.org)

*Make*: Build the code

make

(NOTE: On CentOS and Ubuntu, run make with the LDFLAGS attribute set to "-L /lib64 -l pthread" if make fails;
% make "LDFLAGS=-L /lib64 -l pthread"

If this fails to work, export the attribute manually before running make; 
% export $LDFLAGS=-L /lib64 -l pthread")

*Done*: You are done and it should work now.
