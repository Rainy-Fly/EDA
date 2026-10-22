os ?= linux

run:
ifeq ($(os),linux)
	echo "linux"
endif
ifeq ($(os),win)
	echo "windows"
endif