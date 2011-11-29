#!/bin/sh

rm -f clouddriver.tar.gz
tar cf clouddriver.tar *.py *.yaml *.sh
gzip clouddriver.tar
