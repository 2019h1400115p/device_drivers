/*----------------------------------------------------------------------------
 * Name:    blk_driver.c 
 * Author:  Desai Aneri Hiren (2019H1400582P), Avanti Satsheel Sapre (2019H1400115P)
 * Course : EEE G547 Device Drivers
 * Assignment : 3
 * System Detail : UBUNTU 16.04
                   Kernel Version :4.15.0-96-generic  
*----------------------------------------------------------------------------
*/

#include "blk_driver.h"

static int usb_block_open(struct block_device *bdev, fmode_t mode)
{
    printk(KERN_INFO "usb_block_open called\n");

    return 0;
}

static int usb_block_release(struct gendisk *gd, fmode_t mode)
{
    
    printk(KERN_INFO "usb_block_release called\n");
 
    return 0;
}


static struct block_device_operations blkdev_ops =
{
	.owner = THIS_MODULE,
	.open = usb_block_open,
	.release = usb_block_release
};


/*send_mass_storage_command() : Send SCSI command request */
static int send_mass_storage_command(struct usb_device *usb_dev, unsigned int endpoint,uint8_t lun,uint8_t *cdb, uint8_t direction, uint32_t data_length, uint32_t *ret_tag)
{
     static uint32_t tag = 1; 
     typedef struct command_block_wrapper command_block_wrapper;
     command_block_wrapper *cbwptr; 
     int retval,actual_length;
     uint8_t cdb_len; 
     int i;
  
    cbwptr = (command_block_wrapper *) kmalloc(sizeof(command_block_wrapper),GFP_KERNEL);

    if(cbwptr == NULL)
    {
       printk(KERN_ERR "Memory not allocated for CBW\n");
       return -1;
    }
    
    if (cdb == NULL)
     {
       
       return -1;
     }

    cdb_len = cdb_length[cdb[0]];

     if ((cdb_len == 0) || (cdb_len > sizeof(cbwptr->CBWCB))) 
     {
       printk(KERN_INFO "send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",cdb[0], cdb_len);
                		return -1;
     }
 
  cbwptr->dCBWSignature[0] = 'U';
	cbwptr->dCBWSignature[1] = 'S';
	cbwptr->dCBWSignature[2] = 'B';
	cbwptr->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbwptr->dCBWTag = tag++;
	cbwptr->dCBWDataTransferLength = data_length;
	cbwptr->bmCBWFlags = direction;
	cbwptr->bCBWLUN = lun;
	cbwptr->bCBWCBLength = cdb_len;
        
        for(i=0;i<16;i++)
		cbwptr->CBWCB[i] = *(cdb+i);

        retval = usb_bulk_msg(usb_dev,usb_sndbulkpipe(usb_dev,endpoint),(void *)cbwptr,31,&actual_length,10*HZ);

        if (retval < 0) 
	{
		printk(KERN_ERR "Bulk message returned %d\n", retval); 
		return retval;
	}
         
        kfree(cbwptr); 
	return 0;
}

/*get_mass_storage_status () : Get status of SCSI command request sent earlier*/

static int get_mass_storage_status(struct usb_device *usb_dev, unsigned int endpoint, uint32_t e_tag)
{
	
         int r, size;
        typedef struct command_status_wrapper command_status_wrapper;
        command_status_wrapper *cswptr;
        cswptr = (command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);

          if(cswptr == NULL)
          {
                 printk(KERN_ERR "Memory not allocated for CBW\n");
                 return -1;
          }

          
         r = usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev,endpoint),(void *)cswptr, 13, &size, 0);

         printk(KERN_INFO "Mass Storage Status: %02X (%s)\n", cswptr->bCSWStatus, cswptr->bCSWStatus?"FAILED":"Success");

        if (size != 13) {
		printk(KERN_INFO "get_mass_storage_status: received %d bytes ( expected 13)\n", size);
                kfree(cswptr);
		return -1;
	}
	if (cswptr->dCSWTag != e_tag) {
		printk(KERN_INFO "get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",e_tag, cswptr->dCSWTag);
                 kfree(cswptr);
		return -1;
	}
	
	if (cswptr->bCSWStatus) {
	    if (cswptr->bCSWStatus == 1){
                 kfree(cswptr);
		return -2;
             }	
	    else{
		 kfree(cswptr);
	        return -1;
           }
	}

        if(r < 0){
		printk(KERN_ERR "Bulk message returned %d\n", r);
                 kfree(cswptr);
	        return r;
         }

         kfree(cswptr);
	return 0;
}


/*delayed_data_transfer : In Bottom half this fuction is called to serve request as per it direction

rq_for_each_segment () : iterates through each segment of each struct bio structure of a struct request structure and updates a struct req_iterator structure */
void delayed_data_transfer(struct work_struct *work)
{

        struct usb_work *usb_wrk;
        struct request *cur_req;

        int r,actual_length,j,dir;
        struct bio_vec bv;
        unsigned int sector_cnt;
        unsigned long offset;

        struct req_iterator iter;
	sector_t sector_offset,start;
	u8 *buffer;
        uint8_t* cdb;
        size_t len;
        uint16_t sectors;

        usb_wrk = container_of(work,struct usb_work,work);
        cur_req = usb_wrk->req;
        dir = rq_data_dir(cur_req);
        sector_cnt = blk_rq_sectors(cur_req);  //No of sector on which operation to be done
        sector_offset = 0;
        
        cdb = kmalloc(16 * sizeof(uint8_t),GFP_KERNEL);

         if(cdb == NULL)
        {
          printk(KERN_ERR "Memory not allocated for CDB\n");
          return;
        }

        buffer = (char *)kmalloc(sizeof(char),GFP_KERNEL);
         if(buffer == NULL )
          {
            printk(KERN_ERR "Memory not allocated for bufferptr\n");
            return;
          }

        memset(buffer,0,sizeof(char));

        
        rq_for_each_segment(bv,cur_req, iter)
	{
               buffer = kmap_atomic(bv.bv_page);
               offset =  bv.bv_offset; 


              if(((bv.bv_len) % (SECTOR_SIZE)) != 0)
              {
		  printk(KERN_ERR "Bio size is not a multiple ofsector size\n");
                  return;
		  	      
              }

              start = iter.iter.bi_sector;
              
              len = bv.bv_len;
              sectors = ((bv.bv_len) / SECTOR_SIZE);
              

               for(j = 0; j < 16 ; j++)
                {
                    *(cdb+j) = 0 ;    
                }
            

              *(cdb + 2) = (start >> 24) & 0xFF;
              *(cdb + 3) = (start >> 16) & 0xFF;
              *(cdb + 4) = (start >> 8) & 0xFF;
              *(cdb + 5) = (start >> 0) & 0xFF;
 
              *(cdb + 7) = (sectors >> 8) & 0xFF;
              *(cdb + 8)= (sectors >> 0) & 0xFF;

              //Check Direction of request
              if(dir == 1) //WRITE Request

              {

                       printk (KERN_INFO "Request to write %d sectors \n ",sectors);            
		       *cdb = 0x2A;

                           send_mass_storage_command(device, endpoint_out, 0, cdb, 0x00,len, &ret_tag);
                
                           r = usb_bulk_msg(device, usb_sndbulkpipe(device, endpoint_out), ((void *) (buffer+offset)) ,len, &actual_length, 0);
                           if(r != 0)
			   {
				printk(KERN_INFO "Writing into drive failed (Error Code : %d)\n",r);
                                usb_clear_halt(device, usb_sndbulkpipe(device, endpoint_out));   
		           }

                           if (get_mass_storage_status(device, endpoint_in, ret_tag) < 0) 
                           {
				printk(KERN_INFO "Error in write success status\n");
                           }
 
              }
              else //READ Request
              {

                            printk (KERN_INFO "Request to read %d sectors \n ",sectors);                   
                            *cdb = 0x28;
                  
                           send_mass_storage_command(device, endpoint_out, 0, cdb, 0x80, len, &ret_tag);
                        
			   r = usb_bulk_msg(device, usb_rcvbulkpipe(device, endpoint_in), ((void *) (buffer+offset)), len, &actual_length, 0);
                           if(r != 0)
			    {
				printk(KERN_INFO "Reading from drive failed (Error Code : %d)\n",r);
				usb_clear_halt(device, usb_rcvbulkpipe(device, endpoint_in));                                
			    }
                        

		           if(get_mass_storage_status(device, endpoint_in, ret_tag) < 0)
                           {
			       	printk(KERN_INFO "Error in READ success status\n");			
                           }

              }

              sector_offset += sectors;

                       
        }

        __blk_end_request_cur(cur_req,0);
        kunmap_atomic(buffer);
 
        kfree(cdb);
        kfree(usb_wrk);
   
        if (sector_offset != sector_cnt)
	{
		printk(KERN_ERR "USB_DISK: bio info doesn't match with the request info\n");
	}
             

    return ;

}



/* usb_make_request () : Iterate through each request and server it in Bottom Half*/
static void usb_make_request(struct request_queue *q)
{

        struct request *req;
        struct usb_work *usb_wrk = NULL;


         //Iterate through Each Request              
        while((req = blk_fetch_request(q)) != NULL)
        {
	              
                if (blk_rq_is_passthrough(req)) {
                     printk (KERN_NOTICE "Skip non-fs request\n");
                     __blk_end_request_all(req, -EIO);
                     continue;
                }

                usb_wrk = (struct usb_work *)kmalloc(sizeof(struct usb_work),GFP_ATOMIC);
                if(usb_wrk == NULL){

                   printk(KERN_ERR "Memory allocation failed for usb_wrk\n");                 
                   __blk_end_request_all(req, 0);

                   continue;
                }


                usb_wrk->req = req;
                printk(KERN_ALERT "Target Sector No. %lu\n ", req->__sector);
                //Transfer request serving in bottom half
                printk(KERN_INFO"Deferring request processing to bottom half \n");
                INIT_WORK(&usb_wrk->work,delayed_data_transfer);
                queue_work(p_blkdev->usbqueue, &usb_wrk->work);

       }


        return;
}


/*usb_block_init :Register USB Block Device  and Create Local Work Queue*/
int usb_block_init(void)
{
       struct gendisk *usb_disk = NULL;

        err = register_blkdev(MAJOR_NO, "USB_DISK");
	if (err < 0) 
		printk(KERN_WARNING "Disk on USB: Unable to get major number\n");


     	p_blkdev = (struct blkdev_private *)kmalloc(sizeof(struct blkdev_private),GFP_KERNEL);
	
	if(!p_blkdev)
	{
		printk(KERN_ERR "ENOMEM  at %d\n",__LINE__);
		return -1;
	}
	memset(p_blkdev, 0, sizeof(struct blkdev_private));

        //Create Local Work Queue

        p_blkdev->usbqueue = create_workqueue("usbqueue");

        if(!p_blkdev->usbqueue){

            printk(KERN_INFO "Workqueue creation failed\n");
            kfree(p_blkdev);
            return -1;
         }


        spin_lock_init(&p_blkdev->lock);

 
	  p_blkdev->queue = blk_init_queue(usb_make_request, &p_blkdev->lock);
          
         
          if (p_blkdev->queue == NULL) 
          {
                 printk(KERN_ERR "Can not allocate Block Device queue\n");
                 return -ENOMEM;
          }
         
	usb_disk = p_blkdev->gd = alloc_disk(2); //here 2 is number of minors
	if(!usb_disk)
	{
		destroy_workqueue(p_blkdev->usbqueue);
		printk(KERN_INFO "alloc_disk() for Device failed\n");
		return -1;
	}
	blk_queue_logical_block_size(p_blkdev->queue, SECTOR_SIZE);

	usb_disk->major = MAJOR_NO;
	usb_disk->first_minor = 0;
	usb_disk->fops = &blkdev_ops;
	usb_disk->queue = p_blkdev->queue;
	usb_disk->private_data = p_blkdev;
	strcpy(usb_disk->disk_name, DEVICE_NAME); 
        set_capacity(usb_disk,(usb_capacity+1));

	add_disk(usb_disk); 

        //Now Disk is ready and live

	printk(KERN_INFO "Registered USB disk\n");

        return 0;

}



/*get_usb_info() : Max LUN and Capacity*/
int get_usb_info(void)
{

       int r,j,actual_length;
       uint8_t *lunptr;
       uint8_t* cdb;
       uint8_t* bufferptr; 
      
       cdb = kmalloc(16 * sizeof(uint8_t),GFP_KERNEL);

       bufferptr = (uint8_t *)kmalloc(8 * sizeof(uint8_t),GFP_KERNEL);

       lunptr = (uint8_t *) kmalloc(sizeof(uint8_t),GFP_KERNEL);

        if(bufferptr == NULL )
        {
          printk(KERN_ERR "Memory not allocated for bufferptr\n");
          return -1 ;
        }


        if(cdb == NULL)
        {
          printk(KERN_ERR "Memory not allocated for CDB\n");
          return -1 ;
        }
      
         if(lunptr == NULL)
       {
	printk(KERN_ERR "Memory allocation for LUN failed\n");
        return -1;

       }

        for(j = 0; j < 16 ; j++)
        {
          *(cdb+j) = 0 ;    
        }

      
	
     /*****************Sending BOM_RESET control********************/
	r = usb_control_msg(device, usb_rcvctrlpipe(device, 0),0xFF,0x21,0, 0, NULL, 0,100*HZ);

       if(r == 0)
       printk(KERN_INFO "BULK STORAGE DEVICE RESET : SUCCESS \n");
       else
       printk(KERN_INFO "BULK STORAGE DEVICE RESET : FAILED \n"); 

       //**************Obtaining Max LUN******************************************

        r = usb_control_msg(device,usb_rcvctrlpipe(device,0), 0xFE,0xA1,0,0,(void *)lunptr,1,100*HZ);
        lun = *lunptr;
          if (r < 0){
	    	printk(KERN_INFO "Failed: %d usb_control_msg for Max LUN\n",r);
                return -1;
          }
        else{
		printk(KERN_INFO "Max LUN %d\n", lun);
            }

       //**************Obtaining Read Capacity******************************************
        
         *cdb = 0x25;

             send_mass_storage_command(device,endpoint_out,lun,cdb,0x80,0x08, &ret_tag);

              r = usb_bulk_msg(device, usb_rcvbulkpipe(device,endpoint_in), bufferptr, 8, &actual_length, 10*HZ);//Check return value n throw error

                 if(r != 0)
                 {
                     printk(KERN_INFO "USB Read Capacity Response Failed \n");
                     usb_clear_halt(device, usb_rcvbulkpipe(device, endpoint_in)); 
                     return -1;
                 }
                 else
                 {
                  	usb_capacity = be_to_int32(bufferptr);
                 }


	if (get_mass_storage_status(device, endpoint_in,ret_tag ) < 0) {
              printk(KERN_INFO "Error in Mass Storage Status\n");
	}

       kfree(lunptr);
       kfree(bufferptr);
       kfree(cdb);


       return 0;
}


/* pen_probe () : */
static int pen_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
        int i,r;
	unsigned char epAddr, epAttr;
	struct usb_endpoint_descriptor *ep_desc;
        struct usb_host_interface *iface_desc;
        
        iface_desc = interface->cur_altsetting;
        device = interface_to_usbdev(interface);

          
       //Check if device belongs to SCSI type Mass storage class
       if(!((iface_desc->desc.bInterfaceClass == 0x08) && (iface_desc->desc.bInterfaceSubClass == 0x06 ) && (iface_desc->desc.bInterfaceProtocol == 0x50 )))
      {
        printk(KERN_INFO "Device is not USB attaced SCSI type Mass storage class\n");
        return -1;
      }     

       //Get Endpoint_in and Endpoint_out
       for(i=0;i<iface_desc->desc.bNumEndpoints;i++)
	{
		ep_desc = &iface_desc->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK) 
		{
			if(epAddr & USB_ENDPOINT_IN)
                        {
                                endpoint_in = 	epAddr;
                               
                        }
			else
                        {
                                endpoint_out = epAddr;
                        }	
		}
	}

        //Get USB Information  
        r = get_usb_info();
         if(r < 0)
        {
           printk(KERN_INFO "Failed to get USB Info. Can Not Register USB Device \n");

        }
        else
        {
                  //Register Block Device
                  r = usb_block_init(); 
                  if(r < 0)
                  {
		     printk(KERN_INFO "USB Block Device Registration Failed\n");
                  }


        }		

       
        return 0;
}


static void pen_disconnect(struct usb_interface *interface)
{
    	printk(KERN_INFO "USB Device Removed\n");

}

static struct usb_device_id pen_table[] =
{
    {USB_DEVICE(SANDISK_PEN_VID, SANDISK_PEN_PID) },
    {USB_DEVICE(SANDISK1_PEN_VID, SANDISK1_PEN_PID) },
    {} /* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, pen_table);
 
static struct usb_driver pen_driver =
{
    .name = "pen_driver",  //name of the device
    .probe = pen_probe, // Whenever Device is plugged in
    .disconnect = pen_disconnect, // When device is removed
    .id_table = pen_table,  //  List of devices served by this driver
};


int block_init(void)
{
        int result;

        result = usb_register(&pen_driver);
	
        if(result < 0)
        {
           printk(KERN_ERR"Failed to register USB Block Driver\n");
           return -1;
        } 
	
        printk(KERN_NOTICE "USB Block Driver Inserted\n");

        return 0;	
}

void block_exit(void)
{
	struct gendisk *usb_disk = p_blkdev->gd;
        if(usb_disk)
        {
         	del_gendisk(usb_disk);
        }
	blk_cleanup_queue(p_blkdev->queue);
	kfree(p_blkdev);
        unregister_blkdev(MAJOR_NO, "USB_DISK");
        usb_deregister(&pen_driver);

	return;
}


module_init(block_init);
module_exit(block_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aneri Desai , Avanti Sapre ");
MODULE_DESCRIPTION("USB BLOCK DRIVER");
