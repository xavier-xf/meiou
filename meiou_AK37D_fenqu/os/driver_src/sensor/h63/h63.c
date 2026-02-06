/*
 * h63 Camera Driver
 *
 * Copyright anyka
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-image-sizes.h>
#include "../include/ak_video_priv_cmd.h"
#include "../include_internal/ak_video_priv_cmd_internal.h"
#include "../include/ak_isp_drv.h"

#include "../include/ak_sensor.h"
#include "../include/sensor_cmd.h"
#include "../common/sensor_sys.h"
#include "../common/sensor_i2c.h"

/*
 *
SENSOR_PWDN_LEVEL:	the io level when sensor power down
SENSOR_RESET_LEVEL:	the io level when sensor reset
SENSOR_I2C_ADDR:	the sensor slave address, 7 bits width.
the i2c bus left shift 1bits.
SENSOR_ID:	the sensor ID.
SENSOR_MCLK:	mclk, unit：MHz。24MHz or 27MHz mclk for most sensors.
NOTE: 27MHz mclk is inaccurate.
SENSOR_REGADDR_BYTE:	sensor register address bytes width, 1 byte or 2 bytes.
SENSOR_DATA_BYTE:		register's value bytes width, 1 byte or 2bytes.
SENSOR_OUTPUT_WIDTH:	use for isp cut window MAX width.
SENSOR_OUTPUT_HEIGHT:	use for isp cut window MAX height.
SENSOR_VALID_OFFSET_X:	use for isp cut window left offset.
SENSOR_VALID_OFFSET_Y:	use for isp cut window top offset.
SENSOR_BUS_TYPE:		sensor output data type.
SENSOR_IO_INTERFACE:	sensor output data bus type.
SENSOR_IO_LEVEL:		sensor output data io level.
MAX_FPS:				current sensor configurtion support max fps.
EXP_EFFECT_FRAMES:		!!IMPORT!! frequency for update exp_time
0 - adjust exp_time for every frames
1 - adjust exp_time for 1/2 frames
2 - adjust exp_time for 1/3 frames
and so on.
A_GAIN_EFFECT_FRAMES:	!!IMPORT!! frequency for update a_gain
0 - adjust a_gain for every frames
1 - adjust a_gain for 1/2 frames
2 - adjust a_gain for 1/3 frames
and so on.
D_GAIN_EFFECT_FRAMES:	!!IMPORT!! frequency for update d_gain
0 - adjust d_gain for every frames
1 - adjust d_gain for 1/2 frames
2 - adjust d_gain for 1/3 frames
and so on.
DELAY_FLAG
 *
 * */
#define SENSOR_PWDN_LEVEL		0
#define SENSOR_RESET_LEVEL		0
#define SENSOR_I2C_ADDR			0x40
#define SENSOR_ID           (0x0a63)
#define SENSOR_MCLK				27
#define SENSOR_REGADDR_BYTE		1
#define SENSOR_DATA_BYTE		1
#define SENSOR_OUTPUT_WIDTH		1280
#define SENSOR_OUTPUT_HEIGHT	720
#define SENSOR_VALID_OFFSET_X	0
#define SENSOR_VALID_OFFSET_Y	0
#define SENSOR_BUS_TYPE			BUS_TYPE_RAW
//#define SENSOR_IO_INTERFACE		DVP_INTERFACE
#define SENSOR_IO_INTERFACE		MIPI_INTERFACE
#define SENSOR_IO_LEVEL			IO_LEVEL_1V8
#define MIPI_MBPS				742
#define MIPI_LANES				1
#define MAX_FPS					20
#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	0	/*adjust a_gain for every frames*/
#define D_GAIN_EFFECT_FRAMES	0	/*adjust d_gain for every frames*/

#define DELAY_FLAG		(0xffff)
#define OV_MAX(a, b)            (((a) < (b) ) ?  (b) : (a))
#define OV_MIN(a, b)            (((a) > (b) ) ?  (b) : (a))
#define OV_CLIP3(low, high, x)  (OV_MAX(OV_MIN((x), high), low))

#define PCLK_FREQ  (74250000)  

/**
 * 适配运算 FPS 时需要转换成全帧的宽度。
 */
#define __get_frame_width(priv) (2 * __get_frame_half_width(priv))

/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct h63_win_size {
	char				*name;
	u32				width;
	u32				height;
	const struct regval_list	*regs;
};

/*
 * store sensor fps informations
 * @current_fps: current fps
 * @to_fps: should changed to the fps
 * @to_fps_target_frame_height: the fps paramter
 */
struct sensor_fps_info {
	int current_fps;
	int to_fps;
	int to_fps_target_frame_height;
	int current_target_frame_height;

	int current_exp;
	int target_exp;

	int frame_width;
};

/*
 * the host callbacks for sensor
 * @isp_timing_cb_info: isp timing callback.
 * 	a few sensor need the callback when fps had changed
 */
struct host_callbacks {
	struct isp_timing_cb_info isp_timing_cb_info;
};

/*
 * aec paramters. some sensors need record paramters.
 */
struct h63_aec_parms {
};

static const struct v4l2_ctrl_ops h63_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &h63_ctrl_ops,
	.id		= SENSOR_GET_ID,
	.name	= "get sensor id",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= 0xfffffff,
	.step	= 1,
	.def	= 0,
	.flags	= V4L2_CTRL_FLAG_VOLATILE,/*set V4L2_CTRL_FLAG_VOLATILE avoid get cached*/
};

/*
 * the sensor struct
 * @client: point to i2c client
 * @subdev: v4l2 subdev struct
 * @hdl: 	v4l2 control handler
 * @cfmt_code:
 * @win:	default window
 * @gpio_reset:	gpio number for reset pin
 * @gpio_pwdn:	gpio number for power down pin
 * @cb_info:	sensor callbacks
 * @fps_info:	fps informations
 * @hcb:		host callbacks
 * @aec_parms:	aec paramters
 */
struct h63_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct h63_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct h63_aec_parms aec_parms;

	AK_ISP_SENSOR_INIT_PARA para;

	struct v4l2_ctrl *ctrl_sensor_get_id;
};
static struct h63_priv *g_h63_priv = NULL;

static AK_ISP_SENSOR_CB h63_cb;

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 0;
module_param(check_id, int, 0644);

static void h63_set_fps_async(struct h63_priv *priv);
static int __h63_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int h63_sensor_read_id_func(void *arg);   //no use IIC bus
static int h63_sensor_get_resolution_func(void *arg, int *width, int *height);
static int h63_sensor_set_fps_func(void *arg, const int fps);
static int h63_sensor_probe_id_func(void *arg);  //use IIC bus
static int __get_frame_half_width(struct h63_priv *priv);
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);

static u32 h63_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct h63_priv *to_h63(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct h63_priv,
			subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct h63_priv, hdl)->subdev;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int h63_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int h63_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	int ret = 0;

	switch (ctrl->id) {
		case SENSOR_GET_ID:
			ctrl->val = SENSOR_ID;
			break;

		default:
			ret = -EINVAL;
			pr_err("%s id:%d no support\n", __func__, ctrl->id);
			break;
	}

	return ret;
}

/*
 * XXXX_s_stream -
 * set ctrls
 * soc_camera_ops functions
 *
 * @ctrl:			pointer to ctrl
 */
static int h63_s_ctrl(struct v4l2_ctrl *ctrl)
{
	return 0;
}

/*
 * XXXX_s_power -
 * set power operation
 * soc_camera_ops functions
 *
 * @sd:			subdev
 * @on:			power flags
 */
static int h63_core_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

/*
 * XXXX_get_sensor_id -
 * get sensor ID
 * private callback functions
 *
 * @ctrl:			pointer to ctrl
 */
static int h63_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = h63_sensor_read_id_func(NULL);   //no use IIC bus
	return 0;
}

/*
 * XXXX_get_sensor_id -
 * get sensor callback
 * private callback functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int h63_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct h63_priv *priv = to_h63(client);

	ctrl->value = (int)&priv->cb_info;
	return 0;
}

static int h63_get_max_exp_for_fps(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct h63_priv *priv = to_h63(client);
	int fps = ctrl->value;
	int tmp;
	int target_frame_height;

	/// FPS = PCLK / FRAMEW / FRAMEH
	/// 计算出将要设置的帧长度。
	switch (fps) {
		case 12:
			tmp = 1250;
			break;

		case 14:
			tmp = 1428;
			break;

		default:
			tmp = fps * 100;
			break;
	}

	target_frame_height = PCLK_FREQ * 10 / __get_frame_width(priv);
	target_frame_height *= 10;
	target_frame_height /= tmp;

	ctrl->value = target_frame_height - 8;
	return 0;
}

/*
 * XXXX_core_g_ctrl -
 * get control
 * core functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int h63_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = h63_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = h63_get_sensor_cb(sd, ctrl);
			break;

		case GET_MAX_EXP_FOR_FPS:
			ret = h63_get_max_exp_for_fps(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

/*
 * XXXX_set_isp_timing_cb -
 * set isp timing callback function
 * private callback functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int h63_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct h63_priv *priv = to_h63(client);
	struct isp_timing_cb_info *isp_timing_cb_info = (void *)ctrl->value;

	memcpy(&priv->hcb.isp_timing_cb_info,
			isp_timing_cb_info,
			sizeof(struct isp_timing_cb_info));
	return 0;
}

/*
 * XXXX_set_fps_direct -
 * set sensor fps directly, synchronously.
 * private callback functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int h63_set_fps_direct(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct h63_priv *priv = to_h63(client);
	int fps = ctrl->value;

	h63_sensor_set_fps_func(priv, fps);
	//h63_set_fps_async(priv);
	return 0;
}

/*
 * XXXX_core_s_ctrl -
 * set core s_ctrl
 * core functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int h63_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = h63_set_isp_timing_cb(sd, ctrl);
			break;

		case SET_FPS_DIRECT:
			ret = h63_set_fps_direct(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static int h63_store_initial_regs(struct h63_priv *priv, void *arg)
{
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *sensor_para = arg;
	void *p_reg_info;
	int ret;

	p_reg_info = sensor_para->reg_info;
	sensor_para->reg_info =
		kmalloc(sizeof(AK_ISP_SENSOR_REG_INFO) * sensor_para->num, GFP_KERNEL);
	if (!sensor_para->reg_info) {
		pr_err("%s alloc1 fail\n", __func__);
		ret = -ENOMEM;
		goto alloc1_fail;
	} else {
		if (copy_from_user(sensor_para->reg_info, p_reg_info,
					sizeof(AK_ISP_SENSOR_REG_INFO) * sensor_para->num)) {
			pr_err("%s cp2 fail\n", __func__);
			ret = -EFAULT;
			goto cp2_fail;
		}

		if (priv->para.reg_info) {
			devm_kfree(&client->dev, priv->para.reg_info);
			priv->para.reg_info = NULL;
			priv->para.num = 0;
		}

		/*store sensor configurtion*/
		priv->para.num = sensor_para->num;
		priv->para.reg_info = devm_kzalloc(&client->dev,
				sizeof(AK_ISP_SENSOR_REG_INFO)*sensor_para->num, GFP_KERNEL);
		if (!priv->para.reg_info) {
			pr_err("%s alloc2 fail\n", __func__);
			goto alloc2_fail;
		}
		memcpy(priv->para.reg_info, sensor_para->reg_info,
				sizeof(AK_ISP_SENSOR_REG_INFO)*sensor_para->num);

		pr_debug("%s para.num;%d\n", __func__, priv->para.num);
	}

	kfree(sensor_para->reg_info);
	return 0;

alloc2_fail:
cp2_fail:
	kfree(sensor_para->reg_info);
alloc1_fail:
	return ret;
}

static long h63_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct h63_priv *priv = to_h63(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			h63_store_initial_regs(priv, arg);
			break;

		case AK_SENSOR_GET_MAX_EXP_FOR_FPS: {
			struct sensor_max_exp_for_fps *exp_for_fps = arg;
			struct v4l2_control ctrl;

			ctrl.value = exp_for_fps->fps;
			ret = h63_get_max_exp_for_fps(sd, &ctrl);
			if (!ret)
				exp_for_fps->max_exp = ctrl.value;
			break;
		}

		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

/*
 * XXXX_get_fmt -
 * get format
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @format:			return format
 */
static int h63_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct h63_priv *priv = to_h63(client);
	static struct h63_win_size win = {
		.name = "default",
		.width = SENSOR_OUTPUT_WIDTH,
		.height = SENSOR_OUTPUT_HEIGHT,
		.regs = NULL,
	};

	if (format->pad)
		return -EINVAL;

	if (!priv->win) {
		priv->win = &win;
		priv->cfmt_code = MEDIA_BUS_FMT_UYVY8_2X8;
	}

	mf->width	= priv->win->width;
	mf->height	= priv->win->height;
	mf->code	= priv->cfmt_code;

	switch (mf->code) {
		case MEDIA_BUS_FMT_RGB565_2X8_BE:
		case MEDIA_BUS_FMT_RGB565_2X8_LE:
			mf->colorspace = V4L2_COLORSPACE_SRGB;
			break;
		default:
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_UYVY8_2X8:
			mf->colorspace = V4L2_COLORSPACE_JPEG;
	}
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

/*
 * XXXX_set_fmt -
 * set format
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @format:			format to set
 */
static int h63_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	if (format->pad)
		return -EINVAL;

	mf->field	= V4L2_FIELD_NONE;

	switch (mf->code) {
		case MEDIA_BUS_FMT_RGB565_2X8_BE:
		case MEDIA_BUS_FMT_RGB565_2X8_LE:
			mf->colorspace = V4L2_COLORSPACE_SRGB;
			break;
		default:
			mf->code = MEDIA_BUS_FMT_UYVY8_2X8;
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_UYVY8_2X8:
			mf->colorspace = V4L2_COLORSPACE_JPEG;
	}

	if (cfg)
		cfg->try_fmt = *mf;
	return 0;
}

/*
 * XXXX_set_fmt -
 * set format
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @sel:			return selection
 */
static int h63_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	sel->r.left = SENSOR_VALID_OFFSET_X;
	sel->r.top = SENSOR_VALID_OFFSET_Y;
	sel->r.width = SENSOR_OUTPUT_WIDTH;
	sel->r.height = SENSOR_OUTPUT_HEIGHT;

	return 0;
}

/*
 * XXXX_enum_bus -
 * enum bus
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @code:			return code of bus type
 */
static int h63_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(h63_codes))
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_YUYV8_2X8;

	return 0;
}

/*
 * XXXX_g_crop -
 * get crop
 * video functions
 *
 * @sd:				subdev
 * @a:				return crop
 */
static int h63_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left	= SENSOR_VALID_OFFSET_X;
	a->c.top	= SENSOR_VALID_OFFSET_Y;
	a->c.width	= SENSOR_OUTPUT_WIDTH;
	a->c.height	= SENSOR_OUTPUT_HEIGHT;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

/*
 * XXXX_cropcap -
 * get crop capbilities
 * video functions
 *
 * @sd:				subdev
 * @a:				return crop capbilities
 */
static int h63_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	h63_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
	a->bounds.left			= SENSOR_VALID_OFFSET_X;
	a->bounds.top			= SENSOR_VALID_OFFSET_Y;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	pr_err("%s bounds.width:%d, bounds.height:%d\n", __func__, a->bounds.width, a->bounds.height);

	return 0;
}

/*
 * XXXX_video_probe -
 * do some process in subdev probe
 * locale functions
 *
 * @sd:				subdev
 * @a:				return crop capbilities
 */
static int h63_video_probe(struct i2c_client *client)
{
	struct h63_priv *priv = to_h63(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops h63_ctrl_ops = {
	.g_volatile_ctrl	= h63_g_volatile_ctrl,
	.s_ctrl				= h63_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops h63_subdev_core_ops = {
	.s_power	= h63_core_s_power,
	.g_ctrl		= h63_core_g_ctrl,
	.s_ctrl		= h63_core_s_ctrl,
	.ioctl		= h63_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int h63_g_mbus_config(struct v4l2_subdev *sd,
		struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_MASTER |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = 0;

	return 0;
}

/*
 * XXXX_s_crop -
 * set crop
 * video functions
 *
 * @sd:				subdev
 * @crop:			crop to set
 */
static int h63_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	pr_err("%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops h63_subdev_video_ops = {
	.s_stream	= h63_s_stream,
	.cropcap	= h63_cropcap,
	.g_crop		= h63_g_crop,
	.g_mbus_config	= h63_g_mbus_config,
	.s_crop		= h63_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops h63_subdev_pad_ops = {
	.enum_mbus_code = h63_enum_mbus_code,
	.get_fmt	= h63_get_fmt,
	.set_fmt	= h63_set_fmt,
	.get_selection	= h63_get_selection,
};

/*sensor driver subdev ops*/
static struct v4l2_subdev_ops h63_subdev_ops = {
	.core	= &h63_subdev_core_ops,
	.video	= &h63_subdev_video_ops,
	.pad	= &h63_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct h63_priv *priv)
{
	struct device_node *np = client->dev.of_node;

	/*it is exist reset but lack of pwdn gpio in most case*/
	priv->gpio_reset = of_get_named_gpio(np, "reset-gpio", 0);
	priv->gpio_pwdn = of_get_named_gpio(np, "pwdn-gpio", 0);

	if (priv->gpio_reset >= 0) {
		devm_gpio_request(&client->dev, priv->gpio_reset, "sensor-reset");
	}

	if (priv->gpio_pwdn >= 0) {
		devm_gpio_request(&client->dev, priv->gpio_pwdn, "sensor-pwdn");
	}

	return 0;
}

/*
 * XXXX_match - check sensor id for match driver
 * @priv:			pointer to pirvate structure
 *
 * @RETURN:	0-match fail; 1-match success
 */
static int h63_match(struct h63_priv *priv)
{
	int pid;

	if (!check_id)
		return 1;

	priv->cb_info.cb->sensor_set_power_on_func(priv);
	pid = priv->cb_info.cb->sensor_probe_id_func(priv);
	if (pid <= 0) {
		pr_err("%s fail\n", __func__);
		return 0;
	}

	return 1;
}

/*
 * XXXX_probe -
 * driver probe after platform probe ok
 * i2c_driver functions
 *
 * @client:			pointor to i2c client
 * @did:			driver ids
 */
static int h63_probe(struct i2c_client *client,
		const struct i2c_device_id *did)
{
	struct h63_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
				"h63: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct h63_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&adapter->dev,
				"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}
	g_h63_priv = priv;

	/*parse of node*/
	sensor_of_parse(client, priv);

	/*
	 * the current sensor slave address to client.
	 * the address from dts may incorrect
	 * */
	client->addr = SENSOR_I2C_ADDR;
	priv->cb_info.cb = &h63_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!h63_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	/*subdev init*/
	v4l2_i2c_subdev_init(&priv->subdev, client, &h63_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &h63_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &h63_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);

	/*private ctrl*/
	priv->ctrl_sensor_get_id = v4l2_ctrl_new_custom(&priv->hdl, &config_sensor_get_id, NULL);
	if (!priv->ctrl_sensor_get_id)
		pr_err("creat ctrl_sensor_get_id fail\n");

	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error) {
		dev_err(&client->dev, "Hdl error\n");
		ret = priv->hdl.error;
		goto err_clk;
	}

	ret = h63_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "OV2640 probe fail\n");
		goto err_videoprobe;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "h63 Probed success, subdev:%p\n", &priv->subdev);

	/*ak platform need export some informations from sensor*/
	call_sensor_sys_init();

	return 0;

err_videoprobe:
	v4l2_ctrl_handler_free(&priv->hdl);
err_clk:
	return ret;
}

/*
 * XXXX_remove -
 * driver probe after platform remove ok
 * i2c_driver functions
 *
 * @client:			pointor to i2c client
 */
static int h63_remove(struct i2c_client *client)
{
	struct h63_priv       *priv = to_h63(client);

	pr_err("%s %d\n", __func__, __LINE__);

	if (!priv) {
		pr_err("%s had remove\n", __func__);
		return 0;
	}

	/*unregister async subdev*/
	v4l2_async_unregister_subdev(&priv->subdev);
	/*unregister subdev*/
	v4l2_device_unregister_subdev(&priv->subdev);
	/*unregister handler*/
	v4l2_ctrl_handler_free(&priv->hdl);
	/*unregister sys node*/
	call_sensor_sys_deinit();
	return 0;
}

static const struct i2c_device_id h63_id[] = {
	{ "h63", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, h63_id);

static const struct of_device_id h63_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, h63_of_match);

static struct i2c_driver h63_i2c_driver = {
	.driver = {
		.name = "h63",
		.of_match_table = of_match_ptr(h63_of_match),
	},
	.probe    = h63_probe,
	.remove   = h63_remove,
	.id_table = h63_id,
};

module_i2c_driver(h63_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for h63 sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.3");

/*sensor i2c write*/
static int h63_write(const struct i2c_client *client, int reg, int value)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = value,
	};

	return i2c_write(&trans);
}

/*sensor i2c read*/
static int h63_read(const struct i2c_client *client, int reg)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = 0,
	};

	return i2c_read(&trans);
}

/*
 * The sensor must set callbacks
 *
 * */
static int h63_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *npara)
{
	int i;
	AK_ISP_SENSOR_REG_INFO *preg_info;
	struct h63_priv *priv = arg;
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *para = &priv->para;

	h63_sensor_probe_id_func(arg);  //use IIC bus

	if (para->num <= 0 && npara)
		para = (void *)npara;

	preg_info = para->reg_info;
	for (i = 0; i < para->num; i++) {
#if 0
		{
			int value;

			value = h63_read(client, preg_info->reg_addr);
			pr_err("reg:%x read value:%x\n", preg_info->reg_addr, value);
		}
#endif
		if (preg_info->reg_addr == DELAY_FLAG)
			mdelay(preg_info->value);
		else
			h63_write(client, preg_info->reg_addr, preg_info->value);
#if 0
		{
			int value;

			value = h63_read(client, preg_info->reg_addr);
			pr_err("reg:%x write value:%x, read back value:%x\n",
					preg_info->reg_addr, preg_info->value, value);
		}
#endif

		preg_info++;
	}

	priv->fps_info.current_fps = MAX_FPS;
	priv->fps_info.to_fps = priv->fps_info.current_fps;
	priv->fps_info.to_fps_target_frame_height =
		(h63_read(client, 0x23) << 8) | h63_read(client, 0x22);
	priv->fps_info.current_target_frame_height = priv->fps_info.to_fps_target_frame_height;

	return 0;
}

/*read sensor register*/
static int h63_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct h63_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return h63_read(client, reg_addr);
}
/*write sensor register*/
static int h63_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct h63_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return h63_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int h63_sensor_read_id_func(void *arg)   //no use IIC bus
{
	if (!arg)
		arg = g_h63_priv;

	if (!SENSOR_ID)
		h63_sensor_probe_id_func(arg);  //use IIC bus

	return SENSOR_ID;
}

static unsigned int h63_cmos_gains_convert(unsigned int a_gain, unsigned int d_gain,
		unsigned int *a_gain_out ,unsigned int *d_gain_out)
{
	//ģ??????????ת???????????㷨??????????ת????sensor????????ʽ
	unsigned int ag_interget_table[17] = {0,  1, 2, 2, 4, 4, 4, 4, 8, 8, 8, 8,
		8, 8, 8, 8, 16};
	unsigned int ag_integer, ag_fraction, tmp;
	unsigned int ag_return;

	tmp =  a_gain / 16;
	if(tmp == 0)
	{
		tmp = 1;
	}
	//ak_sensor_print("tmp  = %d \n",tmp);
	if(tmp<16)
	{
		tmp =  OV_CLIP3(0, 16, tmp);
		ag_integer =  ag_interget_table[tmp];
	}
	else if ((16<=tmp)&&(tmp<32))
		ag_integer = 16;
	else if ((32<=tmp)&&(tmp<64))
		ag_integer = 32;
	else// if((64<=tmp)&&(tmp<128))
		ag_integer = 64;


	ag_fraction = (a_gain / ag_integer) - 16;
	ag_fraction =  OV_CLIP3(0, 15, ag_fraction);

	if (((ag_fraction + 16) * ag_integer) < a_gain)
	{
		if (ag_fraction < 15)
		{
			ag_fraction++;
		}
		// else if (ag_integer < 16)
		else if (ag_integer <128)
		{
			tmp++;
			/*
			   ag_integer  =  ag_interget_table[tmp];
			   ag_fraction = 0;
			   */
			if(tmp<16)
			{
				tmp =  OV_CLIP3(0, 16, tmp);
				ag_integer =  ag_interget_table[tmp];
			}
			else if ((16<=tmp)&&(tmp<32))
				ag_integer = 16;
			else if ((32<=tmp)&&(tmp<64))
				ag_integer = 32;
			else if((64<=tmp)&&(tmp<128))
				ag_integer = 64;

			ag_fraction = 0;
		}
		else
		{
		}
	}

	switch (ag_integer)
	{
		case 1 :
			ag_integer = 0x00;
			break;
		case 2 :
			ag_integer = 0x10;
			break;
		case 4 :
			ag_integer = 0x20;
			break;
		case 8 :
			ag_integer = 0x30;
			break;
		case 16 :
			ag_integer = 0x40;
			break;
		case 32 :
			ag_integer = 0x50;
			break;
		case 64 :
			ag_integer = 0x60;
			break;
		default: 
			ag_integer = 0x00;
			break;
	}

	ag_return = ag_integer|ag_fraction;
	*a_gain_out = ag_return ; 

	return ag_return;
}

/*set sensor again*/
static int h63_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	struct h63_priv *priv = arg;
	struct i2c_client *client = priv->client;

	//??????ģ???????Ļص?????
	unsigned short ag_value;
	unsigned int tmp_a_gain;
	unsigned int tmp_d_gain;
	unsigned int tmp_a_gain_out;
	unsigned int tmp_d_gain_out;

	tmp_a_gain = a_gain>>4;
	tmp_d_gain = 0;

	//ag_value = cmos_gains_update(isp,isp->aec_param.current_a_gain);
	h63_cmos_gains_convert(tmp_a_gain, tmp_d_gain,  &tmp_a_gain_out ,&tmp_d_gain_out);
	ag_value = tmp_a_gain_out;
	//ak_sensor_print("a_gain=%d\n",a_gain);
	h63_write(client, 0x00, tmp_a_gain_out);

#if 0
	printk("================33================\n\n");
	printk("tmp_a_gain : %d\n",tmp_a_gain);	
	printk("tmp_a_gain_out : %d\n",tmp_a_gain_out);
	printk("----------------------------------\n");
	printk("tmp_a_gain_out : 0x%x\n",tmp_a_gain_out);
	printk("==================================\n");
#endif
	return A_GAIN_EFFECT_FRAMES;
}

/*set sensor dgain*/
static int h63_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
	return D_GAIN_EFFECT_FRAMES;
}

static void __set_exp_ctrl (struct h63_priv *priv, int exp)
{
	struct i2c_client *client = priv->client;

	h63_write (client, 0x02, exp >> 8);
	h63_write (client, 0x01, exp & 0xff);

	priv->fps_info.current_exp = exp;
}

static void set_exp_ctrl(struct h63_priv *priv)
{
	int target_frame_height = priv->fps_info.to_fps_target_frame_height;
	int max_exp = target_frame_height - 1;
	int target_exp = priv->fps_info.target_exp;
	int current_exp = priv->fps_info.current_exp;

	if ((target_exp <= 0) || (max_exp <= 0))
		return;

	if (current_exp != target_exp) {
		if (target_exp <= max_exp)
			__set_exp_ctrl(priv, target_exp);
		else
			__set_exp_ctrl(priv, max_exp);
	}
}

static void update_frame_height_and_exp_ctrl(struct h63_priv *priv)
{
	struct i2c_client *client = priv->client;
	int target_frame_height = priv->fps_info.to_fps_target_frame_height;
	int current_target_frame_height = priv->fps_info.current_target_frame_height;
	int to_fps = priv->fps_info.to_fps;
	int current_fps = priv->fps_info.current_fps;

	if (to_fps <= current_fps) {
		/*decrease or equal fps*/
		h63_set_fps_async(priv);
		set_exp_ctrl(priv);
	} else {
		/*increase fps*/
		set_exp_ctrl(priv);
		h63_set_fps_async(priv);
	}
}

/*set sensor exp time*/
static int h63_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	struct h63_priv *priv = arg;
	struct i2c_client *client = priv->client;
	unsigned char exposure_time_msb;
	unsigned char exposure_time_lsb;

	priv->fps_info.target_exp = exp_time;

	pr_debug("h63 exp:%d\n", exp_time);

	update_frame_height_and_exp_ctrl(priv);

	return EXP_EFFECT_FRAMES;
}
/*sensor timer*/
static int h63_sensor_timer_func(void *arg)
{
	struct h63_priv *priv = arg;
	update_frame_height_and_exp_ctrl(priv);
	return 0;
}
/*standby in*/
static int h63_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int h63_sensor_set_standby_out_func(void *arg)
{
	return 0;
}
/*low level read sensor ID, user i2c ops*/
static int h63_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct h63_priv *priv = arg;
	struct i2c_client *client = priv->client;
	int id;
	int value;

    value = h63_read(client, 0x0a);
    id = value << 8;
    value = h63_read(client, 0x0b);
    id |= value;    

	if (id == SENSOR_ID)
		return SENSOR_ID;

    return 0;

	return 0;
}
/*get resolution*/
static int h63_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int h63_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int h63_sensor_get_fps_func(void *arg)
{
	struct h63_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int h63_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type h63_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}
/*get self definded params*/
static int h63_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct h63_priv *priv = arg;

	switch (param) {
		case GET_INTERFACE:
			*((int *)value) = SENSOR_IO_INTERFACE;
			break;

		case GET_IO_LEVEL:
			*((int *)value) = SENSOR_IO_LEVEL;
			break;

		case GET_MIPI_MHZ:
			*((int *)value) = MIPI_MBPS;
			break;

		case GET_MIPI_LANES:
			*((int *)value) = MIPI_LANES;
			break;

		case GET_RESET_GPIO:
			*((int *)value) = priv->gpio_reset;
			break;

		case GET_PWDN_GPIO:
			*((int *)value) = priv->gpio_pwdn;
			break;

		default:
			pr_err("%s param:%d not support\n", __func__, param);
			ret = -1;
			break;
	}

	return ret;
}
/*sensor power on*/
static int h63_sensor_set_power_on_func(void *arg)
{
	struct h63_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_output(priv->gpio_pwdn, !SENSOR_PWDN_LEVEL);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_output(priv->gpio_reset, !SENSOR_RESET_LEVEL);
		msleep(50);
		gpio_direction_output(priv->gpio_reset, SENSOR_RESET_LEVEL);
		msleep(50);
		gpio_direction_output(priv->gpio_reset, !SENSOR_RESET_LEVEL);
		msleep(100);
	}

	return 0;
}
/*sensor power off*/
static int h63_sensor_set_power_off_func(void *arg)
{
	struct h63_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}

/**
 * 从寄存器读出是半帧的宽度。
 */
static int __get_frame_half_width(struct h63_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (0 == priv->fps_info.frame_width) {
		/// 这里重复读取 I2C 会导致 I2C 超时，
		/// 通过缓冲见

		int const reg_frame_w_15_8 = h63_read (client, 0x21);
		int const reg_frame_w_7_0 = h63_read (client, 0x20);
		priv->fps_info.frame_width =  (reg_frame_w_15_8 << 8) | reg_frame_w_7_0;
	}

	//	pr_err("Get Frame Width = %d\r\n", priv->fps_info.frame_width);
	return priv->fps_info.frame_width;
}

/*set sensor fps*/
static int h63_sensor_set_fps_func(void *arg, const int fps)
{
	struct h63_priv *priv = arg;
	int target_frame_height;
	int tmp;

	/// FPS = PCLK / FRAMEW / FRAMEH
	/// 计算出将要设置的帧长度。
	switch (fps) {
		case 12:
			tmp = 1250;
			break;

		case 14:
			tmp = 1428;
			break;

		default:
			tmp = fps * 100;
			break;
	}

	target_frame_height = PCLK_FREQ * 10 / __get_frame_width(priv);
	target_frame_height *= 10;
	target_frame_height /= tmp;

	priv->fps_info.to_fps_target_frame_height = target_frame_height;
	priv->fps_info.to_fps = fps;

	//update_frame_height_and_exp_ctrl ();

	return 0;
}

static void h63_set_fps_async(struct h63_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (priv->fps_info.to_fps != priv->fps_info.current_fps) {
		int value = priv->fps_info.to_fps_target_frame_height;

		h63_write(client, 0x23, (value >> 8) & 0xff);
		h63_write(client, 0x22, value & 0xff);
		priv->fps_info.current_fps = priv->fps_info.to_fps;
		priv->fps_info.current_target_frame_height = value;
	}
}

static AK_ISP_SENSOR_CB h63_cb = {
	.sensor_init_func = h63_sensor_init_func,
	.sensor_read_reg_func = h63_sensor_read_reg_func,
	.sensor_write_reg_func = h63_sensor_write_reg_func,
	.sensor_read_id_func = h63_sensor_read_id_func,
	.sensor_update_a_gain_func = h63_sensor_update_a_gain_func,
	.sensor_update_d_gain_func = h63_sensor_update_d_gain_func,
	.sensor_updata_exp_time_func = h63_sensor_updata_exp_time_func,
	.sensor_timer_func = h63_sensor_timer_func,
	.sensor_set_standby_in_func = h63_sensor_set_standby_in_func,
	.sensor_set_standby_out_func = h63_sensor_set_standby_out_func,
	.sensor_probe_id_func = h63_sensor_probe_id_func,
	.sensor_get_resolution_func = h63_sensor_get_resolution_func,
	.sensor_get_mclk_func = h63_sensor_get_mclk_func,
	.sensor_get_fps_func = h63_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func = h63_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func = h63_sensor_get_bus_type_func,
	.sensor_get_parameter_func = h63_sensor_get_parameter_func,
	.sensor_set_power_on_func = h63_sensor_set_power_on_func,
	.sensor_set_power_off_func = h63_sensor_set_power_off_func,
	.sensor_set_fps_func = h63_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return h63_sensor_read_id_func(NULL);
}

static char *sensor_if_func(void)
{
	static char ifstr[16] = "dvp";

	if (SENSOR_IO_INTERFACE == MIPI_INTERFACE)
		sprintf(ifstr, "mipi%d", MIPI_LANES);

	return ifstr;
}

static int call_sensor_sys_init(void)
{
	return sensor_sys_init(sensor_id_func, sensor_if_func);
}

static int call_sensor_sys_deinit(void)
{
	sensor_sys_exit();
	return 0;
}
