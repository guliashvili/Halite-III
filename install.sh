#!/usr/bin/env bash

cd $1
mkdir tmp
cd tmp
wget -O archive.tar.gz https://github.com/guliashvili/halite-private/archive/71.1.tar.gz  > /dev/null 2>&1
tar  -xvzf  archive.tar.gz > /dev/null 2>&1
mv  -v  halite-private-71.1/* . > /dev/null 2>&1

g++-7 -std=c++17 -march=native -oFast -flto -fprofile-generate=prof MyBot.cpp hlt/*.cpp -o MyBotProfile > /dev/null 2>&1
g++-7 -std=c++17 -march=native -oFast -flto MyBot.cpp hlt/*.cpp -o MyBotTmp > /dev/null 2>&1

./haliteLinux --no-timeout --no-replay --no-logs --width 64 --height 64 "./MyBotProfile" "./MyBotTmp" > /dev/null 2>&1

g++-7 -std=c++17 -march=native -oFast -flto  -fprofile-use=prof MyBot.cpp hlt/*.cpp -o MyBot > /dev/null 2>&1

cd ../

mv tmp/MyBot .
