/*
 * gc4653 Camera Driver
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
#define SENSOR_I2C_ADDR			0x29
#define SENSOR_ID				0x4653
#define SENSOR_MCLK				24
#define SENSOR_REGADDR_BYTE		2
#define SENSOR_DATA_BYTE		1
#define SENSOR_OUTPUT_WIDTH		2560
#define SENSOR_OUTPUT_HEIGHT	1440
#define SENSOR_VALID_OFFSET_X	0
#define SENSOR_VALID_OFFSET_Y	0
#define SENSOR_BUS_TYPE			BUS_TYPE_RAW
//#define SENSOR_IO_INTERFACE		DVP_INTERFACE
#define SENSOR_IO_INTERFACE		MIPI_INTERFACE
#define SENSOR_IO_LEVEL			IO_LEVEL_1V8
/*
 * gc4653 at 15fps, file from IC fae said:
 * 		mipiclk 324Mhz,
 * but it is not stable in the frequency.
 * so increase to 620Mhzbps
 */
#define MIPI_MBPS				648
#define MIPI_LANES				2
static int MAX_FPS=				15;
#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	0	/*adjust a_gain for every frames*/
#define D_GAIN_EFFECT_FRAMES	1	/*adjust d_gain for 1/2 frames*/

#define DELAY_FLAG		(0xffff)

/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct gc4653_win_size {
	char				*name;
	u32				width;
	u32				height;
	const struct regval_list	*regs;
};

/*
 * store sensor fps informations
 * @current_fps: current fps
 * @to_fps: should changed to the fps
 * @to_fps_value: the fps paramter
 */
struct sensor_fps_info {
	int current_fps;
	int to_fps;
	int to_fps_value;
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
struct gc4653_aec_parms {
	int curr_again_level;
	int curr_again_10x;
	int r0x3e02_value;
	int r0x3e01_value;
	int r0x3e00_value;
	int curr_2x_dgain;
	int curr_corse_gain;
};

static const struct v4l2_ctrl_ops gc4653_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &gc4653_ctrl_ops,
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
struct gc4653_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct gc4653_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct gc4653_aec_parms aec_parms;

	AK_ISP_SENSOR_INIT_PARA para;

	struct v4l2_ctrl *ctrl_sensor_get_id;
};

/*default aec of fast boot*/
static struct ae_fast_struct ae_fast_default = {
	.sensor_exp_time = 896,
	.sensor_a_gain	= 256,
	.sensor_d_gain	= 256,
	.isp_d_gain		= 256,
	{
		.r_gain = 590,
		.g_gain = 256,
		.b_gain = 515,
		.r_offset = 0,
		.g_offset = 0,
		.b_offset = 0
	}
};

static AK_ISP_SENSOR_CB gc4653_cb;

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 0;
module_param(check_id, int, 0644);

static void gc4653_set_fps_async(struct gc4653_priv *priv);
static int __gc4653_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int gc4653_sensor_read_id_func(void *arg);   //no use IIC bus
static int gc4653_sensor_get_resolution_func(void *arg, int *width, int *height);
static int gc4653_sensor_set_fps_func(void *arg, const int fps);
static int gc4653_fps_to_vts(struct gc4653_priv *priv, const int fps);
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);
static int gc4653_sensor_probe_id_func(void *arg);  //use IIC bus

static u32 gc4653_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct gc4653_priv *to_gc4653(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct gc4653_priv,
			    subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct gc4653_priv, hdl)->subdev;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int gc4653_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int gc4653_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
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
static int gc4653_s_ctrl(struct v4l2_ctrl *ctrl)
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
static int gc4653_core_s_power(struct v4l2_subdev *sd, int on)
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
static int gc4653_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = gc4653_sensor_read_id_func(NULL);   //no use IIC bus
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
static int gc4653_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc4653_priv *priv = to_gc4653(client);

	ctrl->value = (int)&priv->cb_info;
	return 0;
}

static int gc4653_get_max_exp_for_fps(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc4653_priv *priv = to_gc4653(client);
	int fps = ctrl->value;
	int vts = gc4653_fps_to_vts(priv, fps);

	ctrl->value = vts;
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
static int gc4653_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = gc4653_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = gc4653_get_sensor_cb(sd, ctrl);
			break;

		case GET_MAX_EXP_FOR_FPS:
			ret = gc4653_get_max_exp_for_fps(sd, ctrl);
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
static int gc4653_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc4653_priv *priv = to_gc4653(client);
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
static int gc4653_set_fps_direct(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc4653_priv *priv = to_gc4653(client);
	int fps = ctrl->value;

	gc4653_sensor_set_fps_func(priv, fps);
	gc4653_set_fps_async(priv);
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
static int gc4653_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = gc4653_set_isp_timing_cb(sd, ctrl);
			break;

		case SET_FPS_DIRECT:
			ret = gc4653_set_fps_direct(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static int gc4653_store_initial_regs(struct gc4653_priv *priv, void *arg)
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

static long gc4653_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc4653_priv *priv = to_gc4653(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			gc4653_store_initial_regs(priv, arg);
			break;

		case AK_SENSOR_GET_MAX_EXP_FOR_FPS: {
			struct sensor_max_exp_for_fps *exp_for_fps = arg;
			struct v4l2_control ctrl;

			ctrl.value = exp_for_fps->fps;
			ret = gc4653_get_max_exp_for_fps(sd, &ctrl);
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
static int gc4653_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct gc4653_priv *priv = to_gc4653(client);
	static struct gc4653_win_size win = {
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
static int gc4653_set_fmt(struct v4l2_subdev *sd,
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
static int gc4653_get_selection(struct v4l2_subdev *sd,
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
static int gc4653_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(gc4653_codes))
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
static int gc4653_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
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
static int gc4653_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	gc4653_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
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
static int gc4653_video_probe(struct i2c_client *client)
{
	struct gc4653_priv *priv = to_gc4653(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops gc4653_ctrl_ops = {
	.g_volatile_ctrl	= gc4653_g_volatile_ctrl,
	.s_ctrl				= gc4653_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops gc4653_subdev_core_ops = {
	.s_power	= gc4653_core_s_power,
	.g_ctrl		= gc4653_core_g_ctrl,
	.s_ctrl		= gc4653_core_s_ctrl,
	.ioctl		= gc4653_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int gc4653_g_mbus_config(struct v4l2_subdev *sd,
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
static int gc4653_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	printk(KERN_ERR "%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops gc4653_subdev_video_ops = {
	.s_stream	= gc4653_s_stream,
	.cropcap	= gc4653_cropcap,
	.g_crop		= gc4653_g_crop,
	.g_mbus_config	= gc4653_g_mbus_config,
	.s_crop		= gc4653_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops gc4653_subdev_pad_ops = {
	.enum_mbus_code = gc4653_enum_mbus_code,
	.get_fmt	= gc4653_get_fmt,
	.set_fmt	= gc4653_set_fmt,
	.get_selection	= gc4653_get_selection,
};

/*sensor driver subdev ops*/
static struct v4l2_subdev_ops gc4653_subdev_ops = {
	.core	= &gc4653_subdev_core_ops,
	.video	= &gc4653_subdev_video_ops,
	.pad	= &gc4653_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct gc4653_priv *priv)
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
static int gc4653_match(struct gc4653_priv *priv)
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
static int gc4653_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct gc4653_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"gc4653: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct gc4653_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&adapter->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	/*parse of node*/
	sensor_of_parse(client, priv);

	/*
	 * the current sensor slave address to client.
	 * the address from dts may incorrect
	 * */
	client->addr = SENSOR_I2C_ADDR;
	priv->cb_info.cb = &gc4653_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!gc4653_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	/*subdev init*/
	v4l2_i2c_subdev_init(&priv->subdev, client, &gc4653_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &gc4653_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &gc4653_ctrl_ops,
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

	ret = gc4653_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "OV2640 probe fail\n");
		goto err_videoprobe;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "gc4653 Probed success, subdev:%p\n", &priv->subdev);

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
static int gc4653_remove(struct i2c_client *client)
{
	struct gc4653_priv       *priv = to_gc4653(client);

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

static const struct i2c_device_id gc4653_id[] = {
	{ "gc4653", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc4653_id);

static const struct of_device_id gc4653_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, gc4653_of_match);

static struct i2c_driver gc4653_i2c_driver = {
	.driver = {
		.name = "gc4653",
		.of_match_table = of_match_ptr(gc4653_of_match),
	},
	.probe    = gc4653_probe,
	.remove   = gc4653_remove,
	.id_table = gc4653_id,
};

module_i2c_driver(gc4653_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for gc4653 sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.3");

/*sensor i2c write*/
static int gc4653_write(const struct i2c_client *client, int reg, int value)
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
static int gc4653_read(const struct i2c_client *client, int reg)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = 0,
	};

	pr_err("%s client:%p, addr:%x\n", __func__, client, client->addr);
	return i2c_read(&trans);
}

/*
 * The sensor must set callbacks
 *
 * */
static int gc4653_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *npara)
{
	int i;
	int value;
	AK_ISP_SENSOR_REG_INFO *preg_info;
	struct gc4653_priv *priv = arg;
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *para = &priv->para;

	gc4653_sensor_probe_id_func(arg);  //use IIC bus

	if (para->num <= 0 && npara)
		para = (void *)npara;

	preg_info = para->reg_info;
	for (i = 0; i < para->num; i++) {
#if 0
		{
			int value;

			value = gc4653_read(client, preg_info->reg_addr);
			pr_err("reg:%x read value:%x\n", preg_info->reg_addr, value);
		}
#endif
		gc4653_write(client, preg_info->reg_addr, preg_info->value);
#if 0
		{
			int value;

			value = gc4653_read(client, preg_info->reg_addr);
			pr_err("reg:%x write value:%x, read back value:%x\n",
					preg_info->reg_addr, preg_info->value, value);
		}
#endif

		preg_info++;
	}

	value = gc4653_read(client, 0x326);
	switch (value) {
		case 0x36:
				MAX_FPS = 15;
				pr_err("R0x326:0x%x \n", value);
				break;

		case 0x48:
				pr_err("R0x326:0x%x \n", value);
				MAX_FPS = 20;
				break;

		default:
				MAX_FPS = 15;
				pr_err("R0x326:0x%x is unknow\n", value);
				break;
	}

	priv->fps_info.current_fps = MAX_FPS;
	priv->fps_info.to_fps = priv->fps_info.current_fps;
	priv->fps_info.to_fps_value = 0;

	return 0;
}

/*read sensor register*/
static int gc4653_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct gc4653_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return gc4653_read(client, reg_addr);
}
/*write sensor register*/
static int gc4653_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct gc4653_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return gc4653_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int gc4653_sensor_read_id_func(void *arg)   //no use IIC bus
{
	return SENSOR_ID;
}

/*
 * from gc fae
 * */
static unsigned int regValTable[26][5] =
{
	/*02b3, 0519, 02d9, 02b8, 02b9*/
	{0x00, 0x04, 0x42, 0x01, 0x00},
	{0x20, 0x05, 0x43, 0x01, 0x0B},
	{0x01, 0x04, 0x42, 0x01, 0x19},
	{0x21, 0x05, 0x43, 0x01, 0x2a},
	{0x02, 0x04, 0x42, 0x02, 0x00},
	{0x22, 0x05, 0x43, 0x02, 0x17},
	{0x03, 0x05, 0x43, 0x02, 0x33},
	{0x23, 0x06, 0x44, 0x03, 0x14},
	{0x04, 0x06, 0x44, 0x04, 0x00},
	{0x24, 0x08, 0x46, 0x04, 0x2f},
	{0x05, 0x08, 0x46, 0x05, 0x26},
	{0x25, 0x0a, 0x48, 0x06, 0x28},
	{0x06, 0x0c, 0x4a, 0x08, 0x00},
	{0x26, 0x0d, 0x4b, 0x09, 0x1E},
	{0x46, 0x0f, 0x4d, 0x0B, 0x0C},
	{0x66, 0x11, 0x4f, 0x0D, 0x11},
	{0x0e, 0x13, 0x51, 0x10, 0x00},
	{0x2e, 0x16, 0x54, 0x12, 0x3D},
	{0x4e, 0x19, 0x57, 0x16, 0x19},
	{0x6e, 0x1b, 0x59, 0x1A, 0x22},
	{0x1e, 0x1e, 0x5c, 0x20, 0x00},
	{0x3e, 0x21, 0x5f, 0x25, 0x3A},
	{0x5e, 0x25, 0x63, 0x2C, 0x33},
	{0x7e, 0x29, 0x67, 0x35, 0x05},
	{0x9e, 0x2d, 0x6b, 0x40, 0x00},
	{0xbe, 0x31, 0x6f, 0x4B, 0x35},
};

static unsigned int analog_gain_table[26] =
{
	64,
	75,
	89,
	106,
	128,
	151,
	179,
	212,
	256,
	303,
	358,
	424,
	512,
	606,
	716,
	849,
	1024,
	1213,
	1433,
	1698,
	2048,
	2426,
	2867,
	3397,
	4096,
	4853,
};

/*************************************************************************
 * * FUNCTION
 * *    set_gain
 * *
 * * DESCRIPTION
 * *    This function is to set global gain to sensor.
 * *
 * * PARAMETERS
 * *    iGain : sensor global gain(base: 0x40)
 * *
 * * RETURNS
 * *    the actually gain set to sensor.
 * *
 * * GLOBALS AFFECTED
 * *
 * *************************************************************************/
static int SetSensor_Gain(struct i2c_client *client, unsigned int gain)
{
	unsigned char i;
	unsigned char total;
	unsigned int tol_dig_gain = 0;
	unsigned int tmp_gain = gain / 4;

	total = sizeof(analog_gain_table) / sizeof(analog_gain_table[0]);
	for(i = 0; i < total-1; i++)
	{
		if((analog_gain_table[i] <= tmp_gain)&&(tmp_gain < analog_gain_table[i+1]))
			break;
	}

	tol_dig_gain = tmp_gain*64/analog_gain_table[i];

	/*
	reg_sc_wr(0x2b3,regValTable[i][0]);
	reg_sc_wr(0x519,regValTable[i][1]);
	reg_sc_wr(0x2d9,regValTable[i][2]);
	reg_sc_wr(0x2b8,regValTable[i][3]);
	reg_sc_wr(0x2b9,regValTable[i][4]);

	reg_sc_wr(0x20e,(tol_dig_gain>>6));
	reg_sc_wr(0x20f,((tol_dig_gain&0x3f)<<2));
	*/

	gc4653_write(client,0x2b3,regValTable[i][0]);
	gc4653_write(client,0x519,regValTable[i][1]);
	gc4653_write(client,0x2d9,regValTable[i][2]);
	gc4653_write(client,0x2b8,regValTable[i][3]);
	gc4653_write(client,0x2b9,regValTable[i][4]);

	gc4653_write(client,0x20e,(tol_dig_gain>>6));
	gc4653_write(client,0x20f,((tol_dig_gain&0x3f)<<2));

	return 0;
}

/*************************************************************************
 * * FUNCTION
 * *    set_shutter
 * *
 * * DESCRIPTION
 * *    This function set e-shutter of sensor to change exposure time.
 * *
 * * PARAMETERS
 * *    iShutter : exposured lines
 * *
 * * RETURNS
 * *    None
 * *
 * * GLOBALS AFFECTED
 * *
 * *************************************************************************/
static int SetSensor_shutter(struct i2c_client *client, unsigned int intt)
{
	/*
	reg_sc_wr(0x202,(intt>>8));
	reg_sc_wr(0x203,(intt&0xff));
	*/

	gc4653_write(client,0x202,(intt>>8));
	gc4653_write(client,0x203,(intt&0xff));

	return 0;
}

/*set sensor again*/
/*max gain: 4853*/
static int gc4653_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	struct gc4653_priv *priv = arg;
	struct i2c_client *client = priv->client;

	SetSensor_Gain(client, a_gain);

	return A_GAIN_EFFECT_FRAMES;
}
/*set sensor dgain*/
static int gc4653_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
	return D_GAIN_EFFECT_FRAMES;
}
/*set sensor exp time*/
/*max exp: 0x3fff*/
static int gc4653_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	struct gc4653_priv *priv = arg;
	struct i2c_client *client = priv->client;

	/*
	 * max_exp_time = frame_length = R{0x340,0x341}
	 * */
	SetSensor_shutter(client, exp_time);

	return EXP_EFFECT_FRAMES;
}
/*sensor timer*/
static int gc4653_sensor_timer_func(void *arg)
{
	return 0;
}
/*standby in*/
static int gc4653_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int gc4653_sensor_set_standby_out_func(void *arg)
{
	return 0;
}
/*low level read sensor ID, user i2c ops*/
static int gc4653_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct gc4653_priv *priv = arg;
	struct i2c_client *client = priv->client;
	int id;
	int value;

	value = gc4653_read(client, 0x03f0);
	id = value << 8;

	value = gc4653_read(client, 0x03f1);
	id |= value;

	printk(KERN_ERR "id:%x\n", id);

	if (id == SENSOR_ID)
		return SENSOR_ID;

	return 0;
}
/*get resolution*/
static int gc4653_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int gc4653_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int gc4653_sensor_get_fps_func(void *arg)
{
	struct gc4653_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int gc4653_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type gc4653_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}
static int get_ae_fast_default(struct ae_fast_struct *ae_fast)
{
	*ae_fast = ae_fast_default;
	return 0;
}

/*get self definded params*/
static int gc4653_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct gc4653_priv *priv = arg;

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

		case GET_AE_FAST_DEFAULT:
			get_ae_fast_default(value);
			break;

		default:
			pr_err("%s param:%d not support\n", __func__, param);
			ret = -1;
			break;
	}

	return ret;
}
/*sensor power on*/
static int gc4653_sensor_set_power_on_func(void *arg)
{
	struct gc4653_priv *priv = arg;

	pr_err("%s %d\n",__func__,__LINE__);

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_output(priv->gpio_pwdn, !SENSOR_PWDN_LEVEL);
	}

	if (priv->gpio_reset >= 0) {
	pr_err("%s %d\n",__func__,__LINE__);
		gpio_direction_output(priv->gpio_reset, !SENSOR_RESET_LEVEL);
		msleep(50);
		gpio_direction_output(priv->gpio_reset, SENSOR_RESET_LEVEL);
		msleep(50);
		gpio_direction_output(priv->gpio_reset, !SENSOR_RESET_LEVEL);
		msleep(100);
	}

	//aec_parms_init(priv);

	return 0;
}
/*sensor power off*/
static int gc4653_sensor_set_power_off_func(void *arg)
{
	struct gc4653_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}

static int gc4653_fps_to_vts(struct gc4653_priv *priv, const int fps)
{
	/*
	 * max_exp_time = frame_length = R{0x340,0x341}
	 * */
	/*R{0x340,0x341}=0x5dc=1500 at 15FPS*/

	int vts;

	pr_debug("%s\n", __func__);

	if (fps > MAX_FPS) {
		pr_err("%s MAX_FPS:%d, so set to %d fps fail\n", __func__, MAX_FPS, fps);
		return -EINVAL;
	}

	switch (fps) {
		case 12:
			vts = 1250;
			break;

		case 14:
			vts = 1428;
			break;

		default:
			vts = fps * 100;
			break;
	}

	vts = 0x5dcUL * MAX_FPS * 100 / vts;

	return vts;
}

/*set sensor fps*/
static int gc4653_sensor_set_fps_func(void *arg, const int fps)
{
	int tmp;
	struct gc4653_priv *priv = arg;

	tmp = gc4653_fps_to_vts(priv, fps);

	if (tmp > 0) {
		priv->fps_info.to_fps_value = tmp;
		priv->fps_info.to_fps = fps;
	} else {
		return -EINVAL;
	}

	return 0;
}

static void gc4653_set_fps_async(struct gc4653_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (priv->fps_info.to_fps != priv->fps_info.current_fps) {
		int value = priv->fps_info.to_fps_value;

		gc4653_write(client, 0x340, (value >> 8) & 0xff);
		gc4653_write(client, 0x341, value & 0xff);
		priv->fps_info.current_fps = priv->fps_info.to_fps;
	}
}

static AK_ISP_SENSOR_CB gc4653_cb = {
	.sensor_init_func = gc4653_sensor_init_func,
	.sensor_read_reg_func = gc4653_sensor_read_reg_func,
	.sensor_write_reg_func = gc4653_sensor_write_reg_func,
	.sensor_read_id_func = gc4653_sensor_read_id_func,
	.sensor_update_a_gain_func = gc4653_sensor_update_a_gain_func,
	.sensor_update_d_gain_func = gc4653_sensor_update_d_gain_func,
	.sensor_updata_exp_time_func = gc4653_sensor_updata_exp_time_func,
	.sensor_timer_func = gc4653_sensor_timer_func,
	.sensor_set_standby_in_func = gc4653_sensor_set_standby_in_func,
	.sensor_set_standby_out_func = gc4653_sensor_set_standby_out_func,
	.sensor_probe_id_func = gc4653_sensor_probe_id_func,
	.sensor_get_resolution_func = gc4653_sensor_get_resolution_func,
	.sensor_get_mclk_func = gc4653_sensor_get_mclk_func,
	.sensor_get_fps_func = gc4653_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func = gc4653_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func = gc4653_sensor_get_bus_type_func,
	.sensor_get_parameter_func = gc4653_sensor_get_parameter_func,
	.sensor_set_power_on_func = gc4653_sensor_set_power_on_func,
	.sensor_set_power_off_func = gc4653_sensor_set_power_off_func,
	.sensor_set_fps_func = gc4653_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return gc4653_sensor_read_id_func(NULL);
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
