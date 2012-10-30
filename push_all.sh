#!/bin/bash

DIR=obj/local/armeabi-v7a

adb root \
&& adb remount \
&& adb push $DIR/libcmm.so /system/lib/
