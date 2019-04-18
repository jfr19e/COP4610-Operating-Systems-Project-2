# This Makefile compiles elevator.o as a module,
# so it can be inserted and removed from the kernel;
# it also compiles start_elevator.o, issue_request.o,
# and stop_elevator.o directly into the kernel, meaning
# that they will stay in the kernel, because they define
# the three system calls that have been added into the kernel

obj-y := start_elevator.o issue_request.o stop_elevator.o
obj-m := elevator.o

PWD := $(shell pwd)
KDIR := /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -f *.o *.ko *.mod.* Module.* modules.*
