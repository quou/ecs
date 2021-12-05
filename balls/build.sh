#!/bin/bash

cc -c -O3 -Wall -Iglad/include glad/src/glad.c -o glad/src/glad.o
c++ -g -O3 -Wall -Iglad/include -I../ balls.cpp glad/src/glad.o -lglfw -lm -lGL -lX11 -lpthread -ldl -o balls
