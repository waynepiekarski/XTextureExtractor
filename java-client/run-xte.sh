#!/bin/bash

cd `dirname $0`
javac src/net/waynepiekarski/XTextureExtractor.java || { echo "Failed to compile"; exit 1; }
# Replace $@ with the IP address of your X-Plane instance, plus any extra flags
# The syntax is: <hostname> [--fullscreen] [--windowN] [--geometry=X,Y,W,H]
# An example would be to replace $@ with this for window #1 at 50,50 with dimensions 512x512:
# 192.168.99.88 --window1 --geometry=50,50,512,512
java -classpath src/ net.waynepiekarski.XTextureExtractor $@
exit $?
