# How to Build apib

Sample build instructions for CentOS:

  1. Get the source using git
  2. [Install Bazel](https://docs.bazel.build/versions/0.29.0/install.html)
  3. make

This will work on most Linux platforms. We have also tested it on Mac OS X
and FreeBSD. It also runs on Windows 10 using the "Windows Subsystem for Linux."
We have not tried to build it on native Windows.

## Details

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

## Dependencies

As part of the build, Bazel fetches the third-party code that apib needs.
So, once Bazel is installed you don't need anything else in order to build.
For the interested, that includes:

* [libev](http://software.schmorp.de/pkg/libev.html) for portable asynchronous
I/O and scheduling on all our platforms.
* [BoringSSL](https://boringssl.googlesource.com/boringssl/) Google's port of TLS
that is consistent and easy to build with Bazel
* A Base64 encoder from the Apache project, found in "third_party".

## Testing

    make test

will use Bazel run the tests.
