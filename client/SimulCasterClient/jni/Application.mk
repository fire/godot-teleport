# MAKEFILE_LIST specifies the current used Makefiles, of which this is the last
# one. I use that to obtain the Application.mk dir then import the root
# Application.mk.
ROOT_DIR := $(dir $(lastword $(MAKEFILE_LIST)))../..
include $(ROOT_DIR)/Application.mk

NDK_MODULE_PATH := $(ROOT_DIR)
APP_ALLOW_MISSING_DEPS=true
