#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include "ak_common.h"
#include "ak_log.h"
#include "ak_common_video.h"
#include "ak_venc.h"
#include "ak_thread.h"
#include "ak_mem.h"
#include "ak_vi.h"
#include "ak_video.h"

#ifdef AK_RTOS
#include <kernel.h>
#define THREAD_PRIO 90
#define __STATIC__  static
#else
#define THREAD_PRIO -1
#define __STATIC__ 
#endif

/* video resolution num */
#define DE_VIDEO_SIZE_MAX    8

/* this is length for parse */
#define LEN_HINT                512
#define LEN_OPTION_SHORT        512

#define MAX_ENC_NUM   4

#ifndef __CHIP_AK37E_SERIES
#define THIRD_CHN_SUPPORT	1
#define DEF_FRAME_DEPTH		2
#else
#define THIRD_CHN_SUPPORT	0
#define DEF_FRAME_DEPTH		3
#endif

struct venc_pair
{
    int venc_handle;
    char save_file[128];
    int en_type;
    int dev_id;
};

/* resolation define */
struct resolution_t {
	unsigned int width;
	unsigned int height;
	unsigned char str[20];
};

static char *pc_prog_name = NULL;                      //demo名称
static char *type         = "h264";                      //get the type input
static char *save_path    = "/mnt/video_encode";
static char main_res       = 4;
static char sub_res        = 0;
static int frame_num   = 1000;
static char chn_index = 0;
static char br_mode = BR_MODE_VBR;
//static char init_qp = 100;
static int target_kbps = 1024;
static char vi_fps = 20;

#ifdef AK_RTOS
static char *cfg = "/etc/config/isp_5268_mipi.conf";
#else
static char *cfg = "/etc/config/isp_pr2000_dvp.conf";
#endif
ak_mutex_t refresh_flag_lock;
static int dma_pool_enable=0;
static ak_pthread_t venc_stream_th[2];
static int dev_cnt = 1;
static VI_CHN_ATTR chn_attr = {0};
static VI_CHN_ATTR chn_attr_sub = {0};
static int encoder_type = 0;
static struct venc_pair enc_pair[2] = {{-1}, {-1}};

/* decoder resolution */
static struct resolution_t resolutions[DE_VIDEO_SIZE_MAX] = {
    {640,   360,   "DE_VIDEO_SIZE_360P"},
    {640,   480,   "DE_VIDEO_SIZE_VGA"},
    {1280,  720,   "DE_VIDEO_SIZE_720P"},
    {960,  1080,   "DE_VIDEO_SIZE_960"},
    {1920,	1080,  "DE_VIDEO_SIZE_1080P"},
    {2304,	1296,  "DE_VIDEO_SIZE_1296P"},
    {2560,	1440,  "DE_VIDEO_SIZE_1440P"},
    {2560,  1920,  "DE_VIDEO_SIZE_1920P"}
};

/* this is the message to print */
static char ac_option_hint[  ][ LEN_HINT ] = {                                         //操作提示数组
    "打印帮助信息" ,
    "从vi获取的帧的数量",
    "编码输出数据格式, [h264, h265, jpeg], 37e only support jpeg ",
    //"编码质量(qp) [0, 100]",
    "编码模式 0:cbr , 1:vbr",
	"目标码率, 单位:kbps, [大于 0]",
    "帧率, 大于 0，小于sensor实际帧率",
    "主通道分辨率, [0, 7], 37e[0, 1]",
    "次通道分辨率 [0, 3]. note:need smaller than main channel resolution",
    "编码数据保存成文件的目录",
    "vi 通道选择, [0-main, 1-sub]",
    "dma pool enable, [0-disable, 1-enable]",
    "isp conf file path",
	"[NUM] device count [1 - 2], default 1" ,
    "",
};

/* opt for print the message */
static struct option option_long[ ] = {
    { "help"              , no_argument       , NULL , 'h' } ,      //"打印帮助信息" ,
    { "frame-num"         , required_argument , NULL , 'n' } ,      //"从vi获取的帧的数量" ,
    { "data-output"       , required_argument , NULL , 'o' } ,      //"编码输出数据格式" ,
    //{ "qp"                , required_argument , NULL , 'q' } ,      //"编码质量" ,
    { "br_mode"           , required_argument , NULL , 'b' } ,      //"编码br模式" ,
	{ "target_kbps"       , required_argument , NULL , 't' } ,      //"目标码率" ,
    { "frame-rate"        , required_argument , NULL , 'r' } ,      //"帧率" ,
    { "main_res  "        , required_argument , NULL , 'm' } ,      //"主通道分辨率" ,
    { "sub_res  "         , required_argument , NULL , 's' } ,      //"次通道分辨率" ,
    { "save-path"         , required_argument , NULL , 'p' } ,      //"编码数据保存成文件的目录" ,
    { "channel"           , required_argument , NULL , 'a' } ,      //"vi 输出通道选择" ,
    { "dma_pool_enable"   , required_argument , NULL , 'd' } ,      //"DMA_POOL" ,
    { "isp conf path"     , required_argument , NULL , 'c' } ,      //"isp 配置文件或yuv数据源文件目录"
    { "dev_cnt"           , required_argument , NULL , 'v' } ,
	{ 0                   , 0                 , 0    , 0   } ,
};

/*
* get_option_short: fill the stort option string.
* return: option short string addr.
*/
static char *get_option_short( struct option *p_option, int i_num_option, char *pc_option_short, int i_len_option )
{
    int i;
    int i_offset = 0;
    char c_option;

    /* get the option */
    for( i = 0 ; i < i_num_option ; i ++ )
    {
        if( ( c_option = p_option[ i ].val ) == 0 ) 
        {
            continue;
        }

        switch( p_option[ i ].has_arg )
        {
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

static void usage(const char * name)
{
    printf("eg.: %s -n 1000 -o h264 -b 1 -t 1024 -r 20 -m 4 -s 1 -p /mnt/video -a 0 -c /etc/config/isp_xx.conf\n", name);
    printf("encode data type -o:          h264,  h265,  or  jpeg\n");
    printf("resolution -m -s:     value 0 ~ 7\n");
    printf("                      0 - 640*360\n");
    printf("                      1 - 640*480\n");
    printf("                      2 - 1280*720\n");
    printf("                      3 - 960*1080\n");
    printf("                      4 - 1920*1080\n");
    printf("                      5 - 2304*1296\n");
    printf("                      6 - 2560*1440\n");
    printf("                      7 - 2560*1920\n");
    printf("save path -p:       \n");
    printf("output channel -a:      0-main 1-sub \n");
    printf("dma_pool_enable -d:     0-disable 1-enable \n");
    printf("isp config file -c:       \n");
}

/* if opt is not supported, print the help message */
static int help_hint(void)
{
    int i;
 
    printf("%s\n" , pc_prog_name);
    /* parse the all supported option */
    for(i = 0; i < sizeof(option_long) / sizeof(struct option); i ++)
    {
        if( option_long[ i ].val != 0 ) 
            printf("\t--%-16s -%c %s\n" ,
                                                option_long[ i ].name , option_long[ i ].val , ac_option_hint[ i ]);
    }
    usage(pc_prog_name);
    printf("\n\n");
    return AK_SUCCESS;
}

/* parse the option from cmd line */
static int parse_option( int argc, char **argv )
{
    int i_option;
 
    char ac_option_short[ LEN_OPTION_SHORT ];
    int i_array_num = sizeof( option_long ) / sizeof( struct option ) ; /* get the option num*/
    char c_flag = AK_TRUE;
    pc_prog_name = argv[ 0 ];   /* get the option */

	optind = 0;
    get_option_short( option_long, i_array_num , ac_option_short , LEN_OPTION_SHORT );  /* parse the cmd line input */
    while((i_option = getopt_long(argc , argv , ac_option_short , option_long , NULL)) > 0)
    {
        switch(i_option)
        {
        case 'h' :                                                          //help
            help_hint();
            c_flag = AK_FALSE;
            goto parse_option_end;
        case 'n' :                                                          //data file path
            frame_num = atoi( optarg );
            break;
        case 'o' :                                                          //data_file format 
            type = optarg;
            break;
        case 'm' :                                                          //decode num 
            main_res= atoi( optarg );
            break;
        case 's' :                                                          //decode num 
            sub_res = atoi( optarg );
            break;
        case 'p' :                                                          //decode num 
            save_path = optarg;
            break;
        case 'c' :                                                          //decode num 
            cfg =  optarg;
            break;
        case 'a' :                                                          //decode num 
            chn_index =  atoi( optarg );
            break;
		case 'd' :                                                          //decode num 
            dma_pool_enable = atoi( optarg );
            break;
		//case 'q' :                                                          //encode mode 
            //init_qp =  atoi( optarg );
            //break;
	    case 'b' :                                                          //encode mode 
            br_mode =  atoi( optarg );
            break;
		case 't' :                                                          //target kbps 
            target_kbps =  atoi( optarg );
            break;
		case 'r' :                                                          //frame rate 
            vi_fps =  atoi( optarg );
            break;
        case 'v' :
            dev_cnt = atoi(optarg);
            break;
        default :
            help_hint();
            c_flag = AK_FALSE;
            goto parse_option_end;
        }
    }
parse_option_end:
    return c_flag;
}

static int start_vi(int dev_id)
{
    /* 
     * step 0: global value initialize
     */
    int ret = -1;                                //return value
    int width = resolutions[main_res].width;
    int height = resolutions[main_res].height;
    int subwidth = resolutions[sub_res].width;;
    int subheight = resolutions[sub_res].height;
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

#ifndef __CHIP_AK37E_SERIES
    /*
     * step 2: load isp config
     */
    ret = ak_vi_load_sensor_cfg(dev_id, cfg);
    if (AK_SUCCESS != ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d load isp cfg [%s] failed!\n", dev_id, cfg);    
        return ret;
    }
#endif
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
    
    memset(&chn_attr, 0, sizeof(VI_CHN_ATTR));
    chn_attr.chn_id = chn_main_id;
    chn_attr.res.width = width;
    chn_attr.res.height = height;
    chn_attr.frame_depth = DEF_FRAME_DEPTH;
    /*disable frame control*/
    chn_attr.frame_rate = vi_fps;
    ret = ak_vi_set_chn_attr(chn_main_id, &chn_attr);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d set channel [%d] attribute failed!\n", dev_id, chn_main_id);
        ak_vi_close(dev_id);
        return ret;
    }
    ak_print_normal_ex(MODULE_ID_APP, "vi device %d main sub channel attribute\n", dev_id);

#ifndef __CHIP_AK37E_SERIES
    /*
     * step 6: set sub channel attribute
     */
    
    memset(&chn_attr_sub, 0, sizeof(VI_CHN_ATTR));
    chn_attr_sub.chn_id = chn_sub_id;
    chn_attr_sub.res.width = subwidth;
    chn_attr_sub.res.height = subheight;
    chn_attr_sub.frame_depth = DEF_FRAME_DEPTH;
    /*disable frame control*/
    chn_attr_sub.frame_rate = vi_fps;
    ret = ak_vi_set_chn_attr(chn_sub_id, &chn_attr_sub);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d set channel [%d] attribute failed!\n", dev_id, chn_sub_id);
        ak_vi_close(dev_id);
        return ret;
    }
    ak_print_normal_ex(MODULE_ID_APP, "vi device %d set sub channel attribute\n", dev_id);
#endif

	/* 
     * step 8: enable vi device 
     */
    ret = ak_vi_enable_dev(dev_id);
    if (ret) {
        ak_print_error_ex(MODULE_ID_APP, "vi device %d enable device  failed!\n", dev_id);
        ak_vi_close(dev_id);
        return ret;
    }

    /* 
     * step 9: enable vi main channel 
     */
    ret = ak_vi_enable_chn(chn_main_id);
    if(ret)
    {
        ak_print_error_ex(MODULE_ID_APP, "vi channel[%d] enable failed!\n", chn_main_id);
        ak_vi_close(dev_id);
        return ret;
    }

#ifndef __CHIP_AK37E_SERIES
    /* 
     * step 10: enable vi sub channel 
     */
    ret = ak_vi_enable_chn(chn_sub_id);
    if(ret)
    {
        ak_print_error_ex(MODULE_ID_APP, "vi channel[%d] enable failed!\n",chn_sub_id);
        ak_vi_close(dev_id);
        return ret;
    }
#endif
    return 0;
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
    
    ak_vi_disable_chn(chn_main_id);

#ifndef __CHIP_AK37E_SERIES
    ak_vi_disable_chn(chn_sub_id);
#endif

	ak_vi_disable_dev(dev_id);
    ret = ak_vi_close(dev_id);

    return ret;
}


/* get the encode data from vi module */
static void *video_encode_from_vi_th(void *arg)
{
    ak_thread_set_name("venc_th");
    struct venc_pair *venc_th = (struct venc_pair *)arg; //venc handle
    
    FILE *save_fp = NULL;
    struct video_input_frame frame;
    VI_CHN chn_id = (chn_index == 0) ?  VIDEO_CHN0 : VIDEO_CHN1;

    if (venc_th->dev_id)
        chn_id += 2;

    ak_print_normal(MODULE_ID_APP, "capture start\n");

	RECTANGLE_S res;
	res.width = resolutions[main_res].width;
	res.height = resolutions[main_res].height;
	if(chn_index)
	{
		res.width = resolutions[sub_res].width;
		res.height = resolutions[sub_res].height;
	}

    /* init the save file name and open it */
    memset(venc_th->save_file, 0x00, sizeof(venc_th->save_file));
    if (H264_ENC_TYPE == venc_th->en_type)
        sprintf(venc_th->save_file, "%s/h264_br%d_%dkbps_chn%d_%dx%d.str", save_path, br_mode,target_kbps,chn_id,res.width, res.height);
    else
        sprintf(venc_th->save_file, "%s/h265_br%d_%dkbps_chn%d_%dx%d.str", save_path, br_mode,target_kbps,chn_id, res.width, res.height);

    /* save file open */
    if (MJPEG_ENC_TYPE != venc_th->en_type)
    {
        save_fp = fopen(venc_th->save_file, "w+");
		if(save_fp == NULL)
		{
			ak_print_error_ex(MODULE_ID_APP, "fopen failed, errno = %d.\n", errno);
			return NULL;
		}
    }

    APP_CHN_S Schn;
	APP_CHN_S Dchn;
	Schn.mid = MODULE_ID_VI;
	Schn.oid = chn_id;
	Dchn.mid = MODULE_ID_VENC;
	Dchn.oid = venc_th->venc_handle;
	APP_BIND_PARAM param;
	param.frame_rate = vi_fps;
	param.frame_depth = 200;

	ak_print_normal_ex(MODULE_ID_APP, "Encoder [%d] bind chn!\n",Dchn.oid);	
	int ret = ak_app_video_bind_chn(&Schn, &Dchn, &param);
	if(ret != AK_SUCCESS)
	{
		ak_print_error_ex(MODULE_ID_APP, "ak_app_video_bind_chn failed [%d]\n",ret);
		return NULL;
	}

	/* Force request I frame */
	ak_venc_request_idr(venc_th->venc_handle);	

	ak_print_normal_ex(MODULE_ID_APP, "Encoder [%d] activate chn!\n",Dchn.oid);	
	ret = ak_app_video_set_dst_chn_active(&Dchn, AK_TRUE);

	struct video_stream *stream = ak_mem_alloc(MODULE_ID_APP, sizeof(struct video_stream));
	if(NULL == stream)
	{
		ak_print_error_ex(MODULE_ID_APP, "Can't alloc memory for stream buffer!\n");
		return NULL;
	}

	int frame_c = frame_num;
	while (frame_c > 0)
	{
		memset(stream, 0, sizeof(struct video_stream));

        /* if save as jpeg data */
        if (MJPEG_ENC_TYPE == venc_th->en_type)
        {
            memset(venc_th->save_file, 0x00, sizeof(venc_th->save_file));
            sprintf(venc_th->save_file, "%s/chn%d_%dx%d_num_%d.jpeg", save_path, chn_id, res.width, res.height, frame_num - frame_c);
            save_fp = fopen(venc_th->save_file, "w+");
        }
        
		ret = ak_app_video_venc_get_stream(&Dchn, stream);
		if(ret == AK_SUCCESS)
		{
			//ak_print_normal_ex(MODULE_ID_APP, "ak_app_video_venc_get_stream success!\n");
			if(save_fp)
				fwrite(stream->data, stream->len, 1, save_fp);
			ak_venc_release_stream(venc_th->venc_handle, stream);
			frame_c--;
		}
		else
		{
			//ak_print_error_ex(MODULE_ID_APP, "ak_app_video_venc_get_stream fail [%x]!\n",ret);
			ak_sleep_ms(10);
		}

        /* save as a jpeg picture */
        if (MJPEG_ENC_TYPE == venc_th->en_type)
        {
            if (NULL != save_fp)
            {
                fclose(save_fp);
                save_fp = NULL;
            }
        }
	}

	if(stream)
		ak_mem_free(stream);

	ak_print_normal_ex(MODULE_ID_APP, "Encoder [%d] disable chn!\n", Dchn.oid);	
	ret = ak_app_video_set_dst_chn_active(&Dchn, AK_FALSE);

	ak_print_normal_ex(MODULE_ID_APP, "Encoder [%d] unbind chn!\n", Dchn.oid );	
	ret = ak_app_video_unbind_chn(&Schn, &Dchn);
	if(ret != AK_SUCCESS)
	{
		ak_print_error_ex(MODULE_ID_APP, "ak_app_video_unbind_chn failed [%d]\n",ret);
	}

    /* finished */
    if (MJPEG_ENC_TYPE != venc_th->en_type)
    {
        if(save_fp)
            fclose(save_fp);
    }

    return NULL;
}

static int start_venc(int dev_id)
{
    int ret = -1;
    int width = resolutions[main_res].width;
    int height = resolutions[main_res].height;
    int subwidth = resolutions[sub_res].width;;
    int subheight = resolutions[sub_res].height;
    int handle_id[2] = {-1, -1};
    
    

    /* open venc */
    struct venc_param ve_param;

    if (chn_index == 0)
    {
        ve_param.width  = width;            //resolution width
        ve_param.height = height;           //resolution height
    }
    else
    {
        ve_param.width  = subwidth;            //resolution width
        ve_param.height = subheight;           //resolution height
    }

    ve_param.fps    = vi_fps;               //fps set
    ve_param.goplen = 2*vi_fps;               //gop set
    ve_param.target_kbps = target_kbps;         //k bps
    ve_param.max_kbps    = 2048;        //max kbps
    ve_param.br_mode     = br_mode; //br mode
    ve_param.minqp       = 25;          //qp set
    ve_param.maxqp       = 50;          //qp max value
    ve_param.initqp       = (ve_param.minqp + ve_param.maxqp)/2;    //qp value
    ve_param.jpeg_qlevel = JPEG_QLEVEL_DEFAULT;     //jpeg qlevel
    ve_param.chroma_mode = CHROMA_4_2_0;            //chroma mode
    ve_param.max_picture_size = 0;                  //0 means default
    ve_param.enc_level        = 30;                 //enc level
    ve_param.smart_mode       = 0;                  //smart mode set
    ve_param.smart_goplen     = 100;                //smart mode value
    ve_param.smart_quality    = 50;                 //quality
    ve_param.smart_static_value = 0;                //value
    ve_param.enc_out_type = encoder_type;           //enc type

    /* decode type is h264*/
    if(encoder_type == H264_ENC_TYPE)
    {
        /* profile type */
        ve_param.profile     = PROFILE_MAIN;
        
    }
    /*decode type is jpeg*/
    else if(encoder_type == MJPEG_ENC_TYPE)
    {
        /* jpeg enc profile */
        ve_param.profile = PROFILE_JPEG;
    }
    /*decode type is h265*/
    else
    {
        /* hevc profile */
        ve_param.profile = PROFILE_HEVC_MAIN;
    }

    ret = ak_venc_open(&ve_param, &handle_id[dev_id]);

    if (ret || (-1 == handle_id[dev_id]) )
    {
        ak_print_error_ex(MODULE_ID_APP, "dev%d open venc failed\n", dev_id);
        return ret;
    }

    if (ve_param.width > 1920 && ve_param.height > 1080 && ve_param.initqp > 60)
    {
        ak_venc_set_stream_buff(handle_id[dev_id], 1*1024*1024);
    }
    
	if(dma_pool_enable)
	{
		unsigned long total = 0;
		ak_mem_dma_pool_get_used_mem(MODULE_ID_VENC, &total);
		ak_print_notice_ex(MODULE_ID_APP, "VENC Dma pool used total [%lu] bytes!", total);
		total = 0;
		ak_mem_dma_pool_get_free_mem(MODULE_ID_VENC, &total);
		ak_print_notice_ex(MODULE_ID_APP, "VENC Dma pool free total [%lu] bytes!", total);
	}

    /* recode the file */
    //enc_pair.file = file;
    enc_pair[dev_id].venc_handle = handle_id[dev_id];
    enc_pair[dev_id].en_type     = encoder_type;
    enc_pair[dev_id].dev_id = dev_id;

    /* create the venc thread */
    ak_thread_create(&venc_stream_th[dev_id], video_encode_from_vi_th, &enc_pair[dev_id], ANYKA_THREAD_MIN_STACK_SIZE, THREAD_PRIO);

    return 0;
}

/**
 * note: read the appointed video file, decode, and then output to vo  to playing
 */
__STATIC__ int main(int argc, char **argv)
{
    /* init sdk running */
    sdk_run_config config;
    memset(&config, 0, sizeof(config));             //init the struct
    config.mem_trace_flag = SDK_RUN_NORMAL;
    config.isp_tool_server_flag = 0;                //isp tool sever
    ak_sdk_init( &config );

    ak_print_normal(MODULE_ID_APP, "*****************************************\n");
    ak_print_normal(MODULE_ID_APP, "** venc version: %s **\n", ak_venc_get_version());
    ak_print_normal(MODULE_ID_APP, "*****************************************\n");

    /* start to parse the opt */
    if( parse_option( argc, argv ) == AK_FALSE )
    {                                                                           //解释和配置选项
        return AK_FAILED;
    }
	
	if(frame_num <= 0)
	{
		ak_print_error_ex(MODULE_ID_APP, "frame num must be positive.\n");
		return AK_FAILED;
	}
	
    /* check the res */
    if (main_res < 0 || main_res > DE_VIDEO_SIZE_MAX-1 
            || sub_res < 0 || sub_res > DE_VIDEO_SIZE_MAX-1 
                || main_res < sub_res)
    {
        ak_print_error_ex(MODULE_ID_APP, "resolution out of range, check it\n");
        return AK_FAILED;
    }

	/* check the br mode */
	if (br_mode < BR_MODE_CBR || br_mode > BR_MODE_LOW_LATENCY )
	{
		ak_print_error_ex(MODULE_ID_VENC, "br mode out of range, check it\n");
		return AK_FAILED;
	}

    /* get the type for output */
    if (type == NULL)
    {
        ak_print_error_ex(MODULE_ID_APP, "please input the type\n");
        /* print the hint and notice */
        help_hint();
        return AK_FAILED;
    }

    /* check the chn index value */
    if (chn_index < 0 || chn_index > 1)
    {
        ak_print_error_ex(MODULE_ID_APP, "please input the correct param\n");
        /* print the hint and notice */
        help_hint();
        return AK_FAILED;
    }

	/* check the chn index value */
	if (target_kbps < 1 || target_kbps > 2048)
	{
		ak_print_error_ex(MODULE_ID_VENC, "please input the correct target_kbps (1 ~ 2048)\n");
		/* print the hint and notice */
		help_hint();
		return AK_FAILED;
	}	

    if(dev_cnt < 1 || dev_cnt > 2)
    {
        ak_print_error_ex(MODULE_ID_APP,"dev_cnt error! use default value 1.\n");
        dev_cnt = 1;
    }

    /* get the type for input */
    if(!strcmp(type,"h264"))
    {
        /* h264 */
        ak_print_normal(MODULE_ID_APP, "encode type: h264\n");
        encoder_type = H264_ENC_TYPE;
    }
    else if(!strcmp(type,"h265"))
    {
        /* h265 */
        ak_print_normal(MODULE_ID_APP, "encode type:h265 \n");
        encoder_type = HEVC_ENC_TYPE;
    }
    else if(!strcmp(type,"jpeg"))
    {
        /* jpeg */
        ak_print_normal(MODULE_ID_APP, "encode type:jpeg\n");
        encoder_type = MJPEG_ENC_TYPE;
    }
    else
    {
        ak_print_normal_ex(MODULE_ID_APP, "unsupport video enccode input type [%s] \n", type);
        return AK_FAILED;
    }

    if (access(save_path, W_OK) != 0)
    {
        if (mkdir(save_path, 0755)) 
        {
            ak_print_error_ex(MODULE_ID_APP, "mkdir: %s failed\n", save_path);
            return AK_FAILED;
        }
    }

	if(dma_pool_enable)
	{
		int vi_dma_size = 0;	//M bytes;
		int venc_dma_size = 0;	//M bytes;

		/* below is a rough calc method, and based on the crop res of sensor is equal or less than the main channel res*/
		/* 3MP res, 20 for vi and 12 for venc */
		if(main_res <= 5)		
		{
			vi_dma_size = 20;
			venc_dma_size = 12;	
		}
		/*above 3MP res, 32 for vi and 20 for venc */
		else
		{
			vi_dma_size = 32;
			venc_dma_size = 20;
		}

        if (2 == dev_cnt)
        {
            vi_dma_size *= 2;
            venc_dma_size *= 2;
        }
        
		ak_mem_dma_pool_create(MODULE_ID_VI, vi_dma_size*1024*1024 - 300*1024); /* 300KB left for OSD and audio*/ 
		ak_mem_dma_pool_create(MODULE_ID_VENC, venc_dma_size *1024*1024);
		ak_mem_dma_pool_activate();
	}

    /* get param from cmd line */
    int ret = -1;

    ret = start_vi(VIDEO_DEV0);
	if (dev_cnt > 1)
    {
         ret |= start_vi(VIDEO_DEV1);
    }

    if(ret)
    {
        ak_print_error_ex(MODULE_ID_APP, "vi init failed!\n");
        goto erro;
    }

	if(dma_pool_enable)
	{
		unsigned long total = 0;
		ak_mem_dma_pool_get_used_mem(MODULE_ID_VI, &total);
		ak_print_notice_ex(MODULE_ID_APP, "VI Dma pool used total [%lu] bytes!", total);
		total = 0;
		ak_mem_dma_pool_get_free_mem(MODULE_ID_VI, &total);
		ak_print_notice_ex(MODULE_ID_APP, "VI Dma pool free total [%lu] bytes!", total);
	}

    ret = start_venc(VIDEO_DEV0);
    if (ret)
    {
        goto erro;
    }

    if (dev_cnt > 1)
    {
        start_venc(VIDEO_DEV1);
    }


    /* WAITER for the thread exit */
    ak_thread_join(venc_stream_th[0]);

    if (dev_cnt > 1)
        ak_thread_join(venc_stream_th[1]);

erro:
    /* close the video encoder and video decoder */
    if(enc_pair[0].venc_handle != -1)
    {
        /* close the venc*/
        ak_venc_close(enc_pair[0].venc_handle);
        enc_pair[0].venc_handle = -1;
    }

    if (dev_cnt > 1)
    {
        if(enc_pair[1].venc_handle != -1)
        {
            /* close the venc*/
            ak_venc_close(enc_pair[1].venc_handle);
            enc_pair[1].venc_handle = -1;
        }
    }


    stop_vi(VIDEO_DEV0);
    
    if (dev_cnt > 1)
    {
        stop_vi(VIDEO_DEV1);
    }

	if(dma_pool_enable)
	{
		ak_mem_dma_pool_exit();
	}

	ak_sdk_exit();

    return ret;
}

#ifdef AK_RTOS
SHELL_CMD_EXPORT_ALIAS(main, ak_venc_sample, Video Encode Sample Program);
#endif

/* end of file */

