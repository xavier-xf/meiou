#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <linux/input.h>

#include "ak_common.h"
#include "ak_log.h"
#include "ak_drv_key.h"

#define KEY_MORE_CNT_MAX    2

static int key_handle = -1;
static int key_more_cnt = 0;
static struct key_event key_more[KEY_MORE_CNT_MAX];

/**
 * osal_drv_key_open: open key driver
 * return: 0 - success; otherwise error code;
 */
int osal_drv_key_open(void)
{	
	int fd = -1;
	int i = 0;
	int dev_id = -1;
	char path[128] = {0};
	char name[32] = {0};
	
	if (key_handle < 0) 
	{
		/*
		*  由于触屏和按键都是input设备，根据加载顺序，
		*  先加载的是/dev/input/event0，后加载的是/dev/input/event1，
		*  这里通过检验name，自动识别哪个是按键设备。
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

			if ((0 == strncmp(name, "gpiokeys", 8))
                || (0 == strncmp(name, "adkeys", 6)))
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
		key_handle = ak_open(path, O_RDONLY);
		
		if (key_handle < 0) 
		{
			ak_print_error(MODULE_ID_DRV, "Open key dev fail\n");
			return ERROR_TYPE_DEV_OPEN_FAILED;
		}
	}
	else
		ak_print_notice_ex(MODULE_ID_DRV, "key dev already opened!\n");

	return AK_SUCCESS;
}

/**
 * osal_drv_key_get_event: get key event .
 * @key[OUT]:  key event
 * @ms[IN]:  time out , <0  block; = 0  unblock; > 0 wait time.
 * return: 0 - success; otherwise error code;
 */
int osal_drv_key_get_event(struct key_event *key, long ms)
{
    int fd = -1;
    int ret = 0, rdbyte;
	fd_set readfds;
	struct input_event key_event[64], *event;
	struct timeval timeout;

	/* arguments check */
	if (NULL == key) {
		ak_print_error_ex(MODULE_ID_DRV, "para err\n");
		return ERROR_TYPE_POINTER_NULL;
	}

	if (key_handle < 0) {
		ak_print_error_ex(MODULE_ID_DRV, "not init\n");
		return ERROR_TYPE_NOT_INIT;
	}

	/*
	 * check wait time, deal with it differently.
	 * 0, no wait; > 0, wait
	 */
	fd = key_handle;
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	/* block mode */
	if (ms < 0) {
		ret = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);
		if (ret < 1) {
			ak_print_error(MODULE_ID_DRV, "select error\n");
		}
	} else { /* unblock mode */
        timeout.tv_sec = ms / 1000;
		timeout.tv_usec = (ms % 1000) * 1000;
        ret = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
	}

	/* To check whether the fd's status has changed */
	if (FD_ISSET(fd, &readfds)) {
		/** read the event to the buf **/
		rdbyte = read(fd, key_event, 64 * sizeof(struct input_event));
		/* parse the event */
		int i,j;
		j = 0;
		key_more_cnt = 0;
		memset(&key_more, 0, sizeof(key_more));
		
		for (i = 0; (i < rdbyte / sizeof(struct input_event)); i++) {
			event = (struct input_event *)&key_event[i];
			ak_print_info(MODULE_ID_DRV, "i:%d type = %d, code = %d, value = %d!\n",
					i, event->type, event->code, event->value);
			/* filter others event */
			if (EV_KEY != event->type) {
				continue;
			}
			/* get key event */
	        if(j>0)
			{
				  if(key_more_cnt<KEY_MORE_CNT_MAX)
				  {	
	 					key_more[key_more_cnt].code = event->code;
	 					key_more[key_more_cnt].stat = (0 == event->value) ? PRESS : RELEASE;
	 					key_more_cnt++;	
 					}
					ak_print_notice_ex(MODULE_ID_DRV, "key_more[%d,i=%d],[code=%d,stat=%d]\n",key_more_cnt, i, event->code, (0 == event->value) ? PRESS : RELEASE);
					j++;		
					continue;		
			}	
			key->code = event->code;
			key->stat = (0 == event->value) ? PRESS : RELEASE;	
			j++;	
		}
		ret = AK_SUCCESS;
	} else
		ret = ERROR_TYPE_NO_DATA;

	return ret;
}

int osal_drv_key_get_more(struct key_event *key)
{
    int ret = 0;
	  
    if (key_more_cnt > 0)
    {
		key->code = key_more[0].code;
		key->stat = key_more[0].stat;	

        for (int i=0; i<KEY_MORE_CNT_MAX-1; i++)
        {
            key_more[i].code = key_more[i+1].code;
            key_more[i].stat = key_more[i+1].stat;	
        }
        
		ret = 1;
		key_more_cnt--;
	}
			
	return ret;
}

/**
 * osal_drv_key_close: close key .
 * @handle[IN]:  key handle
 * return: 0 - success; otherwise error;
 */
int osal_drv_key_close(void)
{
	/* argument check */
	if (key_handle < 0) {
		ak_print_error_ex(MODULE_ID_DRV, "not init\n");
		return ERROR_TYPE_NOT_INIT;
	}
	/* close device */
	close(key_handle);
	key_handle = -1;

	return AK_SUCCESS;
}


/**
 * ak_drv_key_open: open key driver
 * return: 0 - success; otherwise error code;
 */
int ak_drv_key_open(void)
{	
	return osal_drv_key_open();
}

/**
 * ak_drv_key_get_event: get key event .
 * @key[OUT]:  key event
 * @ms[IN]:  time out , <0  block; = 0  unblock; > 0 wait time.
 * return: 0 - success; otherwise error code;
 */
int ak_drv_key_get_event(struct key_event *key, long ms)
{
	/* arguments check */
	if (NULL == key) {
		ak_print_error_ex(MODULE_ID_DRV, "para err\n");
		return ERROR_TYPE_POINTER_NULL;
	}

	return osal_drv_key_get_event(key, ms);
}

int ak_drv_key_get_more(struct key_event *key)
{
	return osal_drv_key_get_more(key);
}

/**
 * ak_drv_key_close: close key .
 * @handle[IN]:  key handle
 * return: 0 - success; otherwise error;
 */
int ak_drv_key_close(void)
{
	return osal_drv_key_close();
}
