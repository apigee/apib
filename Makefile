# This makefile just reminds us what tools to build!

BAZ_OPTS=-c opt --copt='-O3'

all: apib apibmon
.PHONY: apib apibmon test testserver

apib:
	bazel build $(BAZ_OPTS) //src:apib
	cp ./bazel-bin/src/apib .
	chmod u+w ./apib

apibmon:
	bazel build $(BAZ_OPTS) //src:apibmon
	cp ./bazel-bin/src/apibmon .
	chmod u+w ./apibmon

testserver:
	bazel build $(BAZ_OPTS) //test:testserver
	cp ./bazel-bin/test/testserver .
	chmod u+w ./testserver

test:
	bazel test $(BAZ_OPTS) ...
