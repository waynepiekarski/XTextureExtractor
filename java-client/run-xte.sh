#!/bin/bash

cd `dirname $0`
javac src/net/waynepiekarski/XTextureExtractor.java || { echo "Failed to compile"; exit 1; }
java -classpath src/ net.waynepiekarski.XTextureExtractor $@
exit $?
