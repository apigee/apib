# How to Build apib

apib can be built in two ways:

  1. Using Bazel, which takes care of fetching and building all the dependencies
  2. Using cmake, which uses system-provided libraries for some things

If you just want to build and run locally, then Bazel, once installed,
makes the process quick.

Otherwise, cmake builds fewer things. Cmake is what we use to build
for Homebrew and someday for other repositories.

## Building using Bazel

In short, this will build the main executable:

  1. Get the source using git
  2. [Install Bazel](https://docs.bazel.build/versions/0.29.0/install.html)
  3. bazel build -c opt //apib //apib:apibmon
  4. The binaries will be in ./bazel-bin/apib/apib and ./bazel-bin/apib/apibmon

This will work on most Linux platforms. We have also tested it on Mac OS X
and FreeBSD. It also runs on Windows 10 using the "Windows Subsystem for Linux."
We have not tried to build it on native Windows.

### Details

apib builds using [Bazel](https://www.bazel.build/). This gives us a consistent build
environment on many platforms. Bazel also 

apib requires version 0.28 or higher of Bazel. You can check the Bazel home
page for installation instructions. Specifically:

* Some Linux distributions have Bazel in their package repos
* Otherwise, the Bazel installation instructions link to an installer
* On the Mac, Bazel is available via Homebrew.
* On FreeBSD, Bazel is available in "ports." (However, sometimes an older
version is available without doing further work to build the port for
a more recent version.)

### Dependencies

As part of the build, Bazel fetches the third-party code that apib needs.
So, once Bazel is installed you don't need anything else in order to build.
For the interested, that includes:

* [libev](http://software.schmorp.de/pkg/libev.html) for portable asynchronous
I/O and scheduling on all our platforms.
* [BoringSSL](https://boringssl.googlesource.com/boringssl/) Google's port of TLS
that is consistent and easy to build with Bazel
* A Base64 encoder from the Apache project, found in "third_party".
* The "Node.js HTTP parser," also in third_party.

### Testing

    bazel test ...

will use Bazel run the tests.

## Building using CMake

The CMake build uses the versions of libev and openssl found
on the system. That means they need to be installed before
apib can be built. Specifically:

* OpenSSL 1.1 or higher must be installed
* libev's headers and libraries must be installed

Once installed, the Cmake build is pretty simple:

1. mkdir release
2. cd release
3. cmake .. -DCMAKE_BUILD_TYPE=Release
4. make apib apibmon

Hint: If you are impatient, try installing "ninja".
Then replace the last two commands:

* cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
* ninja apib apibmon

### Finding Dependencies

If that didn't work, then depending on the platform,
you may need to set any of four variables:

* EXTRA_INCLUDE_DIR: An additional path to find include directories
* EXTRA_LIB_DIR: Same for libraries
* OPENSSL_INCLUDE_DIR: An additional extra directory that will go before the rest
* OPENSSL_LIB_DIR: Same for libraries

For example, on my Mac Homebrew is installed in /opt/homebrew, and
furthermore OpenSSL 1.1 is in a weird place. So I set:

    export EXTRA_INCLUDE_DIR=/opt/homebrew/include
    export EXTRA_LIB_DIR=/opt/homebrew/lib
    export OPENSSL_INCLUDE_DIR=/opt/homebrew/opt/openssl@1.1/include
    export OPENSSL_LIB_DIR=/opt/homebrew/opt/openssl@1.1/lib

Similarly, on FreeBSD you may need to set the first two to /usr/local/include and
/usr/local/lib. 

(You see? If you had used Bazel you wouldn't have had to deal with any of this.)

### Troubleshooting the CMake Build

If "ev.h" is not found, then EXTRA_INCLUDE_DIR must be set to where it's found.
Or it may not be installed yet.

Similarly if "ssl.h" is not found, the similar problem exists for OpenSSL.

If something like "_sk_push" is not found at link time, then either
OPENSSL_LIB_DIR is not set to find the right library, or there is a 
mismatch between the OpenSSL version found in the OPENSSL_INCLUDE_DIR
and OPENSSL_LIB_DIR directories.

Finally, if functions like "TLS_client_method" can't be found, then 
you have selected the wrong version of OpenSSL. Install OpenSSL 1.1,
and use the variables described above to point the build to it.
It may not be in one of the usual locations, and the include and
library files that come up for "ssl.h" and "libssl" will be OpenSSL
1.0, which is not what we want.

### Testing

1. cd release
2. make
3. make test
