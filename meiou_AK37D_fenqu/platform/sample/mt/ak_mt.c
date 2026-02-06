#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include "ak_common.h"

#include "ak_log.h"
#include "ak_mem.h"

#include "ak_mt_inner.h"

/*********************************************/
/*********************************************/
/*********************************************/
// external interface function

/**
 * ak_mt_init - init motion tracking parameters
 * return: motion tracking parameter handle
 * notes:
 */
void* ak_mt_init(void)
{	
	TRACKING_PARAM *param = (TRACKING_PARAM *)ak_mem_alloc(MODULE_ID_APP, sizeof(TRACKING_PARAM));

	if (NULL == param)
	{
		ak_print_error(MODULE_ID_APP, "memory allocation failed!\n");
		return NULL;
	}
	
	ak_print_normal(MODULE_ID_APP, "########%s########\n", MT_LIB_VERSION);
	
	memset(param, 0, sizeof(TRACKING_PARAM));
	
	param->min_agl_h = MIN_ANGLE_H;  
	param->max_agl_h = MAX_ANGLE_H; 
	param->min_agl_v = MIN_ANGLE_V;
	param->max_agl_v = MAX_ANGLE_V;
	
	param->mt_enable = AK_TRUE;
	mt_reset_tracking_param(param);
	
	param->init_agl_h = 180;
	param->init_agl_v = 65;
	param->current_angle_h = 180;    
	param->current_angle_v = 65;

	param->rotate_h_enable = AK_TRUE;
	param->rotate_v_enable = AK_FALSE;
	param->size_min = 1; 
	param->size_max = 768; 
	param->boundary_size_h = 9;
	param->boundary_size_v = 8;

	param->fps = 25;
	param->max_invalid_fm = MAX_INVALID_FRAMES;    
	param->max_decision_fm = MAX_DECISION_FRAMES;
	param->max_decision_fm_backup = param->max_decision_fm;
	param->wait_fm = WAIT_FRAMES;
	param->wait_time = WAIT_TIME;

	param->agl_h = 0;
	param->agl_v = 0;

	param->rotation_dir_h = 1;
	param->rotation_dir_v = 1;
	param->main_decision_dir_v = 1;

	param->mt_factor = 256;

	param->invalid_dis = INVALID_DISTANCE;
	param->undetected_num = 0;

	param->mt_handle = param;

	return param;
}

/**
 * ak_mt_destroy - release motion tracking parameter handle
 * @mt_handle[IN]: motion tracking parameter handle
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_destroy(void *mt_handle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_destroy: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_destroy: input mt_handle is error\n");
		return -1;
	}
    
	ak_mem_free(param);
	
	return 0;
}

/**
 * ak_mt_set_fps - set encode frames per second
 * @mt_handle[IN]: motion tracking parameter handle
 * @fps[IN]: encode frames per second
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_fps(void *mt_handle, short fps)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_fps: mt_handle is NULL\n");
		return -1;
	}

	if (fps <= 0)
	{
		ak_print_normal(MODULE_ID_APP, "ak_mt_set_fps: The value of fps %d is negative, use 25 instead\n", fps);
		fps = 25;
	}

	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;
	
	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_fps: input mt_handle is error\n");
		return -1;
	}
	
	param->fps = fps;

	return 0;
}

/**
 * ak_mt_set_max_invalid_frames - set max frames to determine invalid motion
 * @mt_handle[IN]: motion tracking parameter handle
 * @nFrames[IN]: max frames to determine invalid motion
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_max_invalid_frames(void *mt_handle, unsigned short nFrames)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_max_invalid_frames: mt_handle is NULL\n");
		return -1;
	}

	if (nFrames > INVALID_FRAMES_CAP)
	{
		ak_print_normal(MODULE_ID_APP, "ak_mt_set_max_invalid_frames: input %d is out of capacity, now use the max accessible value %d instead\n", 
			nFrames, INVALID_FRAMES_CAP);
		nFrames = INVALID_FRAMES_CAP;
	}

	if (nFrames < DECISION_FRAMES_CAP)
	{
		ak_print_normal(MODULE_ID_APP, "ak_mt_set_max_invalid_frames: input %d is too small, now use the min accessible value %d instead\n",
			nFrames, DECISION_FRAMES_CAP);
		nFrames = DECISION_FRAMES_CAP;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_max_invalid_frames: input mt_handle is error\n");
		return -1;
	}
	
	param->max_invalid_fm = nFrames;
	
	return 0;
}

/**
 * ak_mt_set_max_decision_frames - set max frames to determine camera rotation
 * @mt_handle[IN]: motion tracking parameter handle
 * @nFrames[IN]: max frames to determine camera rotation
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_max_decision_frames(void *mt_handle, unsigned short nFrames)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_max_decision_frames: mt_handle is NULL\n");
		return -1;
	}
	
	if (nFrames > DECISION_FRAMES_CAP)
	{
		ak_print_normal(MODULE_ID_APP, "ak_mt_set_max_decision_frames: input %d is out of capacity, now use the max accessible value %d instead\n",
			nFrames, DECISION_FRAMES_CAP);
		nFrames = DECISION_FRAMES_CAP;
	}

	if (nFrames < 3)
	{
		ak_print_normal(MODULE_ID_APP, "ak_mt_set_max_decision_frames: input %d is too small, now use the min accessible value 3 instead\n",
			nFrames);
		nFrames = 3;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_max_decision_frames: input mt_handle is error\n");
		return -1;
	}
	
	param->max_decision_fm = nFrames;
	param->max_decision_fm_backup = param->max_decision_fm;
	
	return 0;
}

/**
 * ak_mt_set_wait_time - set waiting time for camera rotation 
 * @mt_handle[IN]: motion tracking parameter handle
 * @msec[IN]: milliseconds to wait camera rotation 
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_wait_time(void *mt_handle, unsigned short msec)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_wait_time: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_wait_time: input mt_handle is error\n");
		return -1;
	}
	
	param->wait_time = msec;
	
	return 0;
}

/**
 * ak_mt_set_invalid_distance - set distance to seperate invalid and valid motion
 * @mt_handle[IN]: motion tracking parameter handle
 * @dis[IN]: distance to seperate invalid and valid motion 
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_invalid_distance(void *mt_handle, unsigned char dis)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_invalid_distance: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_invalid_distance: input mt_handle is error\n");
		return -1;
	}
	
	param->invalid_dis = dis;
	
	return 0;
}

/**
 * ak_mt_set_camera_rotation_range - set camera accessible rotation angle
 * @mt_handle[IN]: motion tracking parameter handle
 * @min_agl_h[IN]: min accessible angle of camera in horizontal direction
 * @max_agl_h[IN]: max accessible angle of camera in horizontal direction
 * @min_agl_v[IN]: min accessible angle of camera in vertical direction
 * @max_agl_v[IN]: max accessible angle of camera in vertical direction
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_camera_rotation_range(void *mt_handle, short min_agl_h, short max_agl_h, 
	short min_agl_v, short max_agl_v)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_camera_rotation_range: mt_handle is NULL\n");
		return -1;
	}

	if (min_agl_h > max_agl_h)
	{
		min_agl_h = min_agl_h + max_agl_h;
		max_agl_h = min_agl_h - max_agl_h;
		min_agl_h = min_agl_h - max_agl_h;
	}

	if (min_agl_v > max_agl_v)
	{
		min_agl_v = min_agl_v + max_agl_v;
		max_agl_v = min_agl_v - max_agl_v;
		min_agl_v = min_agl_v - max_agl_v;
	}
	
	if (min_agl_h < 0)
	{
		min_agl_h = 0;
	}

	if (max_agl_h > 360)
	{
		max_agl_h = 360;
	}

	if (min_agl_v < 0)
	{
		min_agl_v = 0;
	}

	if (max_agl_v > 360)
	{
		max_agl_v = 360;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_camera_rotation_range: input mt_handle is error\n");
		return -1;
	}
	
	param->min_agl_h = min_agl_h;
	param->max_agl_h = max_agl_h;
	param->min_agl_v = min_agl_v;
	param->max_agl_v = max_agl_v;
	
	return 0;
}

/**
 * ak_mt_set_camera_init_angle - set camera init angle
 * @mt_handle[IN]: motion tracking parameter handle
 * @init_agl_h[IN]: init camera angle in horizontal direction
 * @init_agl_v[IN]: init camera angle in vertical direction
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_camera_init_angle(void * mt_handle, short init_agl_h, short init_agl_v)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_camera_init_angle: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_camera_init_angle: input mt_handle is error\n");
		return -1;
	}

	if (init_agl_h < param->min_agl_h)
	{
		init_agl_h = param->min_agl_h;
	}

	if (init_agl_h > param->max_agl_h)
	{
		init_agl_h = param->max_agl_h;
	}

	if (init_agl_v < param->min_agl_v)
	{
		init_agl_v = param->max_agl_v;
	}

	if (init_agl_v > param->max_agl_v)
	{
		init_agl_v = param->max_agl_v;
	}
	
	param->init_agl_h = init_agl_h;
	param->init_agl_v = init_agl_v;

	return 0;
}

/**
 * ak_mt_set_camera_rotation_factor - set camera rotation factor
 * @mt_handle[IN]: motion tracking parameter handle
 * @factor[IN]: camera rotation factor
 * return: 0 on success; -1 on failed
 * notes: if offset caused by motion speed isn't essential, set the input parameter factor = x(x is a negative number, example -1) 
 */
int ak_mt_set_camera_rotation_factor(void *mt_handle, int factor)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_camera_rotation_factor: mt_handle is NULL\n");
		return -1;
	}

	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_camera_rotation_factor: input mt_handle is error\n");
		return -1;
	}

	if (factor == 0)
	{
		factor = 256;
	}
	
	param->mt_factor = factor;

	return 0;
}

/**
 * ak_mt_update_camera_angle - update current camera direction angle
 * @mt_handle[IN]: motion tracking parameter handle
 * @agl_h[IN]: current camera angle in horizontal direction
 * @agl_v[IN]: current camera angle in vertical direction
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_update_camera_angle(void *mt_handle, short agl_h, short agl_v)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_update_camera_angle: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_update_camera_angle: input mt_handle is error\n");
		return -1;
	}

	if (agl_h < param->min_agl_h)
	{
		agl_h = param->min_agl_h;
	}

	if (agl_h > param->max_agl_h)
	{
		agl_h = param->max_agl_h;
	}

	if (agl_v < param->min_agl_v)
	{
		agl_v = param->max_agl_v;
	}

	if (agl_v > param->max_agl_v)
	{
		agl_v = param->max_agl_v;
	}
	
	param->current_angle_h = agl_h;
	param->current_angle_v = agl_v;

	return 0;
}

/**
 * ak_mt_open_camera_rotation_switch_v - open camera rotation switch in vertical direction
 * @mt_handle[IN]: motion tracking parameter handle
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_open_camera_rotation_switch_v(void *mt_handle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_open_camera_rotation_switch_v: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_open_camera_rotation_switch_v: input mt_handle is error\n");
		return -1;
	}
	
	param->rotate_v_enable = AK_TRUE;

	return 0;
}

/**
 * ak_mt_close_camera_rotation_switch_v - close camera rotation switch in vertical direction
 * @mt_handle[IN]: motion tracking parameter handle
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_close_camera_rotation_switch_v(void *mt_handle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_close_camera_rotation_switch_v: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_close_camera_rotation_switch_v: input mt_handle is error\n");
		return -1;
	}
	
	param->rotate_v_enable = AK_FALSE;

	return 0;
}

/**
 * ak_mt_reverse_rotation_direction_h - reverse camera rotation direction in horizontal direction
 * @mt_handle[IN]: motion tracking parameter handle
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_reverse_rotation_direction_h(void *mt_handle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_reverse_rotation_direction_h: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_reverse_rotation_direction_h: input mt_handle is error\n");
		return -1;
	}
	
	param->rotation_dir_h *= -1;

	return 0;
}

/**
 * ak_mt_reverse_rotation_direction_v - reverse camera rotation direction in vertical direction
 * @mt_handle[IN]: motion tracking parameter handle
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_reverse_rotation_direction_v(void *mt_handle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_reverse_rotation_direction_v: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_reverse_rotation_direction_v: input mt_handle is error\n");
		return -1;
	}
	
	param->rotation_dir_v *= -1;

	return 0;
}

/**
 * ak_mt_reverse_main_decision_direction_v - reverse main decision direction in vertical direction
 * @mt_handle[IN]: motion tracking parameter handle
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_reverse_main_decision_direction_v(void *mt_handle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_reverse_main_decision_direction_v: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_reverse_main_decision_direction_v: input mt_handle is error\n");
		return -1;
	}
	
	param->main_decision_dir_v *= -1;

	return 0;
}

/**
 * ak_mt_set_params - set motion tracking parameters from outer input
 * @mt_handle[IN]: motion tracking parameter handle
 * @len[IN]: VPSS_MD_DIMENSION in vertical direction
 * @valid_size_min[IN]: min size of valid motion region
 * @valid_size_max[IN]: max size of valid motion region
 * @center_size_h[IN]: horizontal size of video frame center
 * @center_size_v[IN]: vertical size of video frame center
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_set_motion_region_params(void *mt_handle, short len, short valid_size_min,  short valid_size_max,
	short boundary_size_h, short boundary_size_v)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_motion_region_params: mt_handle is NULL\n");
		return -1;
	}
	
	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_motion_region_params: input mt_handle is error\n");
		return -1;
	}

	if (len < 1 || len > MD_SIZE)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_set_motion_region_params: input %d is out of range ,the avaliable value of parameter len is 1 to %d\n", len , MD_SIZE);
		return -1;
	}
	
	if (valid_size_min > valid_size_max)
	{
		valid_size_min = valid_size_min + valid_size_max;
		valid_size_max = valid_size_min - valid_size_max;
		valid_size_min = valid_size_min - valid_size_max;
	}

	if (valid_size_max < 1)
	{
		valid_size_max = 1;
	}

	if (boundary_size_h < 0)
	{
		boundary_size_h *= -1;
	}

	if (boundary_size_h*2 > MD_SIZE)
	{
		boundary_size_h = MD_SIZE/2;
	}

	if (boundary_size_v < 0)
	{
		boundary_size_v *= -1;
	}

	if (boundary_size_v*2 > len)
	{
		boundary_size_v = len/2;
	}
	
	param->size_min = valid_size_min;
	param->size_max = valid_size_max;
	param->boundary_size_h = boundary_size_h;
	param->boundary_size_v = boundary_size_v;

	return 0;
}

/**
 * ak_mt_wait_camera_rotation - wait camera rotation
 * @mt_handle[IN]: motion tracking parameter handle
 * return: 0 on success; -1 on failed
 * notes:
 */
int ak_mt_wait_camera_rotation(void *mt_handle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_wait_camera_rotation: mt_handle is NULL\n");
		return -1;
	}

	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_wait_camera_rotation: input mt_handle is error\n");
		return -1;
	}
	
	mt_reset_tracking_param(param);
	param->mt_enable = AK_FALSE;

	return 0;
}

/**
 * ak_mt_tracking - track the desired motion region
 * @mt_handle[IN]: motion tracking parameter handle
 * @len[IN]: VPSS_MD_DIMENSION in vertical direction
 * @rect[IN]: rectangle box of the largest motion region
 * @rt_angle[OUT]: absolute rotation angel in horizontal and vertical direction
 * return: 0 need no rotation; 1 need rotation; 2 need rotation and reset to inital position, -1 param error, no effect
 * notes:
 */
int ak_mt_tracking(void *mt_handle, short len, MT_RECTANGLE *rect,MT_POINT *rt_angle)
{
	if (NULL == mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_tracking: mt_handle is NULL\n");
		return -1;
	}

	if (NULL == rt_angle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_tracking: rt_angle pointer is NULL\n");
		return -1;
	}

	if (len < 1)
	{
		len = 1;
	}

	if (len > MD_SIZE)
	{
		len = MD_SIZE;
	}

	TRACKING_PARAM *param = (TRACKING_PARAM *)mt_handle;

	if (mt_handle != param->mt_handle)
	{
		ak_print_error(MODULE_ID_APP, "ak_mt_tracking: input mt_handle is error\n");
		return -1;
	}
	
	rt_angle->px = 0;
	rt_angle->py = 0;
	
	/* camera rotation is completed, so motion tracking is allowed */
	if (AK_TRUE == param->mt_enable)
	{

		int ret = AK_FALSE;
		if (rect->width && rect->height 
			&& (rect->width*rect->height>=param->size_min) && (rect->width*rect->height<=param->size_max))
			ret = AK_TRUE;

		if (AK_TRUE == ret)  // motion region is detected
		{
			MT_POINT distance = {0};
			ret = mt_mrd_Isoutofcenter(param, rect, &distance, len, 
				param->boundary_size_h, param->boundary_size_v);

			if (AK_TRUE == ret) // motion region is out of video frame center
			{
				param->valid_rect_num_v = 0;
				param->decision_enalbe = AK_TRUE; // motion region change switch is open
								
				/* record  valid motion region and corresponding count */
				if (distance.px > 0)
				{
					if (param->valid_rect_right_num > 0)
					{
						param->valid_rect_right_num = 0;
						param->valid_rect_num = 0;
					}
					
					param->valid_rect_left_num += 1;
				}
				else if (distance.px < 0)
				{
					if (param->valid_rect_left_num > 0)
					{
						param->valid_rect_left_num = 0;
						param->valid_rect_num = 0;
					}
					
					param->valid_rect_right_num += 1;
				}

				mt_member_copy(&param->valid_rect[param->valid_rect_num], rect);
				param->valid_rect_num += 1;

				ret = mt_get_rotation_angle(param, len);
				
				/* determine the rotation direction */
				// max_decision_frame is accessible or motion region is ready to leave certain areas
				if (AK_TRUE == ret)
				{
					short rotate_angle_h = 0;   // unsigned short will cause 0-n>0
					short rotate_angle_v = 0;
					
					if ((param->valid_rect_left_num>param->valid_rect_right_num) 
						&& (param->valid_rect_left_num>param->valid_rect_center_num))
					{
						rt_angle->px = param->rotation_dir_h;
					}
					if ((param->valid_rect_right_num>param->valid_rect_left_num) 
						&& (param->valid_rect_right_num>param->valid_rect_center_num))
					{
						rt_angle->px = -1*param->rotation_dir_h;
					}
					
					rotate_angle_h = param->current_angle_h + rt_angle->px*param->agl_h;
					rotate_angle_v = param->current_angle_v + param->rotation_dir_v * param->agl_v;

					if (rotate_angle_h < param->min_agl_h)					
					{						
						rotate_angle_h = param->min_agl_h;					
					}					
					if (rotate_angle_h > param->max_agl_h)					
					{					
						rotate_angle_h = param->max_agl_h;					
					}					
					if (rotate_angle_v < param->min_agl_v)					
					{						
						rotate_angle_v = param->min_agl_v;					
					}					
					if (rotate_angle_v > param->max_agl_v)					
					{						
						rotate_angle_v = param->max_agl_v;					
					}						
					rt_angle->px = rotate_angle_h;					
					rt_angle->py = rotate_angle_v;	
					
					/* if  vertical rotation are forbidden, rotate camera in horizontal direction only */					
					if (AK_FALSE == param->rotate_v_enable)						
						rt_angle->py = param->current_angle_v;
					if (AK_FALSE == param->rotate_h_enable)						
						rt_angle->px = param->current_angle_h;
					if ((rt_angle->px - param->current_angle_h)
						|| (rt_angle->py - param->current_angle_v))					
					{		
						//printf("camera is ready to track motion region!\n");						
						mt_reset_tracking_param(param);
						if (param->max_decision_fm == param->max_decision_fm_backup)
						{
							param->max_decision_fm = param->max_decision_fm - 2;
						}
						
						//printf("inner output, angle_h: %d, angle_v: %d\n", rt_angle->px, rt_angle->py);
						return AK_TRUE;					
					}					
					else					
					{						
						//printf("fake out of center\n");						
						param->decision_enalbe = AK_FALSE;
						param->valid_rect_num = 0;
						param->valid_rect_left_num = 0;						
						param->valid_rect_right_num = 0;						
						param->valid_rect_botoom_num = 0;						
						param->valid_rect_top_num = 0;						
						param->valid_rect_center_num = 0;						
						return AK_FALSE;					
					}				
				}
			}
			/* if motion region is not out of video frame center, do which follows */
			else
			{
				/* if vertical rotation switch is opened, do motion tracking in vertical direction */
				if (AK_TRUE == param->rotate_v_enable)
				{	
					int i = 0;
					int weight[DECISION_FRAMES_CAP_V] = {0};
					weight[1] = 2;
					weight[2] = 3;
					weight[3] = 5;
					MT_RECTANGLE tmp_rect = {{0}};
					
					mt_member_copy(&param->valid_rect_v[param->valid_rect_num_v], rect);
					param->valid_rect_num_v += 1;

					if (param->valid_rect_num_v >= DECISION_FRAMES_CAP_V)
					{
						int t_py = 0;
						int t_height = 0; // if use MT_RECTANGLE originally, the value will overflow
						for (i = 1; i < param->valid_rect_num_v - 1; i++)
						{
							t_py += weight[i] * param->valid_rect_v[i].pt.py;
							t_height += weight[i] * param->valid_rect_v[i].height;
						}
						tmp_rect.pt.py = t_py / 10;
						tmp_rect.height = t_height / 10;
						
						ret = mt_get_rotation_angle_v(param, len, &tmp_rect);

						if (ret)
						{	
							rt_angle->px = param->current_angle_h;
							rt_angle->py = param->current_angle_v + param->rotation_dir_v * param->agl_v;

							if (rt_angle->py < param->min_agl_v)					
							{						
								rt_angle->py = param->min_agl_v;					
							}					
							if (rt_angle->py > param->max_agl_v)					
							{						
								rt_angle->py = param->max_agl_v;					
							}
						
							if (rt_angle->py - param->current_angle_v)					
							{	
								mt_reset_tracking_param(param);
								return AK_TRUE;					
							}
						}
						param->valid_rect_num_v = 0;
					}
				}
				
				/* once motion region change switch opened, motion region which is not out of center is also recorded */
				if (AK_TRUE == param->decision_enalbe)
				{
					param->decision_enalbe = AK_FALSE;
					param->valid_rect_num = 0;
					param->valid_rect_left_num = 0;
					param->valid_rect_right_num = 0;
					param->valid_rect_botoom_num = 0;
					param->valid_rect_top_num = 0;
					param->undetected_num = 0;
				}
				/* if invalid motion region has not been detected, record the first invalid motion region */
				if (param->invalid_rect_num == 0)
				{
					mt_member_copy(&param->invalid_rect[param->invalid_rect_num], rect);
					param->invalid_rect_num += 1;
				}
				/* calculate the max distance between recorded invalid motion region, 
				once the value is greater than given threshold, recorded invalid motion region count is reset to zero */
				else
				{
					mt_tracking_max_distance(param, rect);
					mt_member_copy(&param->invalid_rect[param->invalid_rect_num], rect);
					param->invalid_rect_num += 1;
					if (param->current_max_invalid_rect_dis > param->invalid_dis)
					{
						//printf("valid distance is accessible, invalid rects is reset\n");
						param->invalid_rect_num = 0;
						param->current_max_invalid_rect_dis = 0;
					}
				}

				/* once invalid motion region count is greater than given threshold, camera and tracking parameters are reset*/
				if (param->invalid_rect_num >= param->max_invalid_fm)
				{
					rt_angle->px = param->init_agl_h;
					rt_angle->py = param->init_agl_v;
					mt_reset_tracking_param(param);

					if (param->max_decision_fm != param->max_decision_fm_backup)
					{
						param->max_decision_fm = param->max_decision_fm_backup;
					}
			
					if ((param->current_angle_h!=param->init_agl_h) || (param->current_angle_v!=param->init_agl_v))
					{
						return 2;
					}
					else
					{
						return AK_FALSE;
					}
				}
			}			
		}

		/* motion region is not deteted, so increase the count of invalid motion region */
		else
		{
			param->valid_rect_num_v = 0;
		
			if (AK_TRUE == param->decision_enalbe)
			{
				if (param->undetected_num < 2 * param->max_decision_fm)
				{
					param->undetected_num++;
				}
				else
				{
					param->decision_enalbe = AK_FALSE;
					param->valid_rect_num = 0;
					param->valid_rect_left_num = 0;
					param->valid_rect_right_num = 0;
					param->valid_rect_botoom_num = 0;
					param->valid_rect_top_num = 0;
					param->undetected_num = 0;
				}
			}

			mt_member_copy(&param->invalid_rect[param->invalid_rect_num], rect);
			param->invalid_rect_num += 1;
		    if (param->invalid_rect_num >= param->max_invalid_fm)
			{
				rt_angle->px = param->init_agl_h;
				rt_angle->py = param->init_agl_v;
				mt_reset_tracking_param(param);

				if (param->max_decision_fm != param->max_decision_fm_backup)
				{
					param->max_decision_fm = param->max_decision_fm_backup;
				}
				
				if ((param->current_angle_h!=param->init_agl_h) || (param->current_angle_v!=param->init_agl_v))
				{
					return 2;
				}
				else
				{
					return AK_FALSE;
				}
			}	
		}
	}
	
	else    // wait for camera rotation
	{	
		usleep(param->wait_time*1000);
		param->mt_enable = AK_TRUE;
	}
	
	return AK_FALSE;
}


/*********************************************/
/*********************************************/
/*********************************************/
// inner interface function
/**
 * mt_reset_tracking_param - reset tracking parameters
 * @param[IN]: motion tracking parameter handle
 * return: none
 * notes:
 */
void mt_reset_tracking_param(TRACKING_PARAM *param)
{
	param->invalid_rect_num = 0;       // no recorded invaild motion region
	param->valid_rect_num = 0;         // no recorded valid motion region
	param->valid_rect_left_num = 0;    // no recorded valid motion region on left, right, botoom, and top of valid video frame center
	param->valid_rect_right_num = 0;
	param->valid_rect_botoom_num = 0;
	param->valid_rect_top_num = 0;
	param->valid_rect_center_num = 0;  // no recorded valid motion region in the center of valid video frame
	param->current_max_invalid_rect_dis = 0;
	//param->current_wait_frames = 0;
	param->decision_enalbe = AK_FALSE;  // disable change valid motion region
	param->undetected_num = 0;
	param->agl_v = 0;
	param->valid_rect_num_v = 0;
}

/**
 * mt_member_copy - copy member of struct rectangle
 * @rect_org[IN]: motion region
 * @rect_dst[OUT]: motion region
 * return: none
 * notes:
 */
void mt_member_copy(MT_RECTANGLE *rect1, MT_RECTANGLE *rect2)
{
	rect1->pt.px = rect2->pt.px;
	rect1->pt.py = rect2->pt.py;
	rect1->height = rect2->height;
	rect1->width = rect2->width;
}

/**
 * mt_mrd_Isoutofcenter - whether motion region is out of the center region of video frame
 * @param[IN]: motion tracking parameter handle
 * @rect[IN]: motion region
 * @distance[OUT]: distance between motion region center and video frame center in horizontal and vertical direction
 * @len[IN]: VPSS_MD_DIMENSION in vertical direction
 * @size_h[IN]: video frame center size in horizontal direction
 * @size_v[IN]: video frame center size in vertical direction
 * return: 1 out of center; 0 not out of center
 * notes:
 */
int mt_mrd_Isoutofcenter(TRACKING_PARAM * param, MT_RECTANGLE *rect, MT_POINT *distance, short len, short size_h, short size_v)
{
	signed char pos_x = rect->pt.px+rect->width/2; // horizontal position of motion region center, if the type of pos_x is unsigned char, the value of distanece->px will always be bigger than zero

	/* distance between detecting motion region center and video frame center */
	if ((pos_x>size_h) &&  (pos_x<=MD_SIZE-size_h))
		distance->px = 0;
	else
		distance->px = pos_x - MD_SIZE/2;
	if (distance->px < 0)
		distance->px = distance->px - 1;

	if ((distance->px!=0) || (distance->py!=0))
		return AK_TRUE;
	else
		return AK_FALSE;
}

/**
 * mt_tracking_max_distance - calculate the max distance between recorded invalid motion region
 * @param[IN]: motion tracking parameter handle
 * @rect[IN]: motion region
 * return: none
 * notes:
 */
void mt_tracking_max_distance(TRACKING_PARAM *param, MT_RECTANGLE *rect)
{
	unsigned short tmp_dis = 0;
	unsigned short i;
	/* calculate distance between detecting motion region and recorded invalid motion region, and return the max distance */
	for (i=0; i<param->invalid_rect_num; i++)
	{
		if (param->invalid_rect[i].width == 0)
		{
			tmp_dis = 0;
		}
		else
		{
			tmp_dis = (param->invalid_rect[i].pt.px-rect->pt.px)*(param->invalid_rect[i].pt.px-rect->pt.px)
				+ (param->invalid_rect[i].pt.py-rect->pt.py)*(param->invalid_rect[i].pt.py-rect->pt.py);
		}
		if (tmp_dis > param->current_max_invalid_rect_dis)
		{
			param->current_max_invalid_rect_dis = tmp_dis;
		}
	}
}

/**
 * mt_rect_optimization - optimize the detected rectangles and return some useful information for speed calculation
 * @param[IN]: motion tracking parameter handle
 * return: -1 speed calculation is not required, otherwise, the index of the start rect to calculate speed 
 * notes:
 */
int mt_rect_optimization(TRACKING_PARAM *param)
{
	int i;
	MT_RECTANGLE *pre_rect = &param->valid_rect[param->valid_rect_num - 2];	
	MT_RECTANGLE *after_rect = &param->valid_rect[param->valid_rect_num - 1];

	// the last recorded motion region is part of his nearest record
	if ((after_rect->pt.px >= pre_rect->pt.px)
		&& (after_rect->pt.px + after_rect->width <= pre_rect->pt.px + pre_rect->width)
		&& (after_rect->pt.py >= pre_rect->pt.py)
		&& (after_rect->pt.py + after_rect->height <= pre_rect->pt.py + pre_rect->height)
		&& (after_rect->height * after_rect->width * 10 < pre_rect->height * pre_rect->width * 5))
	{
		return -1;
	}

	for (i = 1; i < param->valid_rect_num; i++)
	{
		pre_rect = &param->valid_rect[param->valid_rect_num - i - 1];
		after_rect = &param->valid_rect[param->valid_rect_num - i];

		// there is no intersection between the latter motion region record and his nearest previous record 
		if ((after_rect->pt.px >= pre_rect->pt.px + pre_rect->width)
			|| (after_rect->pt.px + after_rect->width <= pre_rect->pt.px)
			|| (after_rect->pt.py >= pre_rect->pt.py + pre_rect->height)
			|| (after_rect->pt.py + after_rect->height <= pre_rect->pt.py))
		{
			return param->valid_rect_num - i;
		}
	}
	return 0;
}

/**
 * mt_get_rotation_angle - calculate the rotation angle based on the distance and speed of moving object
 * @param[IN]: motion tracking parameter handle
 * @len[IN]: VPSS_MD_DIMENSION in vertical direction
 * return: 1 ready to rotate, 0 wait for more frames
 * notes:
 */
int mt_get_rotation_angle(TRACKING_PARAM *param, short len)
{	
	static const short block_angle[MD_SIZE*2+1] = 
	{
		0, 2, 4, 5, 7, 9, 11, 12, 14, 16,
		17, 19, 21, 22, 24, 25, 27, 28, 29, 31,
		32, 33, 35, 36, 37, 38, 39, 40, 41, 42, 
		43, 44, 45, 46, 47, 48, 48, 49, 50, 51, 
		51, 52, 53, 53, 54, 55, 55, 56, 56, 57, 
		57, 58, 58, 59, 59, 60, 60, 61, 61, 62, 
		62, 62, 63, 63, 63
	};

	static const short angle_time[91] = 
	{
		0, 19, 29, 39, 49, 59, 79, 89, 99, 109,
		119, 129, 139, 149, 169, 179, 189, 199, 209, 219,
		229, 249, 259, 269, 279, 289, 299, 309, 329, 339, 
		349, 359, 369, 379, 389, 409, 419, 429, 439, 449, 
		459, 469, 479, 499, 509, 519, 529, 539, 549, 559, 
		579, 589, 599, 609, 619, 629, 639, 659, 669, 679, 
		689, 699, 709, 719, 739, 749, 759, 769, 779, 789, 
		799, 819, 829, 839, 849, 859, 869, 879, 899, 909, 
		919, 929, 939, 949, 959, 969, 989, 999, 1009, 1019, 
		1029
	};
	
	MT_POINT distance = {0};	
	int speed = 0;
	int ret = -1;
	short pos_x = 0;
	short pos_y = 0; 
	int tmp_dis = 0;
	int total_dis = 0;
	
	if (param->valid_rect_num >= 2)
	{	
		ret = mt_rect_optimization(param);
		MT_RECTANGLE tmp_rect = {{0}};
		
		if (ret == -1)
		{
			if (param->valid_rect_num == 2)
			{
				mt_member_copy(&tmp_rect, &param->valid_rect[0]);
			}
			else if (param->valid_rect_num == 3)
			{
				tmp_rect.pt.py = (param->valid_rect[0].pt.py * 4 + param->valid_rect[1].pt.py * 6) / 10;
				tmp_rect.height = (param->valid_rect[0].height * 4 + param->valid_rect[1].height * 6) / 10;
			}
			else
			{
				tmp_rect.pt.py = (param->valid_rect[param->valid_rect_num - 2].pt.py * 5
					+ param->valid_rect[param->valid_rect_num - 3].pt.py * 3
					+ param->valid_rect[param->valid_rect_num - 4].pt.py * 2) / 10;
				tmp_rect.height = (param->valid_rect[param->valid_rect_num - 2].height * 5
					+ param->valid_rect[param->valid_rect_num - 3].height * 3
					+ param->valid_rect[param->valid_rect_num - 2].height * 2) / 10;
			}
		}
		else if (ret == param->valid_rect_num - 1)
		{
			mt_member_copy(&tmp_rect, &param->valid_rect[param->valid_rect_num - 1]);
		}
		else
		{
			if (param->valid_rect_num == 2)
			{
				tmp_rect.pt.py = (param->valid_rect[0].pt.py * 4 + param->valid_rect[1].pt.py * 6) / 10;
				tmp_rect.height = (param->valid_rect[0].height * 4 + param->valid_rect[1].height * 6) / 10;
			}
			else
			{
				tmp_rect.pt.py = (param->valid_rect[param->valid_rect_num - 2].pt.py * 5
					+ param->valid_rect[param->valid_rect_num - 3].pt.py * 3
					+ param->valid_rect[param->valid_rect_num - 4].pt.py * 2) / 10;
				tmp_rect.height = (param->valid_rect[param->valid_rect_num - 2].height * 5
					+ param->valid_rect[param->valid_rect_num - 3].height * 3
					+ param->valid_rect[param->valid_rect_num - 2].height * 2) / 10;
			}
		}
		mt_get_rotation_angle_v(param, len, &tmp_rect);
	}
	
	if (param->valid_rect_left_num > param->valid_rect_right_num)	
	{					
		pos_x = param->valid_rect[param->valid_rect_left_num-1].pt.px
			+ param->valid_rect[param->valid_rect_left_num-1].width/2;

		if (param->valid_rect[param->valid_rect_left_num-1].width%2)
		{
			distance.px = 2*pos_x - MD_SIZE - 1;
		}
		else
		{
			distance.px = 2*pos_x - MD_SIZE - 2;
		}

		if ((ret != -1) && (ret != param->valid_rect_num - 1))
		{	
			pos_y = param->valid_rect[ret].pt.px
				+ param->valid_rect[ret].width/2;
			speed = (pos_x - pos_y)*2;
		}
		else
		{	
			speed = 0;
		}
	}	
	else if(param->valid_rect_right_num > param->valid_rect_left_num)	
	{		
		pos_x = param->valid_rect[param->valid_rect_right_num-1].pt.px
			+ param->valid_rect[param->valid_rect_right_num-1].width/2;

		if (param->valid_rect[param->valid_rect_right_num-1].width%2)
		{
			distance.px = MD_SIZE - pos_x*2 + 1;
		}
		else
		{
			distance.px = MD_SIZE - pos_x*2 + 2;
		}

		if ((ret != -1) && (ret != param->valid_rect_num - 1))
		{
			pos_y = param->valid_rect[ret].pt.px
				+ param->valid_rect[ret].width/2;
			speed = (pos_y - pos_x)*2;
		}
		else
		{
			speed = 0;
		}
	}	

	if (param->mt_factor < 0)
	{
		speed = 0;
	}
	
	if (speed == 0)
	{
		total_dis = distance.px;
		if ((total_dis >= MD_SIZE * 2 - 4) || (total_dis <= 10) 
			|| (param->valid_rect_num >= param->max_decision_fm))
		{
			total_dis = distance.px;
		}
		else
		{
			param->agl_v = 0;
			return AK_FALSE;
		}
	}
	
	if (speed > 0)
	{	 
		total_dis = distance.px;

		if ((total_dis >= MD_SIZE*2 - 4) || (param->valid_rect_num >= param->max_decision_fm))
		{
			tmp_dis = ((speed << 8) * angle_time[block_angle[total_dis]] 
				/ (1000 / param->fps * (param->valid_rect_num - 1 - ret + param->undetected_num))) >> 8;
			while ((total_dis - tmp_dis < distance.px) && (total_dis < 64))
			{
				total_dis = total_dis + 1;
				tmp_dis = ((speed << 8) * angle_time[block_angle[total_dis]] 
					/ (1000 / param->fps * (param->valid_rect_num - 1))) >> 8;
			}
		}
		else
		{
			param->agl_v = 0;
			return AK_FALSE;
		}
	}

	if (speed < 0)
	{	
		total_dis = distance.px;

		if ((total_dis <= 10) || (param->valid_rect_num >= param->max_decision_fm))
		{
			tmp_dis = ((speed << 8) * angle_time[block_angle[total_dis]] 
				/ (1000 / param->fps * (param->valid_rect_num - 1 - ret + param->undetected_num))) >> 8;
			while ((total_dis - tmp_dis > distance.px) && (total_dis > 0))
			{
				total_dis = total_dis - 1;
				tmp_dis = ((speed << 8) * angle_time[block_angle[total_dis]] 
					/ (1000 / param->fps * (param->valid_rect_num - 1))) >> 8;
			}
		}
		else
		{
			param->agl_v = 0;
			return AK_FALSE;
		}
	}

	total_dis = (total_dis * param->mt_factor) >> 8;

	if (total_dis > MD_SIZE*2)
		total_dis = MD_SIZE*2;
	if (total_dis < 0)
		total_dis = 0;
	param->agl_h = block_angle[total_dis];
	if (param->agl_h < 6)
		param->agl_h = 0 ;
	
	return AK_TRUE;
}

/**
 * mt_get_rotation_angle_v - calculate the rotation angle based on the distance and speed of moving object
 * @param[IN]: motion tracking parameter handle
 * @len[IN]: VPSS_MD_DIMENSION in vertical direction
 * @rect[IN]: motion region
 * return: 1 ready to rotate, 0 no rotation is required
 * notes:
 */
int mt_get_rotation_angle_v(TRACKING_PARAM *param, short len, MT_RECTANGLE *rect)
{
	if (param->main_decision_dir_v > 0)
	{	
		if (rect->pt.py <= len / 8)
		{
			//printf("condition 1\n");
			if (rect->pt.py + rect->height - 1 >= len - 2)
			{
				param->agl_v = 5;
			}
			else if (rect->pt.py + rect->height - 1 >= len * 3 / 4)
			{
				param->agl_v = 10;
			}
			else
			{
				param->agl_v = 25;
			}
			return 1;
		}
		else if (rect->pt.py + rect->height - 1 >= len - 2)
		{
			if (rect->pt.py >= len * 2 / 3 - 1)
			{
				param->agl_v = -25;
			}
			else if (rect->pt.py >= len / 2 - 1)
			{
				param->agl_v = -10;
			}
			else
			{
				param->agl_v = -5;
			}
			return 1;
		}
		else
		{
			//printf("condition 3\n");
			param->agl_v = 0;
			return 0;
		}
	}
	else
	{
		if (rect->pt.py + rect->height - 1  >= len * 7 / 8)
		{
			if (rect->pt.py <= 2)
			{
				param->agl_v = -5;
			}
			else if (rect->pt.py <= len / 4)
			{
				param->agl_v = -10;
			}
			else
			{
				param->agl_v = -25;
			}
			return 1;
		}
		else if (rect->pt.py <= 2)
		{
			if (rect->pt.py + rect->height - 1 <= len / 3 + 1)
			{
				param->agl_v = 25;
			}
			else if (rect->pt.py + rect->height - 1 <= len / 2 + 1)
			{
				param->agl_v = 10;
			}
			else
			{
				param->agl_v = 5;
			}
			return 1;
		}
		else
		{
			param->agl_v = 0;
			return 0;
		}
	}
}
