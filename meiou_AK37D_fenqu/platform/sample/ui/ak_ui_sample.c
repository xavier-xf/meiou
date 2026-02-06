#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>

#include <math.h>

#include <stdarg.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <linux/fb.h>

#include "ak_common_graphics.h"
#include "ak_mem.h"
#include "ak_log.h"
#include "ak_tde.h"
#include "ak_ui.h"
#include "ak_vdec.h"
#include "ak_thread.h"

#define MIPI_SC_WIDTH 1280
#define MIPI_SC_HEIGHT 800

#define RGB_SC_WIDTH  1024
#define RGB_SC_HEIGHT  600

#define LEN_HINT                512
#define LEN_OPTION_SHORT        512
#define MAX_DIS_NUM             4
#define LOG_WIDTH               577
#define LOG_HEIGHT              160
#define RECORD_READ_LEN         4096*25

static char *pc_prog_name = NULL;                                                      //demo名称
static int screen_flag    = 0;                                                         //mipi屏幕
static char *data_file = "/mnt/yuv";
static char *logo_file = "/mnt/anyka.logo.577.160.rgb";
static int display_num = 4;
static AK_GP_FORMAT data_format = GP_FORMAT_YUV420P;
static AK_GP_FORMAT out_format  = GP_FORMAT_RGB888;

/* struct to store the open vdec info */
struct file_ctrl
{
    int handle_id;
    char *file_path;
    int cycle_time;
};

static char ac_option_hint[  ][ LEN_HINT ] = {                                         //操作提示数组
    "打印帮助信息" ,
    "显示屏幕尺寸 [0 - 10寸屏, 1 - 7寸屏]" ,
    "解码文件路径目录" ,
    "解码循环次数" ,
    "解码数据格式 [0- h264 2 - h265]",
    //"[NUM] 数据输入格式 0:RGB565 1:RGB888 2:BGR565 3:BGR888 4:YUV420P 5:YUV420SP 6:ARGB8888 7:RGBA8888 8:ABGR8888 9:BGRA8888 10:TILED32X4" ,
    "[NUM] 数据输出格式 [0:RGB565 1:RGB888 2:BGR565 3:BGR888]",
    "logo 文件的路径，只支持提供的anyka.logo.577.160.rgb文件显示",
    " ",
};


static struct option option_long[ ] = {
    { "help"              , no_argument       , NULL , 'h' } ,      //"打印帮助信息" ,
    { "screen-type"       , required_argument , NULL , 't' } ,      //"显示屏幕类型" ,
    { "file-dir"          , required_argument , NULL , 'f' } ,      //"文件路径" ,
    { "circle-num"        , required_argument , NULL , 'n' } ,      //"分屏数目1-4" ,
    { "format-in"         , required_argument , NULL , 'i' } ,      //"[NUM] 数据输入格式 0:RGB565 1:RGB888 2:BGR565 3:BGR888 4:YUV420P 5:YUV420SP 6:ARGB8888 7:RGBA8888 8:ABGR8888 9:BGRA8888 10:TILED32X4" ,
    { "format-out"        , required_argument , NULL , 'o' } ,      //"[NUM] 数据输出格式 0:RGB565 1:RGB888 2:BGR565 3:BGR888 ,
    { "logo-file"         , required_argument , NULL , 'l' } ,      //"logo 文件的路径，只支持提供的anyka.logo.rgb文件显示"
    { 0                   , 0                 , 0    , 0   } ,
 };

/*
 * help_hint: use the -h --help option.Print option of help information
 * return: 0
 */
static void usage(const char * name)
{
    printf("\nexample: \r\n"
           "\teg: %s -t 0 -f /mnt/yuv -n 1 -i 0 -o 1 -l /mnt/anyka.logo.bgra8888\n", name);
    printf("\tthe logo file format should be argb8888\n");
}

static int help_hint(void)
{
    int i;

    printf( "%s\n" , pc_prog_name);
    printf( "Version: \r\n\t%s \n\n", ak_app_ui_get_version());
    printf( "Usage: \r\n\t%s  [options] <value>\n\n", pc_prog_name);

    /* parse the all supported option */
    printf( "options: \r\n");
    for(i = 0; i < sizeof(option_long) / sizeof(struct option); i ++) {
        if( option_long[ i ].val != 0 )
            printf("\t-%c, --%-16s\t%s\n" , option_long[ i ].val , option_long[ i ].name , ac_option_hint[ i ]);
    }

    usage(pc_prog_name);
    printf("\n\n");
    return AK_SUCCESS;
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

    for( i = 0 ; i < i_num_option ; i ++ )
    {
        if( ( c_option = p_option[ i ].val ) == 0 )
            continue;
        
        switch( p_option[ i ].has_arg )
        {
        case no_argument:
            i_offset += snprintf( pc_option_short + i_offset , i_len_option - i_offset , "%c" , c_option );
            break;
        case required_argument:
            i_offset += snprintf( pc_option_short + i_offset , i_len_option - i_offset , "%c:" , c_option );
            break;
        case optional_argument:
            i_offset += snprintf( pc_option_short + i_offset , i_len_option - i_offset , "%c::" , c_option );
            break;
        }
    }
    return pc_option_short;
}

static int parse_option( int argc, char **argv )
{
    int i_option;

    char ac_option_short[ LEN_OPTION_SHORT ];
    int i_array_num = sizeof( option_long ) / sizeof( struct option ) ;
    char c_flag = AK_TRUE;
    pc_prog_name = argv[ 0 ];

    optind = 0;
    get_option_short( option_long, i_array_num , ac_option_short , LEN_OPTION_SHORT );
    while((i_option = getopt_long(argc , argv , ac_option_short , option_long , NULL)) > 0) {
        switch(i_option) {
            case 'h' :                                                          //help
                help_hint();
                c_flag = AK_FALSE;
                goto parse_option_end;
            case 't' :                                                          //screen type 
                screen_flag = atoi( optarg );
                break;
            case 'f' :                                                          //data file path
                data_file = optarg;
                break;
            case 'n' :                                                          //display num
                display_num = atoi( optarg );
                break;
            case 'i' :                                                          //data_file format 
                data_format = atoi(optarg);
                break;
            case 'o' :                                                          //out put format
                out_format = atoi(optarg);
                break;
            case 'l' :                                                          //logo file
                logo_file = optarg;
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

/* add the  obj to GUI layer */
void *ak_fb_viraddr = NULL;
int ak_smem_len = 0;
struct ak_vo_layer_out gui_info;
struct fb_fix_screeninfo fb_fix;
struct fb_var_screeninfo fb_var;
int ak_fb_fd = -1;

/* print the fb var param */
static void print_vinfo(struct fb_var_screeninfo *vinfo)
{
	fprintf(stderr, "Printing vinfo:\n");
	fprintf(stderr, "\txres: %d\n", vinfo->xres);
	fprintf(stderr, "\tyres: %d\n", vinfo->yres);
	fprintf(stderr, "\txres_virtual: %d\n", vinfo->xres_virtual);
	fprintf(stderr, "\tyres_virtual: %d\n", vinfo->yres_virtual);
	fprintf(stderr, "\txoffset: %d\n", vinfo->xoffset);
	fprintf(stderr, "\tyoffset: %d\n", vinfo->yoffset);
	fprintf(stderr, "\tbits_per_pixel: %d\n", vinfo->bits_per_pixel);
	fprintf(stderr, "\tgrayscale: %d\n", vinfo->grayscale);
	fprintf(stderr, "\tnonstd: %d\n", vinfo->nonstd);
	fprintf(stderr, "\tactivate: %d\n", vinfo->activate);
	fprintf(stderr, "\theight: %d\n", vinfo->height);
	fprintf(stderr, "\twidth: %d\n", vinfo->width);
	fprintf(stderr, "\taccel_flags: %d\n", vinfo->accel_flags);
	fprintf(stderr, "\tpixclock: %d\n", vinfo->pixclock);
	fprintf(stderr, "\tleft_margin: %d\n", vinfo->left_margin);
	fprintf(stderr, "\tright_margin: %d\n", vinfo->right_margin);
	fprintf(stderr, "\tupper_margin: %d\n", vinfo->upper_margin);
	fprintf(stderr, "\tlower_margin: %d\n", vinfo->lower_margin);
	fprintf(stderr, "\thsync_len: %d\n", vinfo->hsync_len);
	fprintf(stderr, "\tvsync_len: %d\n", vinfo->vsync_len);
	fprintf(stderr, "\tsync: %d\n", vinfo->sync);
	fprintf(stderr, "\tvmode: %d\n", vinfo->vmode);
	fprintf(stderr, "\tred: %d/%d\n", vinfo->red.length, vinfo->red.offset);
	fprintf(stderr, "\tgreen: %d/%d\n", vinfo->green.length, vinfo->green.offset);
	fprintf(stderr, "\tblue: %d/%d\n", vinfo->blue.length, vinfo->blue.offset);
	fprintf(stderr, "\talpha: %d/%d\n", vinfo->transp.length, vinfo->transp.offset);
}

/* print the fb fix param */
static void print_finfo(struct fb_fix_screeninfo *finfo)
{
	fprintf(stderr, "Printing finfo:\n");
	fprintf(stderr, "\tsmem_start = %p\n", (char *)finfo->smem_start);
	fprintf(stderr, "\tsmem_len = %d\n", finfo->smem_len);
	fprintf(stderr, "\ttype = %d\n", finfo->type);
	fprintf(stderr, "\ttype_aux = %d\n", finfo->type_aux);
	fprintf(stderr, "\tvisual = %d\n", finfo->visual);
	fprintf(stderr, "\txpanstep = %d\n", finfo->xpanstep);
	fprintf(stderr, "\typanstep = %d\n", finfo->ypanstep);
	fprintf(stderr, "\tywrapstep = %d\n", finfo->ywrapstep);
	fprintf(stderr, "\tline_length = %d\n", finfo->line_length);
	fprintf(stderr, "\tmmio_start = %p\n", (char *)finfo->mmio_start);
	fprintf(stderr, "\tmmio_len = %d\n", finfo->mmio_len);
	fprintf(stderr, "\taccel = %d\n", finfo->accel);
}

/* get the dev and init the fb dev */
int ak_app_fb_init(char *fb_dev, int width, int height)
{
    /* open the dev */
    ak_fb_fd = open(fb_dev, O_RDWR);
    if (ak_fb_fd == -1)
    {
        printf("open fb failed\n");
        return -1;
    }
    else
        printf("success open fb\n");

    /* get the var param */
    if (ioctl(ak_fb_fd, FBIOGET_VSCREENINFO, &fb_var) < 0)
    {   
        close(ak_fb_fd);
        return -1;
    }

    /* set the color format ARGB8888 */
	fb_var.bits_per_pixel = 32;
	fb_var.red.offset = 16;
	fb_var.red.length = 8;
	fb_var.green.offset = 8;
	fb_var.green.length = 8;
	fb_var.blue.offset = 0;
	fb_var.blue.length = 8;
	fb_var.xres = width;
	fb_var.yres = height;

    /* set the param to fb dev */
    if (ioctl(ak_fb_fd, FBIOPUT_VSCREENINFO, &fb_var) < 0) 
	{
		ak_print_error_ex(MODULE_ID_VO, "Generate new FB VSCREENINFO failed\n");
        close(ak_fb_fd);
        return -1;
	}

    /* get the fix dev */
    if (ioctl(ak_fb_fd, FBIOGET_FSCREENINFO, &fb_fix) < 0)
    {
        close(ak_fb_fd);
        return -1;
    }

    /* get the fb dev vaddr */
    ak_smem_len = fb_fix.smem_len;
    ak_fb_viraddr = mmap(NULL, fb_fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, ak_fb_fd, 0);   /* map the dev */
    memset(ak_fb_viraddr, 0, ak_smem_len);                                                          /* set the vaddr */
    print_vinfo(&fb_var);
    print_finfo(&fb_fix);

    return 0;
}

int ak_app_fb_deinit(void)
{
    /* unmap the dev */
    if (ak_fb_viraddr != NULL)
    {
        munmap(ak_fb_viraddr, ak_smem_len);
        ak_fb_viraddr = NULL;
    }

    /* close the fb dec */
    if (ak_fb_fd != -1)
        close(ak_fb_fd);

    return 0;
}

static int add_logo(void)
{
    /* get the logo file */
    FILE *fp_logo = fopen(logo_file, "rb");
    if (NULL == fp_logo)
    {
        ak_print_error_ex(MODULE_ID_APP, "logo file open failed !!! This file is %s, check it!\n", logo_file);
        return AK_FAILED;
    }
    /* get the file size */
    fseek(fp_logo, 0, SEEK_END);
    int file_size = ftell(fp_logo);
    fseek(fp_logo, 0, SEEK_SET);

    /* read data from teh file */
    void *logo_data = ak_mem_dma_alloc(MODULE_ID_APP, file_size);
    if(logo_data == NULL)
    {
        ak_print_error_ex(MODULE_ID_APP, "Can't malloc DMA memory!\n");
        return AK_FAILED;
    }
    fread(logo_data, 1, file_size, fp_logo);                                /* get the logo data */

    /* calc the fb addr */
    int width = fb_var.xres;
    char *dst = (char *)ak_fb_viraddr;
    char *src = (char *)logo_data;
    for (int i = 0; i < LOG_HEIGHT; i++)
    {
        /* copy the dat to fb buff */
        memcpy(dst, src, LOG_WIDTH * 4);
        dst += (width * 4);             //fb buff 
        src += LOG_WIDTH * 4;           //logo buff
    }
    /* free source */
    ak_mem_dma_free(logo_data);
    fclose(fp_logo);

    return AK_SUCCESS;
}

static FILE* open_record_file(const char *record_file, int handle_id)
{
	FILE *fp = fopen(record_file, "r");
	if(NULL == fp) {
		ak_print_error_ex(MODULE_ID_APP, "open %s failed\n", record_file);
		return NULL;
	}

	ak_print_normal(MODULE_ID_APP, "open record file: %s OK, ID is [%d]\n", record_file, handle_id);
	return fp;
}


static void decode_and_play(int handle_id, unsigned char *data, int read_len)
{
    int send_len = 0;
    int dec_len = 0;
    int ret = 0;

    /* get the data and send to decoder */
    while (read_len > 0) 
    {
        /* send stream to decode */
        ret = ak_vdec_send_stream(handle_id, &data[send_len], read_len, 0, &dec_len);
        if (ret !=  AK_SUCCESS)
        {
            ak_print_error_ex(MODULE_ID_APP, "write video data failed!\n");
            break;
        }

        read_len -= dec_len;
        send_len += dec_len;
    }
}

static void read_record_file(int handle_id, int cycle_time, FILE *fp)
{
	int read_len = 0;
	int total_len = 0;
	unsigned char *data = (unsigned char *)ak_mem_alloc(MODULE_ID_APP, RECORD_READ_LEN);//[RECORD_READ_LEN] = {0};
	int count = cycle_time;
	
	ak_print_normal_ex(MODULE_ID_APP, "[%s][%d]\n",__func__,__LINE__);

    struct ak_timeval pre_tv;
    struct ak_timeval cur_tv;

do_again:
	/* set file from the beginning */
	fseek(fp, 0, SEEK_SET);
	
	while(AK_TRUE){

		/* read the record file stream */
        ak_get_ostime(&pre_tv);
		memset(data, 0x00, RECORD_READ_LEN);//sizeof(data));
		read_len = fread(data, sizeof(char), RECORD_READ_LEN, fp);

        ak_get_ostime(&cur_tv);
        long read_time = ak_diff_ms_time(&cur_tv, &pre_tv);
        if (read_time > 500)
            ak_print_error_ex(MODULE_ID_APP, "id [%d] read [%d] in [%ld]ms\n", handle_id, read_len, read_time);

		if(read_len > 0) {
			total_len += read_len;
			/* play loop */
			decode_and_play(handle_id, data, read_len);

		} else if(0 == read_len) {
			/* read to the end of file */
			ak_print_normal_ex(MODULE_ID_APP, "\n\tread to the end of file, id is [%d]\n", handle_id);
			break;
		} else {
			ak_print_error_ex(MODULE_ID_APP, "\nread, %s\n", strerror(errno));
			break;
		}
	}
    //printf("aaaaaaaaaaaa 777777\n");

	if(count == -1)
	{
		goto do_again;
	}
	else
	{
		count--;
		if (count != 0)
			goto do_again;
	}

	ak_vdec_end_stream(handle_id);
	if (data != NULL)
		ak_mem_free(data);
	ak_print_notice_ex(MODULE_ID_APP, "%s exit\n", __func__);
}

void *vdec_send_frame_th(void *arg)
{
	ak_thread_set_name("vdec_demoss");

	struct file_ctrl *handle = (struct file_ctrl *)arg;

	char *file =handle->file_path;

	/* open audio file */
	FILE *fp = open_record_file(file, handle->handle_id);
	if(NULL == fp) {
		ak_print_error_ex(MODULE_ID_APP, "id [%d] can't open file[%s]\n", handle->handle_id, file);
		return NULL;
	}

	/* read audio file stream and write to DA, use decode handle send stream */
    read_record_file(handle->handle_id, handle->cycle_time,fp);

	ak_print_normal_ex(MODULE_ID_APP, "decoder[%d] send stream th exit\n", handle->handle_id);//

	return NULL;
}

int main(int argc, char **argv)
{
    /* start the application */
    sdk_run_config config;
    memset(&config, 0, sizeof(sdk_run_config));
    config.mem_trace_flag = SDK_RUN_NORMAL;
    ak_sdk_init( &config );

    /* parse the option */
    if( parse_option( argc, argv ) == AK_FALSE )            //解释和配置选项
    {
        return AK_FAILED;                                           //打印帮助后退出
    }

    /* print the version */
    ak_print_normal(MODULE_ID_VI, "*****************************************\n");
    ak_print_normal(MODULE_ID_VI, "** vo version: %s **\n", ak_vo_get_version());
    ak_print_normal(MODULE_ID_VI, "** ui version: %s **\n", ak_app_ui_get_version());
    ak_print_normal(MODULE_ID_VI, "*****************************************\n");

    /* store the file path */
    int ret = 0;
    /* get the res and rotation */
    int dst_width = 0;
    int dst_height = 0;
    int rotate = 0;

    /* get the screen res */
    if (screen_flag == 1)
    {
        /* 1 means for RGB */
        dst_width = RGB_SC_WIDTH;
        dst_height = RGB_SC_HEIGHT;
        rotate    = AK_GP_ROTATE_NONE;
    }
    else if (screen_flag == 0)
    {
        /* 0 means for MIPI */
        dst_width = MIPI_SC_WIDTH;
        dst_height = MIPI_SC_HEIGHT;
        rotate = AK_GP_ROTATE_90;
    }
    else
    {
        ak_print_error_ex(MODULE_ID_APP, "screen type is invalid\n");
        return AK_FAILED;
    }


    /* fill the struct to open the vo */
    /* fill the struct ready to open */
    ret = ak_app_screen_init(dst_width, dst_height, rotate);

    if(ret != 0)
    {
        /* open failed return -1 */
        ak_print_error_ex(MODULE_ID_APP, "ak_vo_open failed![%d]\n",ret);
        return AK_FAILED;    
    }

    /* create the video layer */
    /* vo layer create api */
    int video_win_id;
    AK_RECT rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = dst_width;
    rect.height = dst_height;
    ret = ak_app_win_create(&video_win_id, rect, GP_FORMAT_RGB565);//video;
    if(ret != 0)
    {
        /* if failed, close the vo */
        ak_print_error_ex(MODULE_ID_APP, "ak_vo_create_video_layer failed![%d]\n",ret);
        ak_app_screen_deinit();       // if failed, close the screen
        return AK_FAILED;    
    }

    /* create the gui layer */
    /* only support the given log file */
    //struct ak_vo_layer_out gui_info;                /* output the gui layer info */
    int ui_win_id;
    ret = ak_app_win_create(&ui_win_id, rect, GP_FORMAT_RGB888);
    if(ret != 0)
    {
        ak_print_error_ex(MODULE_ID_APP, "ak_vo_create_gui_layer failed![%d]\n",ret);
        ak_app_win_destroy(video_win_id);
        ak_app_screen_deinit();
        return AK_FAILED;
    }

    /* open vdec */
    struct ak_vdec_param param = {0};
    param.vdec_type = data_format;
    param.sc_height = 1080;
    param.sc_width = 1920;
    param.output_type = AK_TILE_YUV;
    int vdec_id = -1;
    /* open the vdec */
    ret = ak_vdec_open(&param, &vdec_id);
    
    /* use the double buff mode */

    ak_app_fb_init("/dev/fb1", dst_width, dst_height);
    ak_app_ui_init("/dev/fb1");
    ak_app_ui_set_colorkey(0xffffffff);             //ui colorkey white color
    /* support the yuv 420p */
    /* get the size of the pixel */
    
    add_logo();

    /* flush to screen */
    ak_app_attach_video_to_win(video_win_id, vdec_id);
    ak_app_show_win(video_win_id);
    ak_app_show_win(ui_win_id);
    ak_app_ui_update(ui_win_id);

    ak_pthread_t tid_stream_th;
    /* struct to store the open vdec info */
    struct file_ctrl vdec_test;
    vdec_test.handle_id = vdec_id;
    vdec_test.file_path = data_file;
    vdec_test.cycle_time = display_num;
    ak_thread_create(&tid_stream_th, vdec_send_frame_th, &vdec_test, ANYKA_THREAD_MIN_STACK_SIZE, -1);

    ak_sleep_ms(10000);
    ak_app_hide_win(ui_win_id);
    ak_sleep_ms(3000);
    ak_app_show_win(ui_win_id);

    int status = 0;
    while (!status)
    {
        ak_vdec_get_decode_finish(vdec_id, &status);
        if (status)
        {
            ak_print_normal(MODULE_ID_VDEC, "status [%d]\n", status);
            break;
        }
        ak_sleep_ms(1000);
    }

    ak_thread_join(tid_stream_th);
    ak_app_detach_video_to_win(video_win_id, vdec_id);

    /* if err or finish, release the src */
    ak_app_win_destroy(ui_win_id);
    ak_app_win_destroy(video_win_id);
    ak_app_screen_deinit();

    ak_app_ui_deinit();
    ak_app_fb_deinit();
    ak_vdec_close(vdec_id);
    ak_sdk_exit();

    return ret;
}
