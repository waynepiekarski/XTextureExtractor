#!/bin/bash

cd `dirname $0`

for each in `find . -name "*.java" -o -name "*.cpp" -o -name "*.h" -o -name "*.kt" -o -name "README" | grep -v SDK`; do
  # Need to determine if any license text exists before we try to change it
  SEPARATOR=" ---------------------------------------------------------------------"
  EXISTS=`grep -- "$SEPARATOR" $each`
  if [ "$EXISTS" = "" ]; then
    # No license exists, just add it to the top, along with an extra blank line
    (cat COPYRIGHT; echo; cat $each) > $each.temp
  else
    # License text exists, use sed to remove it
    (cat COPYRIGHT; cat $each | sed "1,/$SEPARATOR/d") > $each.temp
  fi
  mv $each.temp $each
done
