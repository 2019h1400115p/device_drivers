
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>              
#include <unistd.h>             
#include <sys/ioctl.h>          
#include "chardev.h"

int chn,chn_in;
char aln,aln_in;
uint16_t user_buffer;


int main()
{
    int fd,ret;
    
    fd = open("/dev/adc8",0);                                 //open ADC
    if (fd < 0) {
        printf("Device file failed to opened with file descriptor %d \n",fd);
       exit-1;
    }
    printf(" ADC opened successfully \n");
    printf("Enter channel number between 0-7 \n");          //take the channel no.from user
scan_chn_in: scanf(" %d",&chn_in);
    if (chn_in <=7 && chn_in>=0 )                          //validate user input
    {
    chn=chn_in;
    }
    else 
    {
    printf("Please re-enter no. between 0-7 \n");
    goto scan_chn_in;
    }

    printf("Enter L for left-align, R for right-align \n");   //take alignment from user
 scan_aln_in: scanf(" %c",&aln_in);
    if (aln_in=='L' || aln_in=='R')                          //validate user input
    {
    aln=aln_in;
    }
    else 
    {
    printf("Please re-enter as L or R \n");
    goto scan_aln_in;
    }
printf("\n Channel no.selected is %d",chn);
printf("\n Alignment selected is %c",aln);
    ioctl(fd,IOCTL_SET_CHANNELNO,&chn);                 // pass them to kernel           
    ioctl(fd,IOCTL_SET_ALIGNMENT,&aln);                // pass them to kernel               
    
    ret=read(fd,&user_buffer,sizeof(user_buffer));    //read 10-bit value obtained in user-space buffer
    if (ret==0)
    printf ("\n Read from ADC successful");
    printf ("\n Value obtained from the ADC is %d",user_buffer);
    printf ("\n");


    close(fd);                                       //close ADC
    return 0;
}
