#include "platform_mtd.h"
#include <stdlib.h>
#include "stdio.h"
#include "string.h"
/*logo分区存放两张jpg图片信息,一张做启动logo，一张做主页logo*/



//读取mtd信息
static int get_mtd_info(const char *mtd_name, char *mtd_str, char *mtd_size) {  
    FILE *fp;  
    char line[256];
    int found = 0;  
  
    fp = fopen("/proc/mtd", "r");  
    if (fp == NULL) {  
        perror("open /proc/mtd failed");  
        return -1;  
    }  
  
    while (fgets(line, sizeof(line), fp)) {  

        if (strstr(line, mtd_name) != NULL) {  
             sscanf(line, "%[^:]: %s", mtd_str, mtd_size);  
            found = 1;  
            break;  
        }  
    }  
  
    fclose(fp);  
  
    return found ? 0 : -1;
}


//读取启动logo信息;0:logo 1:home
int platform_logo_read(char logo_type,char * patf)
{
    char mtd_str[64];
    char mtd_size[64];
    char *name = NULL;
    if(logo_type == 0)
    {
        name = "LOGO";
    }
    if (get_mtd_info(name, mtd_str, mtd_size) == 0) {  
        printf(" name:%s %s, size: %s\n",name, mtd_str, mtd_size);  
    } else {  
        printf("find LOGO device failed\n");  
    }
    char cmd_buff[1024] = {0};
    sprintf(cmd_buff,"mtd_debug read /dev/%s 0x0 0x%s %s",mtd_str,mtd_size,patf);
    system(cmd_buff);  
    return 0;  
}

//更新图片到指定分区到logo分区 logo_type;0:logo 1:home
int platform_logo_write(char logo_type,char * patf)
{
    char mtd_str[10];
    char mtd_size[20];
    char *name = NULL;
    if(logo_type == 0)
    {
        name = "LOGO";
    }
    if (get_mtd_info(name, mtd_str, mtd_size) == 0) {  
        printf(" name:%s %s, size: %s\n",name, mtd_str, mtd_size);  
    } else {  
        printf("find LOGO device failed\n");  
    }
    char cmd_buff[1024] = {0};
    sprintf(cmd_buff,"mtd_debug erase /dev/%s 0x0 0x%s",mtd_str,mtd_size);
    system(cmd_buff);
    sprintf(cmd_buff,"mtd_debug write /dev/%s 0x0 0x%s %s",mtd_str,mtd_size,patf);
    system(cmd_buff);
  
    return 0;  
}
