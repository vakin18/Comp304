obj-m += my_module.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean
test:
	sudo dmesg -C
	sudo insmod my_module.ko PID=952 traverseType="-d"
	sudo rmmod my_module.ko
	dmesg
