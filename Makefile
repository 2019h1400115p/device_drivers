obj-m := USB_read_capacity.o

all:	
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
		find . -type f | xargs -n 5 touch
		rm -rf $(OBJS)
