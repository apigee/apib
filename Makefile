# This makefile just reminds us what tools to build!

BAZ_OPTS=-c opt --copt='-O3'

all: bin/apib bin/apibmon
.PHONY: bin/apib bin/apibmon test bin/testserver

bin/apib: bin
	bazel build $(BAZ_OPTS) //apib
	cp ./bazel-bin/apib/apib ./bin/apib
	chmod u+w ./bin/apib

bin/apibmon: bin
	bazel build $(BAZ_OPTS) //apib:apibmon
	cp ./bazel-bin/apib/apibmon ./bin/apibmon
	chmod u+w ./bin/apibmon

bin/testserver: bin
	bazel build $(BAZ_OPTS) //test:testserver
	cp ./bazel-bin/test/testserver ./bin/testserver
	chmod u+w ./bin/testserver

bin:
	mkdir bin

test:
	bazel test $(BAZ_OPTS) ...
