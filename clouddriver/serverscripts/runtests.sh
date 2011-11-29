#!/bin/sh

rm -rf autoperf
mkdir autoperf

rm -f /tmp/apib.tar.gz
s3cmd get s3://autoperf/apib-64.tar.gz /tmp/apib.tar.gz
rm -f /tmp/clouddriver.tar.gz
s3cmd get s3://autoperf/clouddriver.tar.gz /tmp/clouddriver.tar.gz

cd autoperf

mkdir tmp
cd tmp
tar xzf /tmp/apib.tar.gz
mv apib/* ..
cd ..

tar xzf /tmp/clouddriver.tar.gz

python launchtest.py
