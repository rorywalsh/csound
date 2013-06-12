#!/bin/sh

# Simple script to copy "local" shared libs to the installation libs directories.
# This because I can't seem to get the canonical way to do this to work.

# Do this first because Android does not support embedded versioned sonames?

mkdir libs
mkdir libs/armeabi
mkdir libs/armeabi-v7a

cp -f ../pluginlibs/libfluidsynth/libs/armeabi/libfluidOpcodes.so libs/armeabi/
cp -f ../pluginlibs/libstdutil/libs/armeabi/*.so libs/armeabi/
cp -f ../pluginlibs/signalflowgraph/libs/armeabi/*.so libs/armeabi/
cp -f ../pluginlibs/LuaCsound/libs/armeabi/*.so libs/armeabi/

cp -f ../pluginlibs/libfluidsynth/libs/armeabi-v7a/libfluidOpcodes.so libs/armeabi-v7a/
cp -f ../pluginlibs/libstdutil/libs/armeabi-v7a/libstdutil.so libs/armeabi-v7a/
cp -f ../pluginlibs/signalflowgraph/libs/armeabi-v7a/libsignalflowgraph.so libs/armeabi-v7a/
cp -f ../pluginlibs/LuaCsound/libs/armeabi-v7a/libLuaCsound.so libs/armeabi-v7a/

# Also copy other resources used by Csound opcodes.

mkdir assets/samples/
cp -f ../../samples/* assets/samples
