@echo off
g++ -O2 -std=c++17 FleasionToFishstrap.cpp -o FleasionToFishstrap.exe -lwinhttp -lole32 -luuid -lwindowscodecs
pause