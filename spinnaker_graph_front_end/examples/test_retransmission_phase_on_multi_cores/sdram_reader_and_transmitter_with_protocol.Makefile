# If SPINN_DIRS is not defined, this is an error!
ifndef SPINN_DIRS
    $(error SPINN_DIRS is not set.  Please define SPINN_DIRS (possibly by running "source setup" in the spinnaker package folder))
endif

APP = sdram_reader_and_transmitter_with_protocol
BUILD_DIR = build/
SOURCES = sdram_reader_and_transmitter_with_protocol.c

MAKEFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(dir $(MAKEFILE_PATH))
SOURCE_DIR := $(abspath $(CURRENT_DIR))
SOURCE_DIRS += $(SOURCE_DIR)
APP_OUTPUT_DIR := $(abspath $(CURRENT_DIR))/

include $(SPINN_DIRS)/make/Makefile.SpiNNFrontEndCommon
