#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <time.h>

#include "devices.h"
#include "protocal.h"
#include "cJSON.h"

struct sensor_data *sensor_data_create(int id,int type,const char *value,const char *transfer_type)
{
        struct sensor_data *sd = malloc(sizeof(*sd));
        assert(sd != NULL);

        sd->id = id;
        sd->type = type;

        sd->value = strdup(value);
        assert(sd->value != NULL);
        
        sd->transfer_type = strdup(transfer_type);
        assert(sd->transfer_type != NULL);
        
        time(&sd->timestamp);
        struct tm *tmp = localtime(&sd->timestamp);
        sprintf(sd->asctime,"%d-%02d-%02d %02d:%02d:%02d",tmp->tm_year+1900,tmp->tm_mon+1,tmp->tm_mday,tmp->tm_hour,tmp->tm_min,tmp->tm_sec);

        return sd;
}

void sensor_data_release(struct sensor_data *sd)
{
        free(sd->value);
        free(sd->transfer_type);
        free(sd);
}

int sensor_data_match_id(struct sensor_data *sd1,int id)
{
        return (sd1->id == id);
}

struct sensor_data *sensor_data_dup(struct sensor_data *sd)
{
        return sensor_data_create(sd->id,sd->type,sd->value,sd->transfer_type);
}

void sensor_data_debug(struct sensor_data *sd)
{
        printf("id=%d\t",sd->id);
        printf("value=%s\t",sd->value);
        printf("transfer_type=%s\t",sd->transfer_type);
        printf("time:%s\n",sd->asctime);
}

static char checksum(const char *data,int len)
{
        char sum = 0;
        int i;
        for(i = 0; i < len;i++){
                sum += data[i];
        }
        sum = ~sum + 1;
        return sum;
}


static int slip_encode(const char *src,int len_src,char *dest)
{
        int i,j;
        dest[0] = 0x7e;
        for(i=0,j=1;i<len_src;i++,j++){
                if(src[i]==0x7e || src[i]== 0x7d){
                        dest[j] = 0x7d;
                        j++;
                        dest[j] = src[i]^0x20;
                }else{
                        dest[j] = src[i];
                }
        }
        char sum = checksum(src,len_src);
        if(sum == 0x7d || sum == 0x7e){
                dest[j] = 0x7d;
                j++;
                dest[j] = sum^0x20;
        }else{
                dest[j] = sum;
        }
        j++;
        dest[j] = 0x7e;
        j++;


        return j;
}

static int slip_decode(const char *src,int len_src,char *dest)
{
 
        int i,j;
        for(i=0,j=0;i<len_src;i++,j++){
                if(src[i] == 0x7d){
                        i++;
                        dest[j] = src[i]^0x20;
                }else{
                        dest[j] = src[i];
                }
        }
        char sum = 0;
        for(i = 0;i < j;i++){
                sum += dest[i];
        }
        if(sum != 0){
                return -1;
        }

        return i-1;
}

static const char *transfer_types[] = {"zigbee","wifi","ipv6","bluetooth"};

#define ARRAY_SIZE(a)   (sizeof(a)/sizeof(a[0]))

static int find_transfertype(const char *str)
{
        int i;
        for(i=0;i<ARRAY_SIZE(transfer_types);i++){
                if(strcasecmp(transfer_types[i],str) == 0){ 
                        return i;
                }
        }

        return 0;
}

int sensor_data_to_slip(struct sensor_data *sd,char *slip,int size)
{
        char buf[100] ={0};
        int r = device_v2chararray(sd->id,sd->type,sd->value,buf,sizeof(buf));
        if(r < 0)
                return -1;

        int len = 3+r; 			// dvid  dvtype cmd_type

        if(2*len > size)                
                return -1;

        char data[len];
        data[0] = sd->id;
        data[1] = sd->type;
        data[2] = ( (find_transfertype(sd->transfer_type) << 4)&0xf0 ) | 0x03;
        memcpy(data+3,buf,r);

        return slip_encode(data,len,slip);
  
}


struct sensor_data *slip_to_sensor_data(const char *slip,int len)
{
        char data[len];

        int r = slip_decode(slip,len,data);
        if(r < 4)                            // dvid,type,transfer_type and data
                return NULL;
        
        int dvid = (unsigned char)data[0];
        int dvtype =(unsigned char)data[1];

        int transfer_type = (data[2]&0xf0) >> 4;
        if(transfer_type < 0||transfer_type >= (ARRAY_SIZE(transfer_types)))
                return NULL;
        const char *transfer_type_str = transfer_types[transfer_type];

        char buf[100] = {0};
        const char *value = device_v2string(dvid,dvtype,data+3,8,buf,sizeof(buf));
        if(value == NULL)
                return NULL;

        return sensor_data_create(dvid,dvtype,value,transfer_type_str);
}