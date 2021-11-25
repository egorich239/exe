#!/bin/bash

readonly D=$(mktemp -d)

for f in bench/*.cpp ; do
  sed exe.h $f >$D/$(basename $f) -e '/#include "exe.h"/d; /#pragma once/d;'
done

rm exe.zip
zip -j exe $D/*