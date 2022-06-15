#!/bin/bash

# https://developer.x-plane.com/article/building-and-installing-plugins/
cd `dirname $0`/..
set -x
g++ -fPIC -Wno-format-overflow -Wno-format-truncation -DXPLM301 -DXPLM300 -DXPLM210 -DXPLM200 -DLIN -ISDK/CHeaders/XPLM XTextureExtractor.cpp XTextureExtractorNetwork.cpp lodepng/lodepng.cpp -shared -rdynamic -nodefaultlibs -undefined_warning -lGL -lGLU -o Plugin-XTextureExtractor-x64-Release/64/lin.xpl
