/************************************************************************
* pwm demo:
* -d:device_no(0-4),other value is invalid
************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include "ak_common.h"
#include "ak_log.h"
#include "ak_drv_pwm.h"

#ifdef AK_RTOS
#include <rtthread.h>
#endif

#define LEN_HINT         512
#define LEN_OPTION_SHORT 512

static char *pc_prog_name = NULL;  
static int dev_no = 0;//[0, 4]
static int duty_ns = 0;
static int period_ns = 0;



static char ac_option_hint[  ][ LEN_HINT ] = {                                         //操作提示数组
    "help info" ,
    "[NUM] [0-4]pwm dev_no" ,
    "[NUM] duty in ns" ,
    "[NUM] period in ns" ,
    "" ,
};

static struct option option_long[ ] = {
    { "help"      , no_argument       , NULL , 'h' } ,    //"help info"
    { "dev_no"   , required_argument , NULL , 'a' } ,    //"[NUM] [0-4]pwm dev_no" 
    { "dutyns" , required_argument , NULL , 'b' } ,        //"[NUM] duty in ns"
    { "periodns" , required_argument , NULL , 'c' } ,    //"[NUM] period in ns"
    {0, 0, 0, 0}
 };

static void usage(const char * name)
{
    ak_print_normal(MODULE_ID_APP," %s -a [dev_no] -b [dutyns] -c [periodns]\n", name);
    ak_print_normal(MODULE_ID_APP,"eg: %s -a 0 -b 500000 -c 1000000\n", name);
}

/*
 * help_hint: use the -h --help option.Print option of help information
 * return: 0
 */
static int help_hint(char *pc_prog_name)
{
    int i;

    printf("%s\n" , pc_prog_name);
    for(i = 0; i < sizeof(option_long) / sizeof(struct option); i ++) {
        if( option_long[ i ].val != 0 ) {
            printf("\t--%-16s -%c %s\n" , option_long[ i ].name , option_long[ i ].val , ac_option_hint[ i ]);
        }
    }

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

    for( i = 0 ; i < i_num_option ; i ++ ) {
        if( ( c_option = p_option[ i ].val ) == 0 ) {
            continue;
        }
        switch( p_option[ i ].has_arg ){
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
    char c_flag = 1;
    pc_prog_name = argv[ 0 ];

    get_option_short( option_long, i_array_num , ac_option_short , LEN_OPTION_SHORT );
    while((i_option = getopt_long(argc , argv , ac_option_short , option_long , NULL)) > 0) {
        switch(i_option) {
        case 'h' :  //help
            help_hint(argv[0]);
            c_flag = 0;
            goto parse_option_end;
        case 'a' :  //dev_no
            dev_no = atoi(optarg);
            break;
        case 'b' :  //duty_ns
            duty_ns = atoi(optarg);
            break;
        case 'c' :  //period_ns
            period_ns = atoi(optarg);
            break;
        default :
            help_hint(argv[0]);
            c_flag = AK_FALSE;
            goto parse_option_end;
        }
    }
parse_option_end:
    return c_flag;
}



#ifdef AK_RTOS
int drv_pwm_sample(int argc, char **argv)
#else
int main (int argc, char **argv)
#endif
{
    int ret = 0;
    int count = 100;

    /*
    *sdk init first
    */
    sdk_run_config config = {0};
    config.mem_trace_flag = SDK_RUN_NORMAL;
    ak_sdk_init( &config );

    if (0 == parse_option(argc, argv)) 
    {
        return 0;
    }
    
    ak_print_normal(MODULE_ID_APP, "pwm test start.\n");

    //check params
    
    while(count-- > 0)
    {
        /*
        *pwm dev open
        */
        ret = ak_drv_pwm_open(dev_no);
        if(ret)
        {
            ak_print_error(MODULE_ID_APP, "open pwm device fail.\n");
            return -1;
        }
        ak_print_normal(MODULE_ID_APP, "pwm open success\n");

        /*
        *pwm set param
        */
        ret = ak_drv_pwm_set(dev_no, duty_ns, period_ns);
        if(ret)
        {
            ak_print_error(MODULE_ID_APP, "set pwm params fail.\n");
            return -1;
        }
       
        ak_print_normal(MODULE_ID_APP, "pwm set success\n");

        sleep(10);

        /*
        *pwm dev close
        */
        ak_drv_pwm_close(dev_no);
        ak_print_normal(MODULE_ID_APP, "pwm test ok.\n");
    }

    ak_sdk_exit();
    ak_print_normal(MODULE_ID_APP, "pwm test end.\n");
    return 0;
}

#ifdef AK_RTOS
MSH_CMD_EXPORT_ALIAS(drv_pwm_sample, ak_drv_pwm_sample, driver pwm sample)
#endif
