#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>
#include<linux/slab.h>
#include<linux/types.h>


#define CRUZER_VID 0x0781
#define CRUZER_PID 0x5151

#define SONY_VID 0x054c
#define SONY_PID 0x05ba


#define ANDROID_VID 0x2717
#define ANDROID_PID 0xff40

#define READ_CAPACITY_LENGTH  0x08  
#define RETRY_MAX 05

#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])

unsigned char ENDPOINT_IN,ENDPOINT_OUT;
char *pendrive;
uint32_t expected_tag;
uint32_t  max_lba, block_size;
uint32_t device_size;
uint8_t buffer[8]={0}; 
uint8_t cdb[16]={0};     
uint8_t a;
uint8_t pipe1,pipe2;



struct usb_device *device;


static struct usb_device_id devices_table [] = {
	{USB_DEVICE(CRUZER_VID, CRUZER_PID)},
        {USB_DEVICE(SONY_VID, SONY_PID)},
	{USB_DEVICE(ANDROID_VID, ANDROID_PID)},
	{}	
};

typedef struct {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
}
command_block_wrapper;


 struct command_status_wrapper{
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};




static int request_READCAPACITY(struct usb_device *device,uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len=6; 
	int  i,r, size;
 command_block_wrapper *cbw =kmalloc(sizeof(command_block_wrapper),GFP_KERNEL);
        if (cbw==NULL)
	{
	printk(KERN_INFO" Kmalloc failed \n");
	} 
        else
        {
        printk(KERN_INFO"Kmalloc for CBW successful\n");
	}
        
	cbw->dCBWSignature[0] = 'U';   
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = READ_CAPACITY_LENGTH;
	cbw->bmCBWFlags = 0x00;
	cbw->bCBWLUN = 0;
	cbw->bCBWCBLength = cdb_len;     
	memcpy(cbw->CBWCB, cdb, cdb_len);
        cdb[0] = 0x25;

        pipe1 = usb_sndbulkpipe(device,ENDPOINT_OUT);
        pipe2 = usb_rcvbulkpipe(device,ENDPOINT_IN);

        i=0;
    do{
	r = usb_bulk_msg(device,pipe1,(void*)cbw,31,&size,1000);
	if (r!=0)
	{
        usb_clear_halt(device,pipe1);
	}
        i=i+1;
      }while((r!=0) && i<RETRY_MAX);
     
      
if (r==0)
			{
		printk(KERN_INFO "READ CAPACITY request sentsuccessfully \n Sent %d CDB bytes\n", cdb_len );		

			}
		else 
			{
			printk(KERN_INFO"READ CAPACITY request sending failed with r=%d \n",r);
                        printk(KERN_INFO"Sent %d bytes\n",size);             
			}
return 0;	
}



static int get_status(struct usb_device *device, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper csw;
	i = 0;
	do {
	r = usb_bulk_msg(device, pipe2, (void*)&csw, 13, &size, 0);
		if (r!=0) {
			usb_clear_halt(device,pipe2);
		}
		i++;
	} while ((r!=0) && (i<RETRY_MAX));

	if (r != 0) {
		printk(KERN_INFO " get_status failed, r=%d",r);
		return -1;
	}
	if (size != 13) {
		printk(KERN_INFO"get_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw.dCSWTag != expected_tag) {
		printk(KERN_INFO" get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",expected_tag, csw.dCSWTag);
		return -1;
	}

	printk(KERN_INFO"  Mass Storage Status: %02X (%s)\n", csw.bCSWStatus, csw.bCSWStatus?"FAILED":"Success");
	if (csw.dCSWTag != expected_tag)
		return -1;
	if (csw.bCSWStatus) {
		
		if (csw.bCSWStatus == 1)
			return -2;
		else
			return -1;
	}

	return 0;
}





static int pen_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i,r,count;
	unsigned char epAddr, epAttr;
	struct usb_endpoint_descriptor *ep_desc;
	device=interface_to_usbdev(interface);
	
        if(id->idVendor == CRUZER_VID && id->idProduct == CRUZER_PID)
	{
                pendrive="SanDiskCruzerBlade";
	}
	
	else if(id->idVendor == SONY_VID && id->idProduct == SONY_PID )
	{
		 pendrive="SONY";
	}
	else if(id->idVendor == ANDROID_VID && id->idProduct == ANDROID_PID )
	{
		 pendrive ="Android Phone";
	}
	printk(KERN_INFO "\n Known USB Drive detected \n%s Plugged in \n",pendrive);
	printk(KERN_INFO "VID of device : 0x%x\n",id->idVendor);
	printk(KERN_INFO "PID of device : 0x%x\n",id->idProduct);
        printk(KERN_INFO "Device Class  :   %x\n", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "Device Sub-Class : %x\n", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "Device Protocol : %x\n", interface->cur_altsetting->desc.bInterfaceProtocol);
	printk(KERN_INFO "No. of Endpoints = %d\n", interface->cur_altsetting->desc.bNumEndpoints);


if ((interface->cur_altsetting->desc.bInterfaceSubClass)==0x06
   && (interface->cur_altsetting->desc.bInterfaceProtocol)==0x50)
 {
printk(KERN_INFO "This is USB-attached SCSI type mass storage device \n");
 }
else 	
 {
	printk(KERN_INFO "This USB device does not support SCSI \n");
 }



	for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
	{
		ep_desc = &interface->cur_altsetting->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		{
		  if(epAddr & 0x80)
                    {
	            ENDPOINT_IN = epAddr;
                    printk(KERN_INFO "EP %d is Bulk IN with address 0x%x \n",i,ENDPOINT_IN);
                    }
			else
		    {
	            ENDPOINT_OUT = epAddr;
                    printk(KERN_INFO "EP %d is Bulk OUT with address 0x%x \n",i,ENDPOINT_OUT);
                    }	

		}

	}
       

//Implementing READ CAPACITY 

printk(KERN_INFO"Reading Capacity:\n");	
request_READCAPACITY(device,&expected_tag);
usb_bulk_msg(device, pipe2, (void*)&buffer,READ_CAPACITY_LENGTH, &count, 1000); //receive device response
	printk(KERN_INFO " received %d bytes\n", count);
	max_lba = be_to_int32(&buffer[0]);
	block_size = be_to_int32(&buffer[4]);
       
	device_size = ((max_lba+1)*block_size/(1024*1024*1024));
	printk(KERN_INFO " Max LBA: %08X, Block Size: %08X (%.2X GB)\n", max_lba, block_size, device_size);
    r= get_status(device, expected_tag);

if (r== 0) 
printk (KERN_INFO "Get status successful \n");
   
	
return 0;
}

static void pen_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USB Device %s Removed\n",pendrive);
	return;
}



static struct usb_driver penDriver = {
	name: "penDriver",  
	probe: pen_probe, 
	disconnect: pen_disconnect, 
	id_table: devices_table, 
};



int pendrive_init(void)
{
	printk(KERN_NOTICE "UAS READ Capacity Driver inserted\n");
	usb_register(&penDriver);
	return 0;
}

int pendrive_exit(void)
{
	usb_deregister(&penDriver);
	printk(KERN_NOTICE "UAS READ Capacity Driver removed\n");
	return 0;
}

module_init(pendrive_init);
module_exit(pendrive_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Avanti Sapre");
MODULE_DESCRIPTION("USB host side driver");
