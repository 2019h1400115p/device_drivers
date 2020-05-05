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



unsigned int endpoint_in=0, endpoint_out=0;
char *pendrive;
uint32_t expected_tag;
uint32_t  max_lba, block_size=0;
unsigned long int device_size;     


struct usb_device *device;


//Defining devices supported by this driver
static struct usb_device_id devices_table [] = {
	{USB_DEVICE(CRUZER_VID, CRUZER_PID)},
        {USB_DEVICE(SONY_VID, SONY_PID)},
	{USB_DEVICE(ANDROID_VID, ANDROID_PID)},
	{}	
};


struct command_block_wrapper{
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};


 struct command_status_wrapper{
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

//Look-up table to determine cdb_len

static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};




static int send_command_readcapacity(struct usb_device *device,uint8_t *cdb,uint32_t *ret_tag)
{
     static uint32_t tag = 1; 
     int i,r,actual_length;
     uint8_t cdb_len; 
     
 
     typedef struct command_block_wrapper command_block_wrapper;
     command_block_wrapper *cbw; 
     

     
    
    cbw = (command_block_wrapper *) kmalloc(sizeof(command_block_wrapper),GFP_KERNEL);


    if(cbw == NULL)
    {
       printk(KERN_ERR"Error! memory not allocated for CBW\n");
       return -1;
    }
    
       
//Preparing CBW packet
     
        cdb_len = cdb_length[cdb[0]];        

	cbw->dCBWSignature[0] = 'U';   
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = READ_CAPACITY_LENGTH;
	cbw->bmCBWFlags = 0x80;
	cbw->bCBWLUN = 0;
	cbw->bCBWCBLength = cdb_len; 
        

	for(i=0;i<16;i++)
		cbw->CBWCB[i] = *(cdb+i);
   
    
//Sending CBW packet containing READ_CAPACITY(10) request over Bulk Out endpoint
        i=0;
    do{
	r = usb_bulk_msg(device,usb_sndbulkpipe(device,endpoint_out),(void*)cbw,31,&actual_length,0);
	if (r!=0)
	{
        usb_clear_halt(device,usb_sndbulkpipe(device,endpoint_out));
	}
        i=i+1;
      }while((r!=0) && i<RETRY_MAX);
     
      
if (r==0)
			{
		printk(KERN_INFO "READ CAPACITY request sent successfully\nSent %d bytes\n", actual_length );		

			}
		else 
			{
			printk(KERN_INFO"READ CAPACITY request sending failed with r=%d \n",r);             
			}
return 0;	
}





static int get_status(struct usb_device *device, uint32_t expected_tag)
{
	int i, r, actual_length;
	typedef struct command_status_wrapper command_status_wrapper;
        command_status_wrapper *csw;
        csw = (command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);

          if(csw == NULL)
          {
                 printk("Kmalloc for CSW failed\n");
                 
          }

  //Receiving CSW packet 
	i = 0;
	do {
	r = usb_bulk_msg(device, usb_rcvbulkpipe(device,endpoint_in), (void*)csw, 13, &actual_length, 0);
		if (r!=0) {
			usb_clear_halt(device,usb_rcvbulkpipe(device,endpoint_in));
		}
		i++;
	} while ((r!=0) && (i<RETRY_MAX));



	if (r != 0) {
		printk(KERN_ERR "Get status failed with r=%d",r);
		return -1;
	}

	if (actual_length != 13) {
		printk(KERN_ERR "Get status:received %d bytes (expected 13)\n", actual_length);
		return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk(KERN_ERR "Get status: mismatched tags (expected %08X, received %08X)\n",expected_tag, csw->dCSWTag);
		return -1;
	}

	printk(KERN_INFO"READ CAPACITY Status: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");


	return 0;
}




void request_read_capacity (void)

{

int i,r1,r2,actual_length;
uint8_t* cdb;
uint8_t* buffer;

buffer = (uint8_t *) kmalloc(8 * sizeof(uint8_t),GFP_KERNEL);
cdb = kmalloc(16 * sizeof(uint8_t),GFP_KERNEL);

if (cdb==NULL)
	{
	printk(KERN_ERR" Kmalloc for CDB failed \n");
	} 

if (buffer==NULL)
	{
	printk(KERN_ERR" Kmalloc for buffer failed \n");
	} 

	for(i=0;i<16;i++)
	{
	*(cdb + i) =0;
	}

	for(i=0;i<8;i++)
	{
	*(buffer + i) =0;
	}
       
	*cdb = 0x25; //opcode for command READ_CAAPCITY(10)
	printk("\nReading Capacity:\n");
 
	//Sending READ_CAPACITY(10) request
	
	r1 = send_command_readcapacity(device,cdb,&expected_tag);
        if (r1!=0)
        printk(KERN_ERR"READ REQUEST failed with r =%d\n",r1);

        else if (r1==0)
        {

	//Receiving device response
        i=0;
    do{
	r2 = usb_bulk_msg(device,usb_rcvbulkpipe(device,endpoint_in),(void*)buffer,8,&actual_length,0);
	if (r2!=0)
	{
        usb_clear_halt(device,usb_rcvbulkpipe(device,endpoint_in));
	}
        i=i+1;
      }while((r2!=0) && i<RETRY_MAX);

      if (r2!=0)
	{
	printk(KERN_ERR"READ CAPACITY response failed with r =%d\n",r2);
	}
      else if (r2==0)
	{

	printk(KERN_INFO "Received %d bytes\n",actual_length);
	max_lba = be_to_int32(buffer);
	block_size = be_to_int32(&buffer[4]);
       
	device_size =  (((unsigned long int)(max_lba+1))*block_size)/(1024*1024);
        printk("Max LBA: 0x%08X \n", max_lba);
        printk("Block Size: 0x%03X bytes\n",block_size);
        printk("Device Capacity: %lu MB\n",device_size); 

       //Getting transfer status from device
       r2 = get_status(device, expected_tag);

	
   
 	 }

     }
}






static int pen_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
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
	printk("\nKnown USB Drive detected\n%s Plugged in \n",pendrive);

//Validating that device supports SCSI	
	if ((interface->cur_altsetting->desc.bInterfaceSubClass)==0x06
   && (interface->cur_altsetting->desc.bInterfaceProtocol)==0x50)
 {
printk(KERN_INFO "This is USB-attached SCSI type mass storage device \n");
 }
else 	
 {
	printk(KERN_INFO "This USB device does not support SCSI \n");
 }


//Reading Device descriptor and Interface descriptor
 
	printk(KERN_INFO "\nVID of device : 0x%04x\n",device->descriptor.idVendor);
	printk(KERN_INFO "PID of device : 0x%04x\n",device->descriptor.idProduct);
        printk(KERN_INFO "Device Class  : 0x%02x\n",interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "Device Sub-Class : 0x%02x\n",interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "Device Protocol : 0x%02x\n", interface->cur_altsetting->desc.bInterfaceProtocol);
	printk(KERN_INFO "No. of Endpoints : %d\n", interface->cur_altsetting->desc.bNumEndpoints);




//Obtaining endpoint addresses

	for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
	{
		ep_desc = &interface->cur_altsetting->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		{
		  if(epAddr & 0x80)
                    {
	            endpoint_in = epAddr;
                    printk(KERN_INFO "EP %d is Bulk IN with address 0x%02x \n",i,endpoint_in);
                    }
			else
		    {
	            endpoint_out = epAddr;
                    printk(KERN_INFO "EP %d is Bulk OUT with address 0x%02x \n",i,endpoint_out);
                    }	

		}

	}

       
request_read_capacity();
 
return 0;
}




static void pen_disconnect(struct usb_interface *interface)
{
	printk("USB Device %s Removed\n",pendrive);
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
	printk("\nUAS READ Capacity Driver inserted\n");
	usb_register(&penDriver);
	return 0;
}


int pendrive_exit(void)
{
	usb_deregister(&penDriver);
	printk("UAS READ Capacity Driver removed\n");
	return 0;
}


module_init(pendrive_init);
module_exit(pendrive_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Avanti Sapre");
MODULE_DESCRIPTION("USB Read Capacity Driver");

