#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#ifdef AK_RTOS
#include <kernel.h>
#define __STATIC__  static
#else
#define __STATIC__ 
#endif

#include "ak_common.h"
#include "ak_log.h"
#include "ak_vpss.h"
#include "ak_vi.h"
#include "ak_osd.h"
#include "ak_mem.h"
//#include "ak_osd_ex.h"
#include "ak_osd_ex.h"

#define LEN_HINT         512            //defaut len for opt
#define LEN_OPTION_SHORT 512
#define FONT_WIDTH    16                  //default font size
#define FONT_HEIGHT    16                  //default font size
#define OSD_STRING_FORMAT "CHN%dOSD%d"  //display string

static char *pc_prog_name = NULL;  

static char *save_path = "/mnt/";       //yuv data save path
static char *isp_path = "";             //vi isp cfg file
#define LOGO_FILE_DIR         "/tmp/anyka.bmp"    //logo bmp file
static char *font_file = "/usr/local/ak_font_16.bin";   //default font file
static int dev_cnt = 1;
static int font_interval[2] = {2, 2};

static char ac_option_hint[  ][ LEN_HINT ] = {                      //操作提示数组
    "help info" ,                                                   //help
    "[PATH] isp cfg file path" ,                                    //cfg
    "[PATH] path to save yuv file, default /mnt/" ,                 //path
    "[FONT FILE] font file, default /usr/local/ak_font_16.bin" ,    //font
    "[NUM] device count [1 - 2], default 1" ,
    "[NUM] main chn font interval,  default 2" ,
    "[NUM] sub chn font interval,  default 2" ,
    "" ,
};

/* opt define */
static struct option option_long[ ] = {
    { "help"      , no_argument       , NULL , 'h' } ,    //"help info"
    { "cfg"       , required_argument , NULL , 'f' } ,    //"[PATH] isp cfg path"
    { "path"      , required_argument , NULL , 'p' } ,    //"[PATH] path to save yuv file, default /mnt/"
    { "font"      , required_argument , NULL , 's' } ,    //"[PATH] path to save yuv file, default /mnt/"
    { "dev_cnt"   , required_argument , NULL , 'd' } ,    //"[NUM] device count [1 - 2], default 1"
    { "main font_interval"   , required_argument , NULL , 'I' } ,    //"[NUM] main font interval,  default 2"
    { "sub font_interval"   , required_argument , NULL , 'i' } ,    //"[NUM] sub font interval,  default 2"
    {0, 0, 0, 0}
 };

/* usage */
static void usage(const char * name)
{
    ak_print_normal(MODULE_ID_APP, "Usage: %s -f isp_cfg_path -p savepath -s font_file\n", name);
    ak_print_notice(MODULE_ID_APP, "e.g: %s -f /etc/config/isp*.conf -p /mnt/ -s /mnt/ak_font_16.bin\n", name);
}


/*
 * help_hint: use the -h --help option.Print option of help information
 * return: 0
 */
static int help_hint(char *pc_prog_name)
{
    int i;

    printf("%s\n" , pc_prog_name);
    /* get opt */
    for(i = 0; i < sizeof(option_long) / sizeof(struct option); i ++) {
        if( option_long[ i ].val != 0 ) {
            printf("\t--%-16s -%c %s\n" , option_long[ i ].name , option_long[ i ].val , ac_option_hint[ i ]);
        }
    }

    /* printf a example */
    usage(pc_prog_name);
    
    printf("\n\n");
    return 0;
}

/*
 * get_option_short: fill the stort option string.
 * return: option short string addr.
 */
static char *get_option_short( struct option *p_option, int i_num_option, char *pc_option_short, int i_len_option )
{
    int i;
    int i_offset = 0;
    char c_option;

    /* get opt */
    for( i = 0 ; i < i_num_option ; i ++ ) {
        if( ( c_option = p_option[ i ].val ) == 0 ) {
            continue;
        }
        switch( p_option[ i ].has_arg ){
        case no_argument:
            /* if no argument, set the offset for default */
            i_offset += snprintf( pc_option_short + i_offset , i_len_option - i_offset , "%c" , c_option );
            break;
        case required_argument:
            /* required argument offset calculate */
            i_offset += snprintf( pc_option_short + i_offset , i_len_option - i_offset , "%c:" , c_option );
            break;
        case optional_argument:
            /* calculate the option offset */
            i_offset += snprintf( pc_option_short + i_offset , i_len_option - i_offset , "%c::" , c_option );
            break;
        }
    }
    return pc_option_short;
}

static int parse_option( int argc, char **argv )
{
    int i_option;
    char ac_option_short[ LEN_OPTION_SHORT ];                           /* opt string */
    int i_array_num = sizeof( option_long ) / sizeof( struct option ) ; /* get the option num*/
    char c_flag = 1;
    pc_prog_name = argv[ 0 ];                                           /* get the option */

    optind = 0;
    get_option_short( option_long, i_array_num , ac_option_short , LEN_OPTION_SHORT );  /* parse the cmd line input */
    while((i_option = getopt_long(argc , argv , ac_option_short , option_long , NULL)) > 0) {
        switch(i_option) {
        case 'h' :                   //help
            help_hint(argv[0]);
            c_flag = 0;
            goto parse_option_end;
        case 'f' :                   //isp cfg path
            isp_path = optarg;
            break;
        case 'p' :                   //path
            save_path = optarg;
            break;
        case 's' :                   //path
            font_file = optarg;
            break;
        case 'd' :
            dev_cnt = atoi(optarg);
            break;
        case 'I' :
            font_interval[0] = atoi(optarg);
            break;
        case 'i' :
            font_interval[1] = atoi(optarg);
            break;
        default :
            help_hint(argv[0]);     //help message print
            c_flag = AK_FALSE;
            goto parse_option_end;
        }
    }
parse_option_end:
    return c_flag;
}

static int app_start_vi(int dev_id)
{
    int ret = -1;    
    
    /* 
     * step 0: global value initialize
     */
    int width = 1920;
    int height = 1080;
    int subwidth = 640;
    int subheight = 360;
    int chn_main_id = VIDEO_CHN0;
    int chn_sub_id = VIDEO_CHN1;

	if (dev_id)
    {
        chn_main_id = VIDEO_CHN2;
        chn_sub_id = VIDEO_CHN3;
    }

    /* open vi flow */

    /* 
     * step 1: open video input device
     */
    ret = ak_vi_open(dev_id);
    if (AK_SUCCESS != ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d open failed\n", dev_id);    
        return ret;
    }

    /*
     * step 2: load isp config
     */
    ret = ak_vi_load_sensor_cfg(dev_id, isp_path);
    if (AK_SUCCESS != ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d load isp cfg [%s] failed!\n", dev_id, isp_path);    
        return ret;
    }

    /* 
     * step 3: get sensor support max resolution
     */
    RECTANGLE_S res;                //max sensor resolution
    VI_DEV_ATTR    dev_attr;
    memset(&dev_attr, 0, sizeof(VI_DEV_ATTR));
    dev_attr.dev_id = dev_id;
    dev_attr.crop.left = 0;
    dev_attr.crop.top = 0;
    dev_attr.crop.width = width;
    dev_attr.crop.height = height;
    dev_attr.max_width = width;
    dev_attr.max_height = height;
    dev_attr.sub_max_width = subwidth;
    dev_attr.sub_max_height = subheight;

    /* get sensor resolution */
    ret = ak_vi_get_sensor_resolution(dev_id, &res);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "Can't get dev[%d]resolution\n", dev_id);
        ak_vi_close(dev_id);
        return ret;
    } else {
        ak_print_normal_ex(MODULE_ID_APP, "get dev res w:[%d]h:[%d]\n",res.width, res.height);
        dev_attr.crop.width = res.width;
        dev_attr.crop.height = res.height;
    }

    /* 
     * step 4: set vi device working parameters 
     * default parameters: 25fps, day mode
     */
    ret = ak_vi_set_dev_attr(dev_id, &dev_attr);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d set device attribute failed!\n", dev_id);
        ak_vi_close(dev_id);
        return ret;
    }

    /*
     * step 5: set main channel attribute
     */
    
    VI_CHN_ATTR chn_attr = {0};
    chn_attr.chn_id = chn_main_id;
    chn_attr.res.width = width;
    chn_attr.res.height = height;
    chn_attr.frame_depth = 3;
    /*disable frame control*/
    chn_attr.frame_rate = 0;
    ret = ak_vi_set_chn_attr(chn_main_id, &chn_attr);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d set channel [%d] attribute failed!\n", dev_id, chn_main_id);
        ak_vi_close(dev_id);
        return ret;
    }
    ak_print_normal_ex(MODULE_ID_APP, "vi device %d main sub channel attribute\n", dev_id);


    /*
     * step 6: set sub channel attribute
     */
    
    VI_CHN_ATTR chn_attr_sub;
    chn_attr_sub.chn_id = chn_sub_id;
    chn_attr_sub.res.width = subwidth;
    chn_attr_sub.res.height = subheight;
    chn_attr_sub.frame_depth = 3;
    /*disable frame control*/
    chn_attr_sub.frame_rate = 0;
    ret = ak_vi_set_chn_attr(chn_sub_id, &chn_attr_sub);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d set channel [%d] attribute failed!\n", dev_id, chn_sub_id);
        ak_vi_close(dev_id);
        return ret;
    }
    ak_print_normal_ex(MODULE_ID_APP, "vi device %d set sub channel attribute\n", dev_id);


	/* 
     * step 7: enable vi device 
     */
    ret = ak_vi_enable_dev(dev_id);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d enable device  failed!\n", dev_id);
        ak_vi_close(dev_id);
        return ret;
    }

    /* 
     * step 8: enable vi main channel 
     */
    ret = ak_vi_enable_chn(chn_main_id);
    if(ret)
    {
        ak_print_error_ex(MODULE_ID_APP, "vi channel[%d] enable failed!\n", chn_main_id);
        ak_vi_close(dev_id);
        return ret;
    }

    ret = ak_vi_enable_chn(chn_sub_id);
    if(ret)
    {
        ak_print_error_ex(MODULE_ID_APP, "vi channel[%d] enable failed!\n", chn_sub_id);
        ak_vi_close(dev_id);
        return ret;
    }

    return AK_SUCCESS;
}

static int app_stop_vi(int dev_id)
{   
    int ret = -1;   
    int chn_main_id = VIDEO_CHN0;
	int chn_sub_id = VIDEO_CHN1;
	
    if (dev_id)
    {
        chn_main_id = VIDEO_CHN2;
		chn_sub_id = VIDEO_CHN3;
    }
    
    ak_vi_disable_chn(chn_main_id);
	ak_vi_disable_chn(chn_sub_id);
	ak_vi_disable_dev(dev_id);
    ret = ak_vi_close(dev_id);

    return ret;
}


static void save_yuv_frame(void)
{
    int ret = -1;
    FILE *fd = NULL;
    struct ak_date date;                        //time
    char time_str[32] = {0};
    char file_path[255] = {0};
    struct video_input_frame frame = {{0}};     //vi frame
    int i = 0;

    for (i=0; i<2*dev_cnt; i++)
    {   
        /* vi frame get */
        ret = ak_vi_get_frame(i, &frame);
        if (!ret) {
            ak_get_localdate(&date);            //get the time 
            ak_date_to_string(&date, time_str); //change digital to string

            if (VIDEO_CHN0 == i)
                sprintf(file_path, "%sdev0_osd_main%s.yuv", save_path, time_str);        //save yuv file
            else if (VIDEO_CHN1 == i)
                sprintf(file_path, "%sdev0_osd_sub%s.yuv", save_path, time_str);
            else if (VIDEO_CHN2 == i)
                sprintf(file_path, "%sdev1_osd_main%s.yuv", save_path, time_str);
            else if (VIDEO_CHN3 == i)
                sprintf(file_path, "%sdev1_osd_sub%s.yuv", save_path, time_str);

            /* save the yuv data */
            fd = fopen(file_path, "w+b");
            if (fd) {
                unsigned char *buf = frame.vi_frame.data;
                unsigned int len = frame.vi_frame.len;
                ak_print_normal(MODULE_ID_APP, "yuv len: %u\n", len);
                do {
                    len -= fwrite(buf+frame.vi_frame.len-len, 1, len, fd);
                } while (len != 0);
            } else {
                ak_print_normal(MODULE_ID_APP, "open YUV file failed!!\n");
            }
            
            if (fd)
                fclose(fd);
            
            ak_vi_release_frame(i, &frame);
        }
    }
}

static void app_osd_set(const char *file)
{
    struct video_input_frame frame = {{0}};
    int i = 0;
    int j = 0;
    int drop_num = 40;
    int ret = -1;

    /* osd ex test */
    ak_osd_ex_set_font_file(16, file);
    ak_osd_ex_init();
    ak_osd_ex_set_color(1, 0);
    ak_osd_ex_set_edge_color(2);
    ak_osd_ex_set_font_size(0, 32);
    ak_osd_ex_set_font_size(1, 16);
    ak_osd_ex_set_font_size(2, 32);
    ak_osd_ex_set_font_size(3, 16);

    int video_chn;
    int osd_chn;
    char osd_string[80];
    unsigned short us_osd_string[80];

    for (video_chn = 0; video_chn < 2*dev_cnt; video_chn++)
    for (osd_chn = 0; osd_chn < 3; osd_chn++) {
        int width;
        int height;
        int max_w, max_h;
        /* get the rect max size */
        if (ak_osd_ex_get_max_rect(video_chn, &max_w, &max_h) < 0) {
            ak_print_error_ex(MODULE_ID_APP, "chn:%d ak_osd_get_max_rect fail!\n", video_chn);
            return;
        }

        /* get the string code */
        sprintf(osd_string, OSD_STRING_FORMAT, video_chn, osd_chn);
        width = strlen(osd_string) * (FONT_WIDTH + font_interval[video_chn % 2]);
        height = FONT_HEIGHT * 2 + 2;

        width = (width > max_w) ? max_w : width;
        height = (height > max_h) ? max_h : height;

        int x = FONT_WIDTH * 4;
        int y = FONT_HEIGHT + (height + FONT_HEIGHT) * osd_chn;
        //printf("len %d w %d h %d x %d y %d\n", strlen(osd_string), width, height, x, y);
        if (ak_osd_ex_set_rect(video_chn, osd_chn, x, y, width, height) < 0) {
            ak_print_error_ex(MODULE_ID_APP, "video_chn:%d osd_chn:%d ak_osd_set_rect fail!\n", video_chn, osd_chn);
            return;
        }

        for (int i = 0; i < strlen(osd_string); i++)
            us_osd_string[i] = osd_string[i];
        
        ak_osd_ex_set_font_interval(video_chn, osd_chn, font_interval[video_chn % 2]);

        /* draw string */
        if (ak_osd_ex_draw_str(video_chn, osd_chn, 0, 0,
                    us_osd_string, strlen(osd_string)) < 0) {
            ak_print_error_ex(MODULE_ID_APP, "video_chn:%d osd_chn:%d ak_osd_draw_str fail!\n", video_chn, osd_chn);
            return;
        }
    }

    /* drop frame */
    for (i = 0; i < drop_num; i++) 
    {
        for (j = 0; j < 2*dev_cnt; j++)
        {
            ret = ak_vi_get_frame(j, &frame);
            if (!ret)
                /* the frame has used,release the frame data */
                ak_vi_release_frame(j, &frame);
        }
    }

    /* save data */
    save_yuv_frame();

    /* release osd */
    ak_osd_ex_destroy();

    ak_print_normal_ex(MODULE_ID_APP, "osd set over\n");
}


/**
 * Preconditions:
 * your main video progress must stop
 */
__STATIC__ int main(int argc, char **argv)
{
    sdk_run_config config;
    memset(&config, 0, sizeof(config));
    config.mem_trace_flag = SDK_RUN_NORMAL;
    ak_sdk_init( &config );

    ak_print_normal(MODULE_ID_APP, "***********************************************\n");
    ak_print_normal(MODULE_ID_APP, "***** osd version: %s *****\n", ak_osd_get_version());
    ak_print_normal(MODULE_ID_APP, "***********************************************\n");

    /* parse cmd line */
    if (0 == parse_option(argc, argv)) 
    {
        return 0;
    }
            
    ak_print_normal(MODULE_ID_APP, "osd test start.\n");

    if(dev_cnt < 1 || dev_cnt > 2)
    {
        ak_print_error_ex(MODULE_ID_APP,"dev_cnt error! use default value 1.\n");
        dev_cnt = 1;
    }

    /*
        * step 1: start vi
        */
    if (app_start_vi(VIDEO_DEV0))
        goto exit;


    if (dev_cnt > 1)
    {
        app_start_vi(VIDEO_DEV1);
    }

    /*
        * step 2: osd main function 
        */
    app_osd_set(font_file);

exit:
    /*
     * step 3: release resource
     */
    app_stop_vi(VIDEO_DEV0);
    if (dev_cnt > 1)
    {
        app_stop_vi(VIDEO_DEV1);
    }
    
    ak_print_normal(MODULE_ID_APP, "exit osd sample\n");
    ak_sdk_exit();

    return 0;
}

#ifdef AK_RTOS
SHELL_CMD_EXPORT_ALIAS(main, ak_osd_ex_sample, vi sample)
#endif
