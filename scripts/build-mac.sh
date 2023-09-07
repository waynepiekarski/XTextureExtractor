#!/bin/bash

# https://developer.x-plane.com/article/building-and-installing-plugins/
cd `dirname $0`/..
set -x
clang++ -arch x86_64 -arch arm64 \
  -std=c++17 -fPIC -Wno-deprecated-declarations \
  -DXPLM301 -DXPLM300 -DXPLM210 -DXPLM200 -DAPL -DGL_SILENCE_DEPRECATION \
  -ISDK/CHeaders/XPLM XTextureExtractor.cpp XTextureExtractorNetwork.cpp lodepng/lodepng.cpp \
  -shared -rdynamic \
  -framework OpenGL -FSDK/Libraries/Mac -framework XPLM -framework XPWidgets \
  -o Plugin-XTextureExtractor-x64-Release/64/mac.xpl

file Plugin-XTextureExtractor-x64-Release/64/mac.xpl
