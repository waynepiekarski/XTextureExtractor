#!/bin/bash

cd `dirname $0`
OUT=./Uploads/XTextureExtractor-NEW.zip
set -xv
zip -r $OUT Plugin-XTextureExtractor-x64-Release/
