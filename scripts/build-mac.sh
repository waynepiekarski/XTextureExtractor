#!/bin/bash

# https://developer.x-plane.com/article/building-and-installing-plugins/
cd `dirname $0`/..
set -x
g++ -flat_namespace -undefined suppress -std=c++17 -fPIC -DXPLM301 -DXPLM300 -DXPLM210 -DXPLM200 -DAPL -DGL_SILENCE_DEPRECATION -ISDK/CHeaders/XPLM XTextureExtractor.cpp XTextureExtractorNetwork.cpp lodepng/lodepng.cpp -shared -rdynamic -nodefaultlibs -framework OpenGL -FSDK/Libraries/Mac -framework XPLM -framework XPWidgets -o Plugin-XTextureExtractor-x64-Release/64/mac.xpl
