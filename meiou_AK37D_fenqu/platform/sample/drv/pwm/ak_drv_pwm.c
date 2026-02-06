#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "ak_common.h"
#include "ak_log.h"
#include "ak_drv_pwm.h"


#define PWM_DEV_MAX_NUM		5
#define PERIOD_S_MIN		167			// 1s /6Mhz
#define PERIOD_S_MAX		10869565	// 1s / 92hz



/* 
 * ak_drv_pwm_open - open pwm device
 * device_no[IN]: pwm device minor-number,[0-4]
 * return: 0 - success; otherwise error code;
 */
int ak_drv_pwm_open(int device_no)
{
	int fd = -1;
	int ret = 0;
	char dev_name[64] = {0};
	char path[64] = {0};

	if (device_no < 0 || device_no >= PWM_DEV_MAX_NUM){
		ak_print_error(MODULE_ID_DRV, "device_no error, %d, must in [0-%d]\n", device_no, PWM_DEV_MAX_NUM - 1);
		return ERROR_TYPE_INVALID_ARG;
	}

	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0", device_no);

	if (!access(path, F_OK))
	{
		ak_print_notice(MODULE_ID_DRV, "pwm dev %d already opened.\n", device_no);
		return AK_SUCCESS;
	}

	/*
	*create pwm dev use export; 
	*close pwm dev use unexport.
	*/
	sprintf(dev_name, "/sys/class/pwm/pwmchip%d/export", device_no);

	fd = ak_open(dev_name, O_WRONLY);

	if (fd < 0)
	{
		ak_print_error(MODULE_ID_DRV, "Open pwm dev %d fail\n", device_no);
		return ERROR_TYPE_DEV_OPEN_FAILED;
	}
	
	/*
	*write 0 to export, it will create pwm0 folder. 
	*/
	ret = write(fd, "0", strlen("0"));
	if(ret < 0)
	{	
		close(fd);
		ak_print_error(MODULE_ID_DRV, "create pwm dev %d fail\n", device_no);
		return ERROR_TYPE_DEV_OPEN_FAILED;
	}
	close(fd);

	return AK_SUCCESS;
}

/* 
 * ak_drv_pwm_set - set pwd working param
 * device_no[IN]: pwm device minor-number,[0-4]
 * duty_ns[IN]: pwm duty time in ns.
 * period_ns[IN]: pwm period time in ns.
 * return: 0 on success, otherwise error code.
 */
int ak_drv_pwm_set(int device_no, int duty_ns, int period_ns)
{
	int fd = -1;
	int ret;
	char path[64] = {0};
	char str[32] = {0};

	/* arguments check */
	if (device_no < 0 || device_no >= PWM_DEV_MAX_NUM){
		ak_print_error(MODULE_ID_DRV, "device_no error, %d, must in [0-%d]\n", device_no, PWM_DEV_MAX_NUM - 1);
		return ERROR_TYPE_INVALID_ARG;
	}

	if (period_ns < PERIOD_S_MIN || period_ns > PERIOD_S_MAX)
	{
		ak_print_error(MODULE_ID_DRV, "period_ns error, must in [%d-%d]\n", PERIOD_S_MIN, PERIOD_S_MAX);
		return ERROR_TYPE_INVALID_ARG;
	}

	if (duty_ns <= 0 || duty_ns >= period_ns)
	{
		ak_print_error(MODULE_ID_DRV, "duty_ns error, must less than period_ns\n");
		return ERROR_TYPE_INVALID_ARG;
	}

	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0", device_no);
	
	if (access(path, F_OK))
	{
		ak_print_error(MODULE_ID_DRV, "pwm dev %d not open.\n", device_no);
		return ERROR_TYPE_NOT_INIT;
	}

	/*
	*disable pwm before set pwm period and duty_cycle
	*/
	memset(path, 0, 64);
	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0/enable", device_no);
  	fd = ak_open(path, O_RDWR);
   
  	if(fd < 0)
  	{
  		ak_print_error(MODULE_ID_DRV, "pwm dev %d enable file open failed.\n", device_no);
      	return ERROR_TYPE_DEV_CTRL_FAILED;
  	}

  	ret = write(fd, "0", strlen("0"));
  	if(ret < 0)
  	{
      	ak_print_error(MODULE_ID_DRV, "pwm dev %d set disable failed.\n", device_no);
		close(fd);
      	return ERROR_TYPE_DEV_CTRL_FAILED;
  	}
	close(fd);

    /*
	*clean pwn duty_cycle
	*/
	memset(path, 0, 64);
	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0/duty_cycle", device_no);
  	fd = ak_open(path, O_RDWR);

	if(fd < 0)
  	{
  		ak_print_error(MODULE_ID_DRV, "pwm dev %d duty file open failed.\n", device_no);
      	return ERROR_TYPE_FILE_OPEN_FAILED;
  	}

    char tmp[16] = {0};
    ret = read(fd, tmp, sizeof(tmp));
    if (0 != atoi(tmp))
    {
    	ret = write(fd, "0", strlen("0"));
      	if(ret < 0)
      	{
          	ak_print_error(MODULE_ID_DRV, "pwm dev %d clean duty_ns failed.\n", device_no);
    		close(fd);
          	return ERROR_TYPE_FILE_WRITE_FAILED;
      	}
    }

	close(fd);

	/*
	*set pwn period
	*/
	memset(path, 0, 64);
	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0/period", device_no);
	fd = ak_open(path, O_RDWR);

	if(fd < 0)
  	{
  		ak_print_error(MODULE_ID_DRV, "pwm dev %d period file open failed.\n", device_no);
      	return ERROR_TYPE_FILE_OPEN_FAILED;
  	}

	sprintf(str, "%d", period_ns);
  	ret = write(fd, str, strlen(str));
  	if(ret < 0)
  	{
      	ak_print_error(MODULE_ID_DRV, "pwm dev %d set period_ns failed.\n", device_no);
		close(fd);
      	return ERROR_TYPE_FILE_WRITE_FAILED;
  	}
	close(fd);

	/*
	*set pwn duty_cycle
	*/
	memset(path, 0, 64);
	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0/duty_cycle", device_no);
  	fd = ak_open(path, O_RDWR);

	if(fd < 0)
  	{
  		ak_print_error(MODULE_ID_DRV, "pwm dev %d duty file open failed.\n", device_no);
      	return ERROR_TYPE_FILE_OPEN_FAILED;
  	}

	memset(str, 0, 32);
	sprintf(str, "%d", duty_ns);
  	ret = write(fd, str, strlen(str));
  	if(ret < 0)
  	{
      	ak_print_error(MODULE_ID_DRV, "pwm dev %d set duty_ns failed.\n", device_no);
		close(fd);
      	return ERROR_TYPE_FILE_WRITE_FAILED;
  	}

	close(fd);

	/*
	*enable pwm after set pwm period and duty_cycle
	*/
	memset(path, 0, 64);
	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0/enable", device_no);
  	fd = ak_open(path, O_RDWR);
   
  	if(fd < 0)
  	{
  		ak_print_error(MODULE_ID_DRV, "pwm dev %d enable file open failed.\n", device_no);
      	return ERROR_TYPE_DEV_CTRL_FAILED;
  	}

  	ret = write(fd, "1", strlen("1"));
  	if(ret < 0)
  	{
      	ak_print_error(MODULE_ID_DRV, "pwm dev %d set enable failed.\n", device_no);
		close(fd);
      	return ERROR_TYPE_DEV_CTRL_FAILED;
  	}
	close(fd);
	
	return AK_SUCCESS;
}

/* 
 * ak_drv_pwm_close - close pwm
 * device_no[IN]: pwm device minor-number,[0-4]
 * return: 0 on success, otherwise error code.
 */
int ak_drv_pwm_close(int device_no)
{
	int fd = -1;
	int ret = 0;
	char path[64] = {0};
	char dev_name[64] = {0};
	
	if (device_no < 0 || device_no >= PWM_DEV_MAX_NUM){
		ak_print_error(MODULE_ID_DRV, "device_no error, %d, must in [0-%d]\n", device_no, PWM_DEV_MAX_NUM - 1);
		return ERROR_TYPE_INVALID_ARG;
	}
	
	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0", device_no);
	
	if (access(path, F_OK))
	{
		ak_print_error(MODULE_ID_DRV, "pwm dev %d not open.\n", device_no);
		return ERROR_TYPE_NOT_INIT;
	}

	/*
	*disable pwm before close
	*/
	memset(path, 0, 64);
	sprintf(path, "/sys/class/pwm/pwmchip%d/pwm0/enable", device_no);
	fd = ak_open(path, O_RDWR);
   
	if(fd < 0)
	{
		ak_print_error(MODULE_ID_DRV, "pwm dev %d enable file open failed.\n", device_no);
		return ERROR_TYPE_DEV_CTRL_FAILED;
	}

	ret = write(fd, "0", strlen("0"));
	if(ret < 0)
	{
		ak_print_error(MODULE_ID_DRV, "pwm dev %d set disable failed.\n", device_no);
		close(fd);
		return ERROR_TYPE_DEV_CTRL_FAILED;
	}
	close(fd);

	/* 
	*close pwm dev 
	*/
	sprintf(dev_name, "/sys/class/pwm/pwmchip%d/unexport", device_no);

	fd = ak_open(dev_name, O_WRONLY);

	if (fd < 0)
	{
		ak_print_error(MODULE_ID_DRV, "Open pwm dev unexport %d fail\n", device_no);
		return ERROR_TYPE_DEV_CLOSE_FAILED;
	}

	/*
	*write 0 to unexport, it will close pwm dev.
	*/
	ret = write(fd, "0", strlen("0"));
	if(ret < 0)
	{
		close(fd);
		ak_print_error(MODULE_ID_DRV, "close pwm dev %d fail\n", device_no);
		return ERROR_TYPE_DEV_CLOSE_FAILED;
	}
	close(fd);

	return AK_SUCCESS;
}
