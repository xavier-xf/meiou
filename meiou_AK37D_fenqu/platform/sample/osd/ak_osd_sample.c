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

#define LEN_HINT         512
#define LEN_OPTION_SHORT 512

static char *pc_prog_name = NULL;  

static char *save_path = "/mnt/";
static char *isp_path = "";
static char *logo_path = "/tmp/anyka.bmp";
static int dev_cnt = 1;

static char ac_option_hint[  ][ LEN_HINT ] = {                      //操作提示数组
    "help info" ,                                                   //help
    "[PATH] isp cfg file path" ,                                    //cfg
    "[PATH] path to save yuv file, default /mnt/" ,                 //path
    "[PATH] logo file path, bmp image" ,                            //logo
    "[NUM]	device count [1 - 2], default 1" ,
    "" ,
};

static struct option option_long[ ] = {
    { "help"      , no_argument       , NULL , 'h' } ,    //"help info"
    { "cfg"       , required_argument , NULL , 'f' } ,    //"[PATH] isp cfg path"
    { "path"      , required_argument , NULL , 'p' } ,    //"[PATH] path to save yuv file, default /mnt/"
    { "logo"      , required_argument , NULL , 'l' } ,    //"logo file path, default /tmp/anyka.bmp"
    { "dev_cnt"   , required_argument , NULL , 'd' } ,    //"device count [1 - 2], default 1"
    {0, 0, 0, 0}
 };

/* example for using this sample */
static void usage(const char * name)
{
    ak_print_normal(MODULE_ID_APP, "Usage: %s -f isp_cfg_path -p savepath -l logodir\n", name);
    ak_print_notice(MODULE_ID_APP, "e.g: %s -f /etc/config/isp*.conf -p /mnt/ -l /mnt/anyka.bmp \n", name);
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
        /* opt val */
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
    char ac_option_short[ LEN_OPTION_SHORT ];                               /* opt string */
    int i_array_num = sizeof( option_long ) / sizeof( struct option ) ;     /* get the option num*/
    char c_flag = 1;
    pc_prog_name = argv[ 0 ];                                               /* get the option */

    optind = 0;
    get_option_short( option_long, i_array_num , ac_option_short , LEN_OPTION_SHORT );  /* parse the cmd line input */
    while((i_option = getopt_long(argc , argv , ac_option_short , option_long , NULL)) > 0) {
        switch(i_option) {
        case 'h' :              //help
            help_hint(argv[0]);
            c_flag = 0;
            goto parse_option_end;
        case 'f' :              //isp cfg path
            isp_path = optarg;
            break;
        case 'p' :              //path
            save_path = optarg;
            break;
        case 'l' :              //logo bmp path
            logo_path = optarg;
            break;
        case 'd' :
            dev_cnt = atoi(optarg);
            break;
        default :
            help_hint(argv[0]); //help message print
            c_flag = AK_FALSE;
            goto parse_option_end;
        }
    }
parse_option_end:
    return c_flag;
}

static int start_vi(int dev_id)
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
    dev_attr.crop.width = width;        //res width
    dev_attr.crop.height = height;      //res height
    dev_attr.max_width = width;         //main max width
    dev_attr.max_height = height;       //mian max height
    dev_attr.sub_max_width = subwidth;  //sub max width
    dev_attr.sub_max_height = subheight;//sub max height

    ret = ak_vi_get_sensor_resolution(dev_id, &res);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "Can't get dev[%d]resolution\n",dev_id);
        ak_vi_close(dev_id);
        return ret;
    } else {
        ak_print_normal_ex(MODULE_ID_APP, "get dev res w:[%d]h:[%d]\n",res.width, res.height);
        dev_attr.crop.width = res.width;
        dev_attr.crop.height = res.height;
    }

    /* 
     * step 4: set vi working parameters 
     * default parameters: 25fps, day mode, auto frame-control
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
    chn_attr.chn_id = chn_main_id;   //main channel id
    chn_attr.res.width = width;     //res width
    chn_attr.res.height = height;   //res height
    chn_attr.frame_rate= 0;         //0 means not limit
    chn_attr.frame_depth = 3;       //3 for buff num
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
    chn_attr_sub.chn_id = chn_sub_id;   //sub channel id
    chn_attr_sub.res.width = subwidth;  //res width
    chn_attr_sub.res.height = subheight;//res height
    chn_attr_sub.frame_rate= 0;         //0 means not limit
    chn_attr_sub.frame_depth = 3;       //3 for buff num
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
    ret = ak_vi_enable_chn(chn_main_id);             //main channel enable
    if(ret)
    {
        ak_print_error_ex(MODULE_ID_APP, "vi channel[%d] enable failed!\n",chn_main_id);
        ak_vi_close(dev_id);
        return ret;
    }

    ret = ak_vi_enable_chn(chn_sub_id);             //sub channel enable
    if(ret)
    {
        ak_print_error_ex(MODULE_ID_APP, "vi channel[%d] enable failed!\n",chn_sub_id);
        ak_vi_close(dev_id);
        return ret;
    }

    return AK_SUCCESS;
}

static int stop_vi(int dev_id)
{
    int ret = -1;   
    int chn_main_id = VIDEO_CHN0;
    int chn_sub_id = VIDEO_CHN1;

    if (dev_id)
    {
        chn_main_id = VIDEO_CHN2;
        chn_sub_id = VIDEO_CHN3;
    }
    
    ak_vi_disable_chn(chn_main_id);                  //main channel disable
    ak_vi_disable_chn(chn_sub_id);                  //sub channel disable
    ak_vi_disable_dev(dev_id);                  //dev disable
    ret = ak_vi_close(dev_id);

    return ret;
}


static void save_yuv_frame(int dev_id)
{
    int ret = -1;
    FILE *fd = NULL;
    struct ak_date date;
    char time_str[32] = {0};
    char file_path[255] = {0};
    struct video_input_frame frame = {{0}};         //vi frame
    int i = 0;

    for (i=dev_id*2; i<dev_id*2+2; i++)
    {
        ret = ak_vi_get_frame(i, &frame);
        if (!ret) {
            ak_get_localdate(&date);            //get time
            ak_date_to_string(&date, time_str); //make it to string

            if (0 == i % 2)
                sprintf(file_path, "%sdev%d_osd_main%s_1920x1080.yuv", save_path, dev_id, time_str);      //file to store
            else
                sprintf(file_path, "%sdev%d_osd_sub%s_640x360.yuv", save_path, dev_id, time_str);

            fd = fopen(file_path, "w+b");
            /* if success, store the yuv data to opened file */
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
            
            ak_vi_release_frame(i, &frame);         //release vi frame
        }
    }

}

/* rgb888 table */
static unsigned int rgb_color_tables[] =
{
    0x000000, 0xffffff, 0x000000, 0x266ac0, 0x71408a, 0x4b554a, 0x599540, 0x0ec075,
    0x34aab5, 0x786085, 0x2c8aa0, 0x68d535, 0x34aa5a, 0x43e9ab, 0x4b55a5, 0x008080
};

static void osd_set(int dev_id)
{
    struct video_input_frame frame = {{0}};     //vi frame
    int i = 0;
    int drop_num = 40;                          //drop vi frame num
    int ret = -1;
    FILE *fp = NULL;
    unsigned int line_byte = 0;                 //logo bmp img line byte
    unsigned char *data = NULL;                 //logo bmp img data(yuv)
    int img_w = 0;                              //logo bmp res width
    int img_h = 0;                              //logo bmp res height
    char *p = NULL;                             //logo bmp img raw data(rgb)
    int chn_main_id = VIDEO_CHN0;
    int chn_sub_id = VIDEO_CHN1;

    if (dev_id)
    {
        chn_main_id = VIDEO_CHN2;
        chn_sub_id = VIDEO_CHN3;
    }

    /* get logo bmp info */
    fp = fopen(logo_path, "rb");

    if (NULL == fp) {
        ak_print_normal(MODULE_ID_APP, "logo file open failed!\n");
        goto EXIT;
    }

    //bmp format
    fseek(fp, 18, SEEK_SET);
    fread(&img_w, 1, 4, fp);
    fread(&img_h, 1, 4, fp);

    ak_print_normal(MODULE_ID_APP, "img width:%d, height:%d!\n", img_w, img_h);

    data = (unsigned char *)ak_mem_alloc(MODULE_ID_APP, img_w*img_h/2);

    if (NULL == data) {
        ak_print_normal(MODULE_ID_APP, "data calloc fail!\n");
        goto EXIT;
    }

    /* align */
    line_byte = (img_w*3+3) & ~0x3;

    /* raw bmp data(rgb888) */
    p = (char *)ak_mem_alloc(MODULE_ID_APP, line_byte*img_h);
    if (NULL == p) {
        ak_print_normal(MODULE_ID_APP, "p calloc fail!\n");
        goto EXIT;
    }

    fseek(fp, 54, SEEK_SET);
    fread(p, 1, line_byte*img_h, fp);
    fclose(fp);
    fp = NULL;

    struct osd_attr attr = {0};
    attr.color_mode_index = AK_OSD_COLOR_FMT_RGB;   //rgb mode
    attr.color_table = rgb_color_tables;            //init rgb map table
    if (AK_SUCCESS != ak_osd_init(&attr))           //osd init
    {
        ak_print_normal(MODULE_ID_APP, "ak_osd_init failed!!\n");
        goto EXIT;
    }   

    /* first region */
    struct osd_canvas_info canvas = {0};
    canvas.channel_id = chn_main_id;
    canvas.region_id = 0;
    canvas.pos_in_yuv.x = 100;          //pos x
    canvas.pos_in_yuv.y = 100;          //pos y
    canvas.region_size_width = img_w;   //res w
    canvas.region_size_height = img_h;  //res h
    canvas.canvas_color_index = 0;      //canvas color
    canvas.alpha = 0;
    if (AK_SUCCESS != ak_osd_create_canvas(&canvas))
    {
        ak_print_normal(MODULE_ID_APP, "ak_osd_create_canvas failed!!\n");
        goto EXIT;
    } 

    /* second region */
    canvas.channel_id = chn_main_id;
    canvas.region_id = 1;
    canvas.pos_in_yuv.x = 500;          //pos x
    canvas.pos_in_yuv.y = 300;          //pos y
    canvas.region_size_width = img_w;   //res w
    canvas.region_size_height = img_h;  //res h
    canvas.canvas_color_index = 0;      //canvas color
    canvas.alpha = 40;
    if (AK_SUCCESS != ak_osd_create_canvas(&canvas))
    {
        ak_print_normal(MODULE_ID_APP, "ak_osd_create_canvas failed!!\n");
        goto EXIT;
    } 

    /* third region */
    canvas.channel_id = chn_main_id;
    canvas.region_id = 2;
    canvas.pos_in_yuv.x = 900;          //pos x
    canvas.pos_in_yuv.y = 500;          //pos y
    canvas.region_size_width = img_w;   //res w
    canvas.region_size_height = img_h;  //res h
    canvas.canvas_color_index = 0;      //canvas color
    canvas.alpha = 70;
    if (AK_SUCCESS != ak_osd_create_canvas(&canvas))
    {
        ak_print_normal(MODULE_ID_APP, "ak_osd_create_canvas failed!!\n");
        goto EXIT;
    } 

    /* first region */
    canvas.channel_id = chn_sub_id;
    canvas.region_id = 0;
    canvas.pos_in_yuv.x = 100;          //pos x
    canvas.pos_in_yuv.y = 50;           //pos y
    canvas.region_size_width = img_w;   //res w
    canvas.region_size_height = img_h;  //res h
    canvas.canvas_color_index = 0;      //canvas color
    canvas.alpha = 0;
    if (AK_SUCCESS != ak_osd_create_canvas(&canvas))
    {
        ak_print_normal(MODULE_ID_APP, "ak_osd_create_canvas failed!!\n");
        goto EXIT;
    }

    /* second region */
    canvas.channel_id = chn_sub_id;
    canvas.region_id = 1;
    canvas.pos_in_yuv.x = 200;          //pos x
    canvas.pos_in_yuv.y = 150;          //pos y
    canvas.region_size_width = img_w;   //res w
    canvas.region_size_height = img_h;  //res h
    canvas.canvas_color_index = 0;      //canvas color
    canvas.alpha = 40;
    if (AK_SUCCESS != ak_osd_create_canvas(&canvas))
    {
        ak_print_normal(MODULE_ID_APP, "ak_osd_create_canvas failed!!\n");
        goto EXIT;
    }

    /* third region */
    canvas.channel_id = chn_sub_id;
    canvas.region_id = 2;
    canvas.pos_in_yuv.x = 300;          //pos x
    canvas.pos_in_yuv.y = 250;          //pos y
    canvas.region_size_width = img_w;   //res w
    canvas.region_size_height = img_h;  //res h
    canvas.canvas_color_index = 0;      //canvas color
    canvas.alpha = 70;
    if (AK_SUCCESS != ak_osd_create_canvas(&canvas))
    {
        ak_print_normal(MODULE_ID_APP, "ak_osd_create_canvas failed!!\n");
        goto EXIT;
    } 

    /* transform the rgb value to index data */
    unsigned char *dst = data + img_w * (img_h - 1) / 2;
    unsigned char r, g, b, id;
    char *tmp = p;
    for (int i=0; i<img_h; i++) {
        for (int j=0; j<img_w; j++) {
            b = *tmp;
            g = *(tmp+1);
            r = *(tmp+2);

            if (r + g + b >= 128*3)
                id = 1;
            else
                id = 2;

            if (0 == j % 2)
                *(dst+j/2) = id;
            else
                *(dst+j/2) |= (id << 4);
            
            tmp += 3;
        }
        dst -= img_w/2;
        tmp += line_byte - img_w*3;
    }

    /* draw the osd region */
    struct osd_drawing_info draw = {0};
    draw.channel_id = chn_main_id;
    draw.region_id = 0;
    draw.bitmap_size_height = img_h;
    draw.bitmap_size_width = img_w;
    draw.buffer_size = img_w*img_h/2;
    draw.buffer = data;
    /* first region */
    if (AK_SUCCESS != ak_osd_draw_bitmap_to_canvas(&draw))
        ak_print_normal(MODULE_ID_APP, "ak_osd_draw_bitmap_to_canvas 0 0 failed!!\n");

    draw.region_id = 1;
    /* second region */
    if (AK_SUCCESS != ak_osd_draw_bitmap_to_canvas(&draw))
        ak_print_normal(MODULE_ID_APP, "ak_osd_draw_bitmap_to_canvas 0 1 failed!!\n");

    draw.region_id = 2;
    /* third region */
    if (AK_SUCCESS != ak_osd_draw_bitmap_to_canvas(&draw))
        ak_print_normal(MODULE_ID_APP, "ak_osd_draw_bitmap_to_canvas 0 2 failed!!\n");

    draw.channel_id = chn_sub_id;
    draw.region_id = 0;
    /* first region */
    if (AK_SUCCESS != ak_osd_draw_bitmap_to_canvas(&draw))
        ak_print_normal(MODULE_ID_APP, "ak_osd_draw_bitmap_to_canvas 1 0 failed!!\n");
    
    draw.region_id = 1;
    /* second region */
    if (AK_SUCCESS != ak_osd_draw_bitmap_to_canvas(&draw))
        ak_print_normal(MODULE_ID_APP, "ak_osd_draw_bitmap_to_canvas 1 1 failed!!\n");
    
    draw.region_id = 2;
    /* third region */
    if (AK_SUCCESS != ak_osd_draw_bitmap_to_canvas(&draw))
        ak_print_normal(MODULE_ID_APP, "ak_osd_draw_bitmap_to_canvas 1 2 failed!!\n");
    
    /* set to vi */
    if (AK_SUCCESS != ak_osd_set_to_vi())
        ak_print_normal(MODULE_ID_APP, "ak_osd_set_to_vi failed!!\n");

    /* drop the frame */
    for (i = 0; i < drop_num; i++) 
    {
        ret = ak_vi_get_frame(chn_main_id, &frame);
        if (!ret)
            /* the frame has used,release the frame data */
            ak_vi_release_frame(chn_main_id, &frame);

        ret = ak_vi_get_frame(chn_sub_id, &frame);
        if (!ret)
            /* the frame has used,release the frame data */
            ak_vi_release_frame(chn_sub_id, &frame);
    }

    /* save the vi frame data */
    save_yuv_frame(dev_id);

    /* release the osd */
    ak_osd_uninit();

EXIT:
    /* release source */
    if (data)
        ak_mem_free(data);

    if (p)
        ak_mem_free(p);

    if (fp)
        fclose(fp);
    
    ak_print_normal_ex(MODULE_ID_APP, "osd set over\n");
}


/**
 * Preconditions:
 * your main video progress must stop
 */
__STATIC__ int main(int argc, char **argv)
{
    sdk_run_config config;
    memset(&config, 0, sizeof(sdk_run_config));
    config.mem_trace_flag = SDK_RUN_NORMAL;
    ak_sdk_init( &config );

    ak_print_normal(MODULE_ID_APP, "***********************************************\n");
    ak_print_normal(MODULE_ID_APP, "***** osd version: %s *****\n", ak_osd_get_version());
    ak_print_normal(MODULE_ID_APP, "***********************************************\n");

    if (0 == parse_option(argc, argv)) 
    {
        return 0;
    }
            
    ak_print_normal(MODULE_ID_APP, "osd sample start.\n");

    if(dev_cnt < 1 || dev_cnt > 2)
    {
        ak_print_error_ex(MODULE_ID_APP,"dev_cnt error! use default value 1.\n");
        dev_cnt = 1;
    }

    /*
        * step 1: start vi
        */
    if (start_vi(VIDEO_DEV0))
        goto exit;


    if (dev_cnt > 1)
    {
        start_vi(VIDEO_DEV1);
    }

    /*
        * step 2: osd main function 
        */
    osd_set(VIDEO_DEV0);
    
    if (dev_cnt > 1)
        osd_set(VIDEO_DEV1);

exit:
    /*
     * step 3: release resource
     */
    stop_vi(VIDEO_DEV0);
    if (dev_cnt > 1)
    {
        stop_vi(VIDEO_DEV1);
    }
    
    ak_print_normal(MODULE_ID_APP, "exit osd sample\n");
    ak_sdk_exit();

    return 0;
}

#ifdef AK_RTOS
SHELL_CMD_EXPORT_ALIAS(main, ak_osd_sample, osd sample)
#endif

