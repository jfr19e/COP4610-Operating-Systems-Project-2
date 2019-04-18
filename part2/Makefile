# This Makefile compiles my_xtime.o as a module
# into the kernel, allowing for it to be
# inserted and removed

ifneq ($(KERNELRELEASE),)
  obj-m := my_xtime.o
else
  KERNELDIR ?= /lib/modules/`uname -r`/build/
  PWD := `pwd`
default:
	make -C $(KERNELDIR) M=$(PWD) modules
endif

clean:
	rm -f *.ko *.o Module* *mod*
