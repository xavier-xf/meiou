#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "ak_common.h"
#include "ak_drv_key.h"
#include "ak_log.h"

#ifdef AK_RTOS
#include <rtthread.h>
#endif

#define KEY_INPUT_DEV_PATH "/dev/input/event0"

#define LEN_HINT         512
#define LEN_OPTION_SHORT 512

static char *pc_prog_name = NULL;  
static int wait_time = 20;//second

static char ac_option_hint[  ][ LEN_HINT ] = {                                         //操作提示数组
    "help info" ,
    "[NUM] run time in seconds, default 20s" ,
    "" ,
};

static struct option option_long[ ] = {
    { "help"      , no_argument       , NULL , 'h' } ,    //"help info"
    { "time"      , required_argument , NULL , 'a' } ,    //"[NUM] run time in seconds, default 20s"
    {0, 0, 0, 0}
 };

static void usage(const char * name)
{
    ak_print_normal(MODULE_ID_APP," %s -a [time]\n", name);
    ak_print_normal(MODULE_ID_APP,"eg: %s -a 10\n", name);
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
        case 'a' :  //time
            wait_time = atoi(optarg);
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
static int get_code_index(int key_code)
{
   int ret = 0;
   switch(key_code) {
      case 111:
	  ret = 0;
	  break;
      case 113:
	  ret = 1;
	  break;
      case 114:
	  ret = 2;
	  break;
      case 116:
	  ret = 3;
	  break;
      case 117:
	  ret = 4;
	  break;
      case 118:
	  ret = 5;
	  break;
      case 119:
	  ret = 6;
	  break;
      case 120:
	  ret = 7;
	  break;
      case 121:
	  ret = 8;
	  break;
      case 122:
	  ret = 9;
	  break;
   }
    return ret;
}
#ifdef AK_RTOS
int drv_key_sample(int argc, char **argv)
#else
int main (int argc, char **argv)
#endif
{
    int ret = -1;
    struct key_event key;
    struct ak_timeval start_tv;
    struct ak_timeval end_tv;
	  int key_state = 0;
		
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

    if (wait_time <= 0) 
    {
        ak_print_error(MODULE_ID_APP, "wait time should be over 0 second.\n");
        return -1;
    }
            
    ak_print_normal(MODULE_ID_APP, "key test start.\n");
    
    /*
    *open key driver 
    */    
    ret = ak_drv_key_open();
    if(ret){
        ak_print_error_ex(MODULE_ID_APP, "ak_drv_key_open fail.\n");
        return -1;
    }
        
    memset(&key,0,sizeof(key));

    ak_get_ostime(&start_tv);

    /*
    *demo will run wait_time
    */
    do {
       ak_print_normal(MODULE_ID_APP, "ak_drv_key_get_event timeout 1000 ms.\n"); 
       memset(&key,0,sizeof(key));
       ret = ak_drv_key_get_event(&key,1000);
        /* 
        *get key, output 
        */
       if(ret == 0)
       {
			ret = get_code_index(key.code);
			if( key.stat == 1) 
			    key_state |= 1<<ret;
			else if( key.stat == 2) 
			    key_state &= ~(1<<ret);
			     	     	       	
     	    ak_print_normal(MODULE_ID_APP, "get key:%d stat:%d(1==pressed,2==release), key_state = 0x%x\n",key.code,key.stat, key_state); 
     	    memset(&key,0,sizeof(key));  
     	    while( ak_drv_key_get_more(&key) > 0 )
     	    {
     	       	ret = get_code_index(key.code);
     	       	if( key.stat == 1) 
			     	key_state |= 1<<ret;
				else if( key.stat == 2) 
			     	key_state &= ~(1<<ret);
     	       	ak_print_normal(MODULE_ID_APP, "get key more:%d stat:%d, key_state = 0x%x\n",key.code,key.stat, key_state); 
     	    } 
		}
        ak_get_ostime(&end_tv);

        ak_print_normal(MODULE_ID_APP, "\n"); 
        /* 
        *run time is over and exit 
        */
        if (ak_diff_ms_time(&end_tv, &start_tv) > (wait_time * 1000))
            break;
    } while (1);
    /* 
    *close key handle  
    */    
    ak_drv_key_close();
    
    ak_sdk_exit();
    ak_print_normal(MODULE_ID_APP, "key test end.\n");    
    
    return 0;
}

#ifdef AK_RTOS
MSH_CMD_EXPORT_ALIAS(drv_key_sample, ak_drv_key_sample, driver key sample)
#endif
