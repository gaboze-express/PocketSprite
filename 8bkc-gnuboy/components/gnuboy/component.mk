#
# Component Makefile
#
# This Makefile should, at the very least, just include $(SDK_PATH)/make/component.mk. By default, 
# this will take the sources in this directory, compile them and link them into 
# lib(subdirectory_name).a in the build directory. This behaviour is entirely configurable,
# please read the SDK documents if you need to do this.
#

include $(IDF_PATH)/make/component_common.mk

CFLAGS += -DIS_LITTLE_ENDIAN  -O3 -Wno-error=strict-aliasing
# CFLAGS += -DGNUBOYDBG