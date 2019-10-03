# This makefile just reminds us what tools to build!

all: apib apibmon
.PHONY: apib apibmon test

apib:
	bazel build -c opt --copt='-O3' //src:apib
	cp ./bazel-bin/src/apib .
	chmod u+w ./apib

apibmon:
	bazel build -c opt --copt='-O3' //src:apibmon
	cp ./bazel-bin/src/apibmon .
	chmod u+w ./apibmon

test:
	bazel test ...
