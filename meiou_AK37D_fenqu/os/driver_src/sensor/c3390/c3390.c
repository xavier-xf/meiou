/*
 * c3390 Camera Driver
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
#define SENSOR_PWDN_LEVEL		1
#define SENSOR_RESET_LEVEL		0
#define SENSOR_I2C_ADDR			0x36
#define ACTUAL_SENSOR_ID		0x301
#define SENSOR_ID				0x3390
#define SENSOR_MCLK				24
#define SENSOR_REGADDR_BYTE		2
#define SENSOR_DATA_BYTE		1
#define SENSOR_OUTPUT_WIDTH		2304
#define SENSOR_OUTPUT_HEIGHT	1296
#define SENSOR_VALID_OFFSET_X	0
#define SENSOR_VALID_OFFSET_Y	0
#define SENSOR_BUS_TYPE			BUS_TYPE_RAW
//#define SENSOR_IO_INTERFACE		DVP_INTERFACE
#define SENSOR_IO_INTERFACE		MIPI_INTERFACE
#define SENSOR_IO_LEVEL			IO_LEVEL_1V8
#define MIPI_MHZ				568
#define MIPI_LANES				2
#define MAX_FPS					15
#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	1	/*adjust a_gain for every frames*/
#define D_GAIN_EFFECT_FRAMES	1	/*adjust d_gain for every frames*/

#define DELAY_FLAG		(0xffff)

/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct c3390_win_size {
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
struct c3390_aec_parms {
	int curr_again_level;
	int curr_again_10x;
	int r0x0202_value;
	int r0x0203_value;
	int target_exp_ctrl;
	int default_hts;
	int calc_vts_tmp;
};

static const struct v4l2_ctrl_ops c3390_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &c3390_ctrl_ops,
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
struct c3390_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct c3390_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct c3390_aec_parms aec_parms;

	AK_ISP_SENSOR_INIT_PARA para;

	struct v4l2_ctrl *ctrl_sensor_get_id;
};

/*default aec of fast boot*/
static struct ae_fast_struct ae_fast_default = {
	.sensor_exp_time = 2152,
	.sensor_a_gain	= 256,
	.sensor_d_gain	= 256,
	.isp_d_gain		= 256,
	{
		.r_gain = 445,
		.g_gain = 256,
		.b_gain = 396,
		.r_offset = 0,
		.g_offset = 0,
		.b_offset = 0
	}
};

static AK_ISP_SENSOR_CB c3390_cb;

static int dvp = 0;

module_param(dvp, int, 0644);

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 0;
module_param(check_id, int, 0644);

static void c3390_set_fps_async(struct c3390_priv *priv);
static int __c3390_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int c3390_sensor_read_id_func(void *arg);   //no use IIC bus
static int c3390_sensor_get_resolution_func(void *arg, int *width, int *height);
static int c3390_sensor_set_fps_func(void *arg, const int fps);
static int c3390_fps_to_vts(struct c3390_priv *priv, const int fps);
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);

static u32 c3390_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct c3390_priv *to_c3390(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct c3390_priv,
			    subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct c3390_priv, hdl)->subdev;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int c3390_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int c3390_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
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
static int c3390_s_ctrl(struct v4l2_ctrl *ctrl)
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
static int c3390_core_s_power(struct v4l2_subdev *sd, int on)
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
static int c3390_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = c3390_sensor_read_id_func(NULL);   //no use IIC bus
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
static int c3390_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct c3390_priv *priv = to_c3390(client);

	ctrl->value = (int)&priv->cb_info;
	return 0;
}

static int c3390_get_max_exp_for_fps(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct c3390_priv *priv = to_c3390(client);
	int fps = ctrl->value;
	int vts = c3390_fps_to_vts(priv, fps);

	ctrl->value = vts * 2 - 10;
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
static int c3390_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = c3390_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = c3390_get_sensor_cb(sd, ctrl);
			break;

		case GET_MAX_EXP_FOR_FPS:
			ret = c3390_get_max_exp_for_fps(sd, ctrl);
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
static int c3390_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct c3390_priv *priv = to_c3390(client);
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
static int c3390_set_fps_direct(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct c3390_priv *priv = to_c3390(client);
	int fps = ctrl->value;

	c3390_sensor_set_fps_func(priv, fps);
	c3390_set_fps_async(priv);
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
static int c3390_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = c3390_set_isp_timing_cb(sd, ctrl);
			break;

		case SET_FPS_DIRECT:
			ret = c3390_set_fps_direct(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static int c3390_store_initial_regs(struct c3390_priv *priv, void *arg)
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

static long c3390_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct c3390_priv *priv = to_c3390(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			c3390_store_initial_regs(priv, arg);
			break;

		case AK_SENSOR_GET_MAX_EXP_FOR_FPS: {
			struct sensor_max_exp_for_fps *exp_for_fps = arg;
			struct v4l2_control ctrl;

			ctrl.value = exp_for_fps->fps;
			ret = c3390_get_max_exp_for_fps(sd, &ctrl);
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
static int c3390_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct c3390_priv *priv = to_c3390(client);
	static struct c3390_win_size win = {
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
static int c3390_set_fmt(struct v4l2_subdev *sd,
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
static int c3390_get_selection(struct v4l2_subdev *sd,
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
static int c3390_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(c3390_codes))
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
static int c3390_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
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
static int c3390_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	c3390_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
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
static int c3390_video_probe(struct i2c_client *client)
{
	struct c3390_priv *priv = to_c3390(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops c3390_ctrl_ops = {
	.g_volatile_ctrl	= c3390_g_volatile_ctrl,
	.s_ctrl				= c3390_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops c3390_subdev_core_ops = {
	.s_power	= c3390_core_s_power,
	.g_ctrl		= c3390_core_g_ctrl,
	.s_ctrl		= c3390_core_s_ctrl,
	.ioctl		= c3390_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int c3390_g_mbus_config(struct v4l2_subdev *sd,
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
static int c3390_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	printk(KERN_ERR "%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops c3390_subdev_video_ops = {
	.s_stream	= c3390_s_stream,
	.cropcap	= c3390_cropcap,
	.g_crop		= c3390_g_crop,
	.g_mbus_config	= c3390_g_mbus_config,
	.s_crop		= c3390_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops c3390_subdev_pad_ops = {
	.enum_mbus_code = c3390_enum_mbus_code,
	.get_fmt	= c3390_get_fmt,
	.set_fmt	= c3390_set_fmt,
	.get_selection	= c3390_get_selection,
};

/*sensor driver subdev ops*/
static struct v4l2_subdev_ops c3390_subdev_ops = {
	.core	= &c3390_subdev_core_ops,
	.video	= &c3390_subdev_video_ops,
	.pad	= &c3390_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct c3390_priv *priv)
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
static int c3390_match(struct c3390_priv *priv)
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
static int c3390_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct c3390_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"c3390: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct c3390_priv), GFP_KERNEL);
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
	priv->cb_info.cb = &c3390_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!c3390_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	/*subdev init*/
	v4l2_i2c_subdev_init(&priv->subdev, client, &c3390_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &c3390_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &c3390_ctrl_ops,
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

	ret = c3390_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "c3390 probe fail\n");
		goto err_videoprobe;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "c3390 Probed success, subdev:%p\n", &priv->subdev);

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
static int c3390_remove(struct i2c_client *client)
{
	struct c3390_priv       *priv = to_c3390(client);

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

static const struct i2c_device_id c3390_id[] = {
	{ "c3390", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, c3390_id);

static const struct of_device_id c3390_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, c3390_of_match);

static struct i2c_driver c3390_i2c_driver = {
	.driver = {
		.name = "c3390",
		.of_match_table = of_match_ptr(c3390_of_match),
	},
	.probe    = c3390_probe,
	.remove   = c3390_remove,
	.id_table = c3390_id,
};

module_i2c_driver(c3390_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for c3390 sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.3");

/*sensor i2c write*/
static int c3390_write(const struct i2c_client *client, int reg, int value)
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
static int c3390_read(const struct i2c_client *client, int reg)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = 0,
	};

	pr_debug("%s client:%p, addr:%x\n", __func__, client, client->addr);
	return i2c_read(&trans);
}

static void c3390_agc_mode(struct c3390_priv *priv)
{
	int value;
	struct i2c_client *client = priv->client;

	value = c3390_read(client, 0x3e03) & 0xf0;
	value |= 0xb;
	c3390_write(client, 0x3e03, value);
}

/*
 * The sensor must set callbacks
 *
 * */
static int c3390_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *npara)
{
	int i;
	AK_ISP_SENSOR_REG_INFO *preg_info;
	struct c3390_priv *priv = arg;
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *para = &priv->para;

	if (para->num <= 0 && npara)
		para = (void *)npara;

	preg_info = para->reg_info;
	for (i = 0; i < para->num; i++) {
#if 0
		{
			int value;

			value = c3390_read(client, preg_info->reg_addr);
			pr_err("reg:%x read value:%x\n", preg_info->reg_addr, value);
		}
#endif
		c3390_write(client, preg_info->reg_addr, preg_info->value);
#if 0
		{
			int value;

			value = c3390_read(client, preg_info->reg_addr);
			pr_err("reg:%x write value:%x, read back value:%x\n",
					preg_info->reg_addr, preg_info->value, value);
		}
#endif

		preg_info++;
	}

	c3390_agc_mode(priv);

	priv->fps_info.current_fps = MAX_FPS;
	priv->fps_info.to_fps = priv->fps_info.current_fps;

	//VTS=(0x0340,0x0341)=0x‭532‬=‭1330‬ at 15fps
	priv->fps_info.to_fps_value = ((c3390_read(client, 0x340)&0xff)<<8) | ((c3390_read(client, 0x341)&0xff)<<0); //(0x0340,0x0341)
	// pr_err("0x340:%d(0x%04x)", (c3390_read(client, 0x340)&0xff), (c3390_read(client, 0x340)&0xff));
	// pr_err("0x341:%d(0x%04x)", (c3390_read(client, 0x341)&0xff), (c3390_read(client, 0x341)&0xff));

	//HTS=(0x0342,0x0343)=0xb20=2848
	priv->aec_parms.default_hts = ((c3390_read(client, 0x342)&0xff)<<8) | ((c3390_read(client, 0x343)&0xff)<<0); //(0x0340,0x0341)
	// pr_err("0x342:%d(0x%04x)", (c3390_read(client, 0x342)&0xff), (c3390_read(client, 0x342)&0xff));
	// pr_err("0x343:%d(0x%04x)", (c3390_read(client, 0x343)&0xff), (c3390_read(client, 0x343)&0xff));

	priv->aec_parms.calc_vts_tmp  = (priv->fps_info.to_fps_value * MAX_FPS * 100);

	return 0;
}

/*read sensor register*/
static int c3390_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct c3390_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return c3390_read(client, reg_addr);
}
/*write sensor register*/
static int c3390_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct c3390_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return c3390_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int c3390_sensor_read_id_func(void *arg)   //no use IIC bus
{
	return SENSOR_ID;
}

static int aec_parms_init(struct c3390_priv *priv)
{
	priv->aec_parms.curr_again_level = 0;
	priv->aec_parms.curr_again_10x = -1;
	priv->aec_parms.r0x0202_value = 0;
	priv->aec_parms.r0x0203_value = 0;

	priv->aec_parms.target_exp_ctrl = 0;

	priv->aec_parms.default_hts = 0;
	priv->aec_parms.calc_vts_tmp = 0;
	return 0;
}

#define AGIAN_NUM	233
#define SENSOR_MAX_GAIN     (15872*15)                 // max sensor again, a-gain
#define SENSOR_MIN_GAIN     1024
#define SENSOR_MAX_DGAIN     0xFFF
// Gain: 1x = 1024
static unsigned int Tab_c3390RegToGain[240] = 
{ 
    1024,	1088,	1152,	1216,	1280,	1344,	1408,	1472,	1536,	1600,	1664,	1728,	1792,	1856,	1920,	1984,	
	2048,	2048,	2176,	2176,	2304,	2304,	2432,	2432,	2560,	2560,	2688,	2688,	2816,	2816,	2944,	2944,	
	3072,	3072,	3200,	3200,	3328,	3328,	3456,	3456,	3584,	3584,	3712,	3712,	3840,	3840,	3968,	3968,	
	4096,	4096,	4096,	4096,	4352,	4352,	4352,	4352,	4608,	4608,	4608,	4608,	4864,	4864,	4864,	4864,	
	5120,	5120,	5120,	5120,	5376,	5376,	5376,	5376,	5632,	5632,	5632,	5632,	5888,	5888,	5888,	5888,	
	6144,	6144,	6144,	6144,	6400,	6400,	6400,	6400,	6656,	6656,	6656,	6656,	6912,	6912,	6912,	6912,	
	7168,	7168,	7168,	7168,	7424,	7424,	7424,	7424,	7680,	7680,	7680,	7680,	7936,	7936,	7936,	7936,	
	8192,	8192,	8192,	8192,	8192,	8192,	8192,	8192,	8704,	8704,	8704,	8704,	8704,	8704,	8704,	8704,	
	9216,	9216,	9216,	9216,	9216,	9216,	9216,	9216,	9728,	9728,	9728,	9728,	9728,	9728,	9728,	9728,	
	10240,	10240,	10240,	10240,	10240,	10240,	10240,	10240,	10752,	10752,	10752,	10752,	10752,	10752,	10752,	10752,	
	11264,	11264,	11264,	11264,	11264,	11264,	11264,	11264,	11776,	11776,	11776,	11776,	11776,	11776,	11776,	11776,	
	12288,	12288,	12288,	12288,	12288,	12288,	12288,	12288,	12800,	12800,	12800,	12800,	12800,	12800,	12800,	12800,	
	13312,	13312,	13312,	13312,	13312,	13312,	13312,	13312,	13824,	13824,	13824,	13824,	13824,	13824,	13824,	13824,	
	14336,	14336,	14336,	14336,	14336,	14336,	14336,	14336,	14848,	14848,	14848,	14848,	14848,	14848,	14848,	14848,	
	15360,	15360,	15360,	15360,	15360,	15360,	15360,	15360,	15872,	15872,	15872,	15872,	15872,	15872,	15872,	15872,
};

/*set sensor again*/
static int c3390_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	/*
	 * max again < 16 times = 4096
	 * */
	struct c3390_priv *priv = arg;
	struct i2c_client *client = priv->client;
	// unsigned int corse_gain, fine_gain;
	// int blooming_dc = 0x0c;
	// int precharge_lag = 0xc6;

	unsigned int tmp_a_gain = 0;
	unsigned int tmp_a_gain_out = 0;
	unsigned int a_gain_out = 0;

	unsigned int igain = tmp_a_gain;
	unsigned char A_value = 0;
	unsigned short D_value = 0x100;
	unsigned int i = 0;

	pr_debug("%s, again:%d\n", __func__, a_gain);
	tmp_a_gain = a_gain<<2 ;

	if(tmp_a_gain < 1024)
	{
		tmp_a_gain = 1024;
	}
	igain = tmp_a_gain;
	// unsigned char A_value = 0;
	// unsigned short D_value = 0x100;
	// unsigned int i;

	if (igain >= Tab_c3390RegToGain[AGIAN_NUM - 1])
	{
		A_value = AGIAN_NUM - 1;
		D_value = (igain >> 1) / 31; //d_value = ((igain >>10)/15.5)<<8  ==> (igain >>1) /31
		if (D_value > SENSOR_MAX_DGAIN)
		{
			D_value = SENSOR_MAX_DGAIN;
		}
	}
	else
	{
		for (i = 1; i < AGIAN_NUM; i++)
		{
			if (igain < Tab_c3390RegToGain[i])
			{
				A_value = i - 1;
				break;
			}
		}
	}
	a_gain_out = A_value;
	// pr_err("a_gain_out=%d\n", a_gain_out);
	c3390_write(client, 0x0205, a_gain_out & 0xff);

	// if(a_gain<512) {
	// 	/*
	// 	 * [1,2)
	// 	 * 1/32
	// 	 * */
	// 	corse_gain = 0x3;
	// 	fine_gain = 0x20 + (a_gain - 256) / 8; /*256/32=8*/
	// 	// blooming_dc = 0x0c;
	// 	// precharge_lag = 0xc6;
	// } else if(a_gain<1024) {
	// 	/*
	// 	 * [2,4)
	// 	 * 1/32
	// 	 * */
	// 	corse_gain = 0x7;
	// 	fine_gain = 0x20 + (a_gain - 512) / 16;/*512/32=16*/
	// 	blooming_dc = 0x16;
	// 	// precharge_lag = 0xc6;
	// } else if(a_gain<2048) {
	// 	/*
	// 	 * [4,8)
	// 	 * 1/16
	// 	 * */
	// 	corse_gain = 0xf;
	// 	fine_gain = 0x20 + (a_gain - 1024) / 32;/*1024/32=32*/
	// 	blooming_dc = 0x16;
	// 	precharge_lag = 0xd6;
	// } else if(a_gain<4096) {
	// 	/*
	// 	 * [8,16)
	// 	 * 1/8
	// 	 * */
	// 	corse_gain = 0x1f;
	// 	fine_gain = 0x20 + (a_gain - 2048) / 64;/*2048/32=64*/
	// 	blooming_dc = 0x16;
	// 	precharge_lag = 0xd6;
	// } else {
	// 	corse_gain = 0x1f;
	// 	fine_gain = 0x3f;
	// 	blooming_dc = 0x16;
	// 	precharge_lag = 0xd6;
	// }

	// if ((corse_gain != priv->aec_parms.r0x3e08_corse_gain) ||
	// 		(fine_gain != priv->aec_parms.r0x3e09_fine_gain)) {
	// 	/*save again*/
	//	priv->aec_parms.r0x3e08_corse_gain = corse_gain;
	// 	priv->aec_parms.r0x3e09_fine_gain = fine_gain;
	// } else {
	// 	return 0;
	// }

	// c3390_write(client, 0x3e08, corse_gain);
	// c3390_write(client, 0x3e09, fine_gain);

	return A_GAIN_EFFECT_FRAMES;
}
/*set sensor dgain*/
static int c3390_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
	return D_GAIN_EFFECT_FRAMES;
}
static int __set_reg_exp_time(struct c3390_priv *priv, int exp_time)
{
	int ret = 0;
	struct i2c_client *client = priv->client;

	unsigned char exposure_time_msb;
	unsigned char exposure_time_lsb;

	pr_debug("%s, exp:%d\n", __func__, exp_time);
	exposure_time_msb =(exp_time>>8)&0xff;
	exposure_time_lsb =(exp_time)&0xff;
	c3390_write(client, 0x202, exposure_time_msb);
	c3390_write(client, 0x203, exposure_time_lsb);

	priv->aec_parms.r0x0202_value = exposure_time_lsb;
	priv->aec_parms.r0x0203_value = exposure_time_msb;

	//priv->aec_parms.target_exp_ctrl = exp_time;
	return ret;
}

static int __set_reg_frame_vts(struct c3390_priv *priv, int vts)
{
	int ret = 0;
	struct i2c_client *client = priv->client;

	c3390_write(client, 0x340, (vts >> 8) & 0xff);
	c3390_write(client, 0x341, vts & 0xff);

	priv->fps_info.to_fps_value = vts;

	return ret;
}

static int __get_reg_frame_vts(struct c3390_priv *priv)
{
	struct i2c_client *client = priv->client;

	return c3390_read(client, 0x340) << 8 | c3390_read(client, 0x341);
}

static void update_frame_vts_and_exp_ctrl(struct c3390_priv *priv)
{
	struct i2c_client *client = priv->client;

    /*
     * exp_time=1 mean half line exptime
     * max exp_time = (0x340,0x341) - 2
     *              = VTS - 2
     *
     * VTS = (0x340,0x341)
     *     = 0x532
     *     = 1330‬                  //at 15fps
     *
     * now:
     * max exp_time = VTS - 2
     *              = 1330‬ - 2
     *              = 1328         //at 15fps
     */

	int const cur_frame_vts     = __get_reg_frame_vts(priv);
	int const target_frame_vts  = priv->fps_info.to_fps_value;

	unsigned int const max_exp_ctrl      = priv->fps_info.to_fps_value - 2;   //vts-2

	unsigned int target_exp_ctrl = 0;
	if (priv->aec_parms.target_exp_ctrl <= max_exp_ctrl) {
		target_exp_ctrl = priv->aec_parms.target_exp_ctrl;
		//pr_err("priv->target_exp_ctrl <= max_exp_ctrl, use priv->aec_parms.target_exp_ctrl:%d\n",priv->aec_parms.target_exp_ctrl);
	} else {
		target_exp_ctrl = max_exp_ctrl;
		//pr_err("priv->target_exp_ctrl > max_exp_ctrl, use max_exp_ctrl:%d\n", max_exp_ctrl);
	}
    if(target_frame_vts <= 0)
        return ;
	// pr_err("VTS = %d->%d \n", cur_frame_vts, target_frame_vts);
	// pr_err("EXP = %d/%d \n", target_exp_ctrl, priv->aec_parms.target_exp_ctrl);

	if (target_frame_vts >= cur_frame_vts) {
		if (target_frame_vts != cur_frame_vts)
			__set_reg_frame_vts(priv, target_frame_vts);
		__set_reg_exp_time(priv, target_exp_ctrl);
	} else {
		__set_reg_exp_time(priv, target_exp_ctrl);
		__set_reg_frame_vts(priv, target_frame_vts);
	}
}

/*set sensor exp time*/
static int c3390_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	struct c3390_priv *priv = arg;
	struct i2c_client *client = priv->client;

	priv->aec_parms.target_exp_ctrl = exp_time;
	update_frame_vts_and_exp_ctrl(priv);

	return EXP_EFFECT_FRAMES;
}
/*sensor timer*/
static int c3390_sensor_timer_func(void *arg)
{
	return 0;
}
/*standby in*/
static int c3390_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int c3390_sensor_set_standby_out_func(void *arg)
{
	return 0;
}

/*low level read sensor ID, user i2c ops*/
static int c3390_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct c3390_priv *priv = arg;
	struct i2c_client *client = priv->client;
	int id;
	int value;

	value = c3390_read(client, 0x0000);
	id = value << 8;

	value = c3390_read(client, 0x0001);
	id |= value;

	pr_err("id:%x\n", id);

	if (id == ACTUAL_SENSOR_ID)
		return SENSOR_ID;


	return 0;
}

/*get resolution*/
static int c3390_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int c3390_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int c3390_sensor_get_fps_func(void *arg)
{
	struct c3390_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int c3390_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type c3390_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}
static int get_ae_fast_default(struct ae_fast_struct *ae_fast)
{
	*ae_fast = ae_fast_default;
	return 0;
}
/*get self definded params*/
static int c3390_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct c3390_priv *priv = arg;

	switch (param) {
		case GET_INTERFACE:
			*((int *)value) = dvp ? DVP_INTERFACE:MIPI_INTERFACE;
			break;

		case GET_IO_LEVEL:
			*((int *)value) = SENSOR_IO_LEVEL;
			break;

		case GET_MIPI_MHZ:
			*((int *)value) = MIPI_MHZ;
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

		case GET_SCAN_METHOD:
			ret = -1;
			break;

		default:
			pr_err("%s param:%d not support\n", __func__, param);
			ret = -1;
			break;
	}

	return ret;
}
/*sensor power on*/
static int c3390_sensor_set_power_on_func(void *arg)
{
	struct c3390_priv *priv = arg;

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

	aec_parms_init(priv);

	return 0;
}
/*sensor power off*/
static int c3390_sensor_set_power_off_func(void *arg)
{
	struct c3390_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}

static int c3390_fps_to_vts(struct c3390_priv *priv, const int fps)
{
	/*
	 * c3390:
	 * C3390_2304x1296_2lane_15fps_mclk24M_pclk_568M_mipirate284Mbps_v2p0.ini
	 *
	 * mipi:
	 * HTS=(0x0342,0x0343)=0xb20=2848
	 * VTS=(0x0340,0x0341)=0x‭532‬=‭1330‬ at 15fps
	 * fps = VTS1 * 15 / VTS
	 *
	 * so :
	 * vts = VTS1 * 15 / fps = ‭1330‬*15 / fps
	 *     = (19950*100) / (fps*100)
	 *     = (1995000UL) / vts;   //vts=fps*100
	 *
	 */

	struct i2c_client *client = priv->client;

	int vts;

	pr_debug("%s %d fps:%d default_hts:%d tmp:%d\n", __func__, __LINE__, fps, priv->aec_parms.default_hts, priv->aec_parms.calc_vts_tmp);

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

	vts = (priv->aec_parms.calc_vts_tmp) / vts;

	return vts;
}

/*set sensor fps*/
static int c3390_sensor_set_fps_func(void *arg, const int fps)
{
	int tmp;
	struct c3390_priv *priv = arg;

	tmp = c3390_fps_to_vts(priv, fps);

	if (tmp > 0) {
		priv->fps_info.to_fps_value = tmp;
		priv->fps_info.to_fps = fps;
	}
	return 0;
}

static void c3390_set_fps_async(struct c3390_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (priv->fps_info.to_fps != priv->fps_info.current_fps) {
		update_frame_vts_and_exp_ctrl(priv);
		priv->fps_info.current_fps = priv->fps_info.to_fps;
	}
}

static AK_ISP_SENSOR_CB c3390_cb = {
	.sensor_init_func					= c3390_sensor_init_func,
	.sensor_read_reg_func				= c3390_sensor_read_reg_func,
	.sensor_write_reg_func				= c3390_sensor_write_reg_func,
	.sensor_read_id_func				= c3390_sensor_read_id_func,
	.sensor_update_a_gain_func			= c3390_sensor_update_a_gain_func,
	.sensor_update_d_gain_func			= c3390_sensor_update_d_gain_func,
	.sensor_updata_exp_time_func		= c3390_sensor_updata_exp_time_func,
	.sensor_timer_func					= c3390_sensor_timer_func,
	.sensor_set_standby_in_func			= c3390_sensor_set_standby_in_func,
	.sensor_set_standby_out_func		= c3390_sensor_set_standby_out_func,
	.sensor_probe_id_func				= c3390_sensor_probe_id_func,
	.sensor_get_resolution_func			= c3390_sensor_get_resolution_func,
	.sensor_get_mclk_func				= c3390_sensor_get_mclk_func,
	.sensor_get_fps_func				= c3390_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func	= c3390_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func			= c3390_sensor_get_bus_type_func,
	.sensor_get_parameter_func			= c3390_sensor_get_parameter_func,
	.sensor_set_power_on_func			= c3390_sensor_set_power_on_func,
	.sensor_set_power_off_func			= c3390_sensor_set_power_off_func,
	.sensor_set_fps_func				= c3390_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return c3390_sensor_read_id_func(NULL);
}

static char *sensor_if_func(void)
{
	static char ifstr[16] = "dvp";

	if (!dvp)
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
