/*----------------------------------------------------------------------------
 * Name:    blk_driver.h
 * Author:  Desai Aneri Hiren (2019H1400582P), Avanti Satsheel Sapre (2019H1400115P)
 * Course : EEE G547 Device Drivers
 * Assignment : 3
 * System Detail : UBUNTU 16.04
                   Kernel Version :4.15.0-96-generic  
*----------------------------------------------------------------------------
*/

#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/blkdev.h>
#include<linux/genhd.h>
#include<linux/spinlock.h>
#include <linux/bio.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/bio.h>

//Device list
#define SANDISK_PEN_VID  0x0781
#define SANDISK_PEN_PID  0x5151

#define SANDISK1_PEN_VID 0x0781
#define SANDISK1_PEN_PID 0x558a



#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_OUT 0x00
#define USB_REQUEST_TYPE_CLASS (0x01 << 5)
#define USB_RECIPIENT_INTERFACE 0x01

#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])


#define DEVICE_NAME "USB_DRIVE"
#define MAJOR_NO 166
#define SECTOR_SIZE 512

int err =0;
unsigned int endpoint_in = 0, endpoint_out = 0;
uint32_t usb_capacity;
uint32_t ret_tag;
uint8_t lun; 

static struct usb_device *device;
struct request *req;


struct blkdev_private{
	struct request_queue *queue;
	struct gendisk *gd;
	spinlock_t lock;
        struct workqueue_struct *usbqueue;
};

//Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

//Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

struct usb_work{
	struct work_struct work;
	struct request *req;
     
};


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



static struct blkdev_private *p_blkdev = NULL;

