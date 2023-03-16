obj-m = producer_consumer.o
main:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules	
