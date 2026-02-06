#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include "ak_common.h"
#include "ak_log.h"
#include "ak_drv_ts.h"

/* input protocol type */
enum input_protocol_type{
	PROTO_TYPE_A = 0x0,
	PROTO_TYPE_B,
};

struct slot_info{
	int slot_id;		/* slot id  */
	int ts_id;			/* touch point index */
	int x_pos;			/* x coordinate */
	int y_pos;			/* y coordinate */
	int	pressure;		/* pressure value */
	int	ps_st;		/* press status*/
	int status;			/* change status of slot*/
};

static int ts_handle = -1;
//static unsigned int ts_index = 0;
//static struct ak_ts_event ts_old = {0};
static struct slot_info ts_slot[TS_POINT_MAX_NUM];
//default slot id
static int def_slot = 1;
//current slot id
static int cur_slot = 0;
//default ts_id
static int def_ts_id = 1;
//current ts_id
static int cur_ts_id = 0;
static int cur_x_pos = -1;
static int cur_y_pos = -1;
static int cur_pressure = 0;
/* input type, 0:type A, 1:type B, defult should be type A, should not changed*/
int input_type = PROTO_TYPE_A;

static void set_bit(int *flag, unsigned char bit)
{
    *flag |= (1 << bit);
}


/**
 * osal_drv_ts_open: open touch screen driver
 * return: 0 - success; otherwise error code;
 */
int osal_drv_ts_open(void)
{		
	int fd = -1;
	int i = 0;
	int dev_id = -1;
	char path[128] = {0};
	char name[32] = {0};
	
	if (ts_handle < 0) 
	{
		/*
		*  由于触屏和按键都是input设备，根据加载顺序，
		*  先加载的是/dev/input/event0，后加载的是/dev/input/event1，
		*  这里通过检验name，自动识别哪个是触屏设备。
		*/
		for (i=0; i<2; i++)
		{
			sprintf(path, "/sys/class/input/event%d/device/name", i);
			fd = ak_open(path, O_RDONLY);

			if (fd < 0)
			{
				ak_print_error(MODULE_ID_DRV, "Open input%d name fail\n", i);
				return ERROR_TYPE_DEV_OPEN_FAILED;
			}

			read(fd, name, 32);

			/*
			* 开发驱动时要求触屏设备的名字要以ts_ 开头。
			*/
			if (0 == strncmp(name, "ts_", 3))
			{
				dev_id = i;
				close(fd);
				break;
			}
			close(fd);
			
		}

		if (-1 == dev_id)
		{
			ak_print_error(MODULE_ID_DRV, "find key dev fail\n");
			return ERROR_TYPE_DEV_OPEN_FAILED;
		}

		sprintf(path, "/dev/input/event%d", dev_id);
		ts_handle = ak_open(path, O_RDONLY);
		
		if (ts_handle < 0) 
		{
			ak_print_error_ex(MODULE_ID_DRV, "Open ts dev fail\n");
			return ERROR_TYPE_DEV_OPEN_FAILED;
		}
	}
	else
	{	
		/*
		* 设备打开一次即可使用，重复打开也返回成功，
		* 但是并不计数，关闭一次即关闭了设备。
		*/
		ak_print_notice_ex(MODULE_ID_DRV, "ts dev already opened!\n");
	}

	//ts_old.map = 0;
	
	for (int i=0; i<TS_POINT_MAX_NUM; i++)
	{
//		ts_old.info[i].x = -1;
//		ts_old.info[i].y = -1;
		/* init ts_slot info */
		memset(&ts_slot[i], 0, sizeof(struct slot_info));
	}

	return AK_SUCCESS;
}

/**
 * osal_drv_ts_get_event: get touch screen event .
 * @ts[OUT]:  ts event
 * @ms[IN]:  time out , <0  block; = 0  unblock; > 0 wait time.
 * return: 0 - success; otherwise error code;
 */
int osal_drv_ts_get_event(struct ak_ts_event *ts, long ms)
{
    int fd = -1;
    int ret = 0, rdbyte;
	fd_set readfds;
	struct input_event ts_event[64], *event;
	struct timeval timeout;
	int i = 0;

	/* arguments check */
	if (NULL == ts) 
	{
		ak_print_error_ex(MODULE_ID_DRV, "para null\n");
		return ERROR_TYPE_POINTER_NULL;
	}

	if (ts_handle < 0) 
	{
		ak_print_error_ex(MODULE_ID_DRV, "ts is not init\n");
		return ERROR_TYPE_NOT_INIT;
	}

	/*
	 * check wait time, deal with it differently.
	 * 0, no wait; > 0, wait
	 */
	fd = ts_handle;

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	ts->map = 0;
	
	for (i=0; i<TS_POINT_MAX_NUM; i++)
	{
		ts->info[i].x = -1;
		ts->info[i].y = -1;
	}

	/* block mode */
	if (ms < 0) 
	{
		ret = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);
		if (ret < 1) {
			ak_print_error_ex(MODULE_ID_DRV, "select error\n");
		}
	} 
	else 
	{ /* unblock mode */
        timeout.tv_sec = ms / 1000;
		timeout.tv_usec = (ms % 1000) * 1000;
        ret = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
	}

	/* To check whether the fd's status has changed */
	if (FD_ISSET(fd, &readfds)) 
	{
		/** read the event to the buf **/
		rdbyte = read(fd, ts_event, 64 * sizeof(struct input_event));
		/* parse the event */
		
		for (i=0; (i<rdbyte/sizeof(struct input_event)); i++) 
		{
			event = (struct input_event *)&ts_event[i];
			ak_print_info(MODULE_ID_DRV, "i:%d type = %d, code = %d, value = %d!\n",
					i, event->type, event->code, event->value);
			//ak_print_notice_ex(MODULE_ID_DRV, "i:%d type = %d, code = %d, value = %d!\n",
			//		i, event->type, event->code, event->value);
			/* filter others event */
			if (EV_ABS != event->type) 
				continue;

			/*init the cur_slot to defaut slot index 1 */
			if(0 == cur_slot)
				cur_slot = def_slot;

			/*init the ts index to defaut index index 1 */
			if(0 == cur_ts_id)
				cur_ts_id = def_ts_id;

			/* switch the event code to get the value */
			switch(event->code)
			{
				case ABS_MT_SLOT:
					cur_slot = event->value;
					/* slot event, set type B flag */
					input_type = PROTO_TYPE_B;
					break;
				case ABS_MT_TRACKING_ID:
					cur_ts_id = event->value;
					/* ID is -1 , type B release event */
					if(-1 == cur_ts_id )
					{
						input_type = PROTO_TYPE_B;	//set type B flag
					}

					if(input_type == PROTO_TYPE_B) /* TYPE B*/
					{
						/* -1 mean press is release in type B*/
						if(-1 == cur_ts_id)
						{
							ts_slot[cur_slot-1].ps_st = AK_FALSE;
						}
						/* press status */
						else
						{
							ts_slot[cur_slot-1].ts_id = cur_ts_id;
							ts_slot[cur_slot-1].ps_st = AK_TRUE;
						}
					}
					else /* TYPE A*/
					{
						/* in type A, use ts_id to indentify slot id */
						cur_slot = cur_ts_id;
						ts_slot[cur_slot-1].ts_id = cur_ts_id;
						ts_slot[cur_slot-1].ps_st = AK_TRUE;
						/* press status */
					}
					break;
				case ABS_MT_POSITION_X:
					cur_x_pos = event->value;
					ts_slot[cur_slot-1].x_pos = cur_x_pos;
					break;
				case ABS_MT_POSITION_Y:
					cur_y_pos = event->value;
					ts_slot[cur_slot-1].y_pos = cur_y_pos;
					break;
				case ABS_MT_TOUCH_MAJOR:
					cur_pressure = event->value;
					/* for type A , use pressure 0 for release */
					if(input_type == PROTO_TYPE_A && 0 == cur_pressure)
					{
						ts_slot[cur_slot-1].ps_st = AK_FALSE;
					}
						ts_slot[cur_slot-1].pressure = cur_pressure;
					break;

				default:
					break;
			}

			ts_slot[cur_slot-1].status = AK_TRUE;

#if 0
			/* get ts event */
			if ((ABS_MT_TRACKING_ID == event->code) || (ABS_MT_SLOT == event->code))
			{
				if ((event->value >= 1) && (event->value <= TS_POINT_MAX_NUM)) 
				{
					ts_index = event->value - 1;
				}
			}
			else if (ABS_MT_POSITION_X == event->code) 
			{
				ts->info[ts_index].x = event->value;
			}
			else if (ABS_MT_POSITION_Y == event->code) 
			{
				ts->info[ts_index].y = event->value;
			}
#endif
		}

#if 0
		/*
		* input架构会过滤同样的消息，导致上层收到的消息可能不全。
		* 平台处理方法是保存了上一次的坐标信息，
		* 当本次消息不全时把上次保留的值赋过来。
		*/
		for (i=0; i<TS_POINT_MAX_NUM; i++)
		{
			if ((ts->info[i].x >= 0) && (ts->info[i].y >= 0))
			{
				set_bit(&ts->map, i);
			}
			else if ((ts->info[i].x >= 0) && (ts->info[i].y < 0))
			{
				if (ts_old.info[i].y >= 0)
				{
					ts->info[i].y = ts_old.info[i].y;
					set_bit(&ts->map, i);
				}
			}
			else if ((ts->info[i].y >= 0) && (ts->info[i].x < 0))
			{
				if (ts_old.info[i].x >= 0)
				{
					ts->info[i].x = ts_old.info[i].x;
					set_bit(&ts->map, i);
				}
			}
		}
		/*save ts info to ts_old*/
		memcpy(&ts_old, ts, sizeof(struct ak_ts_event));
#endif
		for (i=0; i<TS_POINT_MAX_NUM; i++)
		{
			/* check whether ts status change*/
			if(ts_slot[i].status == AK_TRUE)
			{
				//ak_print_notice_ex(MODULE_ID_DRV,"slot[%d],id[%d],x[%d],y[%d],status[%s],[%s]\n",i, ts_slot[i].ts_id, ts_slot[i].x_pos, ts_slot[i].y_pos,(ts_slot[i].status == AK_TRUE?"Change!":"Not change!"), (ts_slot[i].ps_st == AK_TRUE?"Press":"Release"));

				set_bit(&ts->map, ts_slot[i].ts_id - 1);
				if(ts_slot[i].ps_st == AK_TRUE )
				{
					ts->info[ts_slot[i].ts_id - 1].x = ts_slot[i].x_pos;
					ts->info[ts_slot[i].ts_id - 1].y = ts_slot[i].y_pos;
				}
				else
				{
					/* set the x,y coordinate to -1 mean press release */
					ts->info[ts_slot[i].ts_id - 1].x = -1;
					ts->info[ts_slot[i].ts_id - 1].y = -1;
				}

				/*reset the ts status to  change */
				ts_slot[i].status = AK_FALSE;
			}
			else
			{
				//ak_print_normal_ex(MODULE_ID_DRV,"slot[%d],id[%d],x[%d],y[%d],status[%s],[%s]\n",i, ts_slot[i].ts_id, ts_slot[i].x_pos, ts_slot[i].y_pos,(ts_slot[i].status == AK_TRUE?"Change!":"Not change!"), (ts_slot[i].ps_st == AK_TRUE?"Press":"Release"));
			}
		}

		ret = AK_SUCCESS;
	} 
	else 
	{
		ak_print_info(MODULE_ID_DRV, "fd status no change.\n");
		ret = ERROR_TYPE_NO_DATA;
	}
	return ret;
}

/**
 * osal_drv_ts_close: close touch screen .
 * return: 0 - success; otherwise error code;
 */
int osal_drv_ts_close(void)
{
	/* argument check */
	if (ts_handle < 0) 
	{
		ak_print_error_ex(MODULE_ID_DRV, "ts is not init\n");
		return ERROR_TYPE_NOT_INIT;
	}
	
	/* close device */
	close(ts_handle);
	ts_handle = -1;

	return AK_SUCCESS;
}

/**
 * ak_drv_ts_open: open touch screen driver
 * return: 0 - success; otherwise error code;
 */
int ak_drv_ts_open(void)
{
	return osal_drv_ts_open();
}

/**
 * ak_drv_ts_get_event: get touch screen event .
 * @ts[OUT]:  ts event
 * @ms[IN]:  time out , <0  block; = 0  unblock; > 0 wait time.
 * return: 0 - success; otherwise error code;
 */
int ak_drv_ts_get_event(struct ak_ts_event *ts, long ms)
{
	/* arguments check */
	if (NULL == ts) 
	{
		ak_print_error_ex(MODULE_ID_DRV, "para null\n");
		return ERROR_TYPE_POINTER_NULL;
	}

	return osal_drv_ts_get_event(ts, ms);
}

/**
 * ak_drv_ts_close: close touch screen .
 * return: 0 - success; otherwise error code;
 */
int ak_drv_ts_close(void)
{
	return osal_drv_ts_close();
}

