#
# makefile for LiJy-launch
#
# Author: Stefan Richthofer
#

CC = gcc
OUTPUTDIR = ./build

# Adjust this path to match your system, if not yet appropriate
JAVA_HOME = /usr/lib/jvm/default-java
#The symlink "default-java" does not exist on every system. If gnumake tells you that the header
#jni.h is missing, please adjust JAVA_HOME appropriately. Example for Java 7, 64 bit:
#JAVA_HOME = /usr/lib/jvm/java-7-openjdk-amd64

LIJY = ./src
INCLUDES = -I./src -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux
LIBS = -ldl -lz -lpthread
CFLAGS = -Wl,--add-stdcall-alias -c $(INCLUDES)

SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:.c=.o)

all: $(OUTPUTDIR) LiJyLaunch
	@echo ''
	@echo 'Build finnished.'

$(OUTPUTDIR):
	mkdir $(OUTPUTDIR)

.o:
	$(CC) $(CFLAGS) $< -o $@

LiJyLaunch: $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBS) -o $(OUTPUTDIR)/jython

clean:
	rm -f ./src/*.o

.PHONY: JyNI libJyNI libJyNI-Loader clean all

