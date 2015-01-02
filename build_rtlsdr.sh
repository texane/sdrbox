#!/usr/bin/env sh

# variables
CROSS_COMPILE=/home/texane/repo/lfs/_work_bbb_home/host_install/armv6-rpi-linux-gnueabi/bin/armv6-rpi-linux-gnueabi-

# default and create directories
TOP_DIR=`pwd`
BUILD_DIR=$TOP_DIR/dl
INSTALL_DIR=$TOP_DIR/install
[ -d $BUILD_DIR ] || mkdir $BUILD_DIR
[ -d $INSTALL_DIR ] || mkdir $INSTALL_DIR

# libusb
cd $BUILD_DIR
wget http://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.9/libusb-1.0.9.tar.bz2
bzip2 -d libusb-1.0.9.tar.bz2
tar xvf libusb-1.0.9.tar
mkdir libusb-1.0.9/build
cd libusb-1.0.9/build
CC=$CROSS_COMPILE\gcc ../configure --host=arm-linux --prefix=$INSTALL_DIR
make && make install 

# rtlsdr
cd $BUILD_DIR
git clone git://git.osmocom.org/rtl-sdr.git
mkdir rtl-sdr/build
cd rtl-sdr/build
CC=$CROSS_COMPILE\gcc \
cmake \
-DLIBUSB_PKG_INCLUDE_DIRS=$INSTALL_DIR/include/libusb-1.0 \
-DLIBUSB_PKG_LIBRARY_DIRS=$INSTALL_DIR/lib \
-DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
..
make && make install
