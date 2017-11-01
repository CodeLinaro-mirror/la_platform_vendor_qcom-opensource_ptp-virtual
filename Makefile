export PTPSUPPORT CONFIG_PTPSUPPORT_OBJ

PTPSUPPORT=y	#ptp is enabled

ifeq ($(RELEASE_PACKAGE),1)
EXTRA_CFLAGS+=-DRELEASE_PACKAGE
endif
LBITS ?= $(shell getconf LONG_BIT)
ifeq ($(LBITS),64)
CCFLAGS += -m64
EXTRA_CFLAGS+=-DSYSTEM_IS_64
else
CCFLAGS += -m32
endif

CONFIG_PTPSUPPORT_OBJ=y
EXTRA_CFLAGS+=-DCONFIG_PTPSUPPORT_OBJ

obj-m := ptp_virtual.o

ptp_virtual-y += ptp_virtual_module.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(shell pwd) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(shell pwd) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
