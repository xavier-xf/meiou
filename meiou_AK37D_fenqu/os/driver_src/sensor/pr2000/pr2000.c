/*
 * pr2000 Camera Driver
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

/*pr2000 resulotins params*/
#include "pr2000_720p_25fps.h"
#include "pr2000_720p_30fps.h"
#include "pr2000_ntsc_60fps.h"
#include "pr2000_pal_50fps.h"

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
#define SENSOR_I2C_ADDR			0x5c 
#define SENSOR_ID				0x2000
#define SENSOR_MCLK				27
#define SENSOR_REGADDR_BYTE		1
#define SENSOR_DATA_BYTE		1
#if 0
#define SENSOR_OUTPUT_WIDTH		1280
#define SENSOR_OUTPUT_HEIGHT	720
#else
static int SENSOR_OUTPUT_WIDTH =1280;
static int SENSOR_OUTPUT_HEIGHT=720;
#endif
#define SENSOR_VALID_OFFSET_X	0
#define SENSOR_VALID_OFFSET_Y	0
#define SENSOR_BUS_TYPE			BUS_TYPE_YUV
#define SENSOR_IO_INTERFACE		DVP_INTERFACE
#define SENSOR_IO_LEVEL			IO_LEVEL_3V3
#define MAX_FPS					25

#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	1	/*adjust a_gain for 1/2 frames*/
#define D_GAIN_EFFECT_FRAMES	1	/*adjust d_gain for 1/2 frames*/

#define DELAY_FLAG		(0xffff)

enum input_res {
	INPUT_SD480I = 0,
	INPUT_SD576I,
	INPUT_HD720P,
	INPUT_HD1080P,
	INPUT_HD960P_HD800P,
};

struct res_to_timing_info {
	int fps;
	int res;
	struct isp_timing_info isp_timing;
};
/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct pr2000_win_size {
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
struct pr2000_aec_parms {

};

static struct res_to_timing_info res_to_timing[] = {
	{60, INPUT_SD480I, {8000,	1,	1400,	12}},/*INPUT_SD480I*/
	{50, INPUT_SD576I, {11520,	1,	1866,	12}},/*INPUT_SD576I*/
	{25, INPUT_HD720P, {9599,	1,	3373,	15}},/*INPUT_HD72OP@25fps*/
	{30, INPUT_HD720P, {8000,	1,	1774,	15}},/*INPUT_HD72OP@30fps*/
	{0, INPUT_HD1080P, {0,	0,		0,	0}},/*INPUT_HD108OP*/
	{0, INPUT_HD960P_HD800P, {0,	0,	0,	0}},/*INPUT_HD960P_HD800P*/
};

static const struct v4l2_ctrl_ops pr2000_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &pr2000_ctrl_ops,
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
 * @method:		scanning method
 * @preg_info:	sensor registers
 * @num:		number of sensor registers
 * @fps_num:	fps & resolution
 */
struct pr2000_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct pr2000_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct pr2000_aec_parms aec_parms;

	enum scan_method method;

	AK_ISP_SENSOR_REG_INFO *preg_info;
	int num;
	int fps_res;

	struct v4l2_ctrl *ctrl_sensor_get_id;
};

/*configure pr2000 to auto detect the format of input video signal*/
static AK_ISP_SENSOR_REG_INFO PR2000_INPUT_SOURCE_DET[]=
{
	{0xFF,0x00},
	{0xD0,0x30},
	{0xd1,0x08},
	{0xd2,0x21},
	{0xd3,0x00},
	{0xd4,0x00},
	{0xd5,0x00},
	{0xd6,0x00},
	{0xd7,0x00},
	{0xd8,0x30},
	{0xd9,0x08},
	{0xda,0x21},
	{0xff,0x01},
	{0x00,0xe4},
	{0x01,0x61},
	{0x02,0x00},
	{0x03,0x57},
	{0x04,0x0c},
	{0x05,0x88},
	{0x06,0x04},
	{0x07,0xb2},
	{0x08,0x44},
	{0x09,0x34},
	{0x0a,0x02},
	{0x0b,0x14},
	{0x0c,0x04},
	{0x0d,0x08},
	{0x0e,0x5e},
	{0x0f,0x5e},
	{0x10,0x26},
};

static AK_ISP_SENSOR_CB pr2000_cb;

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 0;
module_param(check_id, int, 0644);

static void pr2000_set_fps_async(struct pr2000_priv *priv);
static int __pr2000_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int pr2000_sensor_read_id_func(void *arg);   //no use IIC bus
static int pr2000_sensor_get_resolution_func(void *arg, int *width, int *height);
static AK_ISP_SENSOR_REG_INFO *get_init_sequence(
		struct i2c_client *client, struct pr2000_priv *priv, int *fps, int *num);
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);

static u32 pr2000_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct pr2000_priv *to_pr2000(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct pr2000_priv,
			    subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct pr2000_priv, hdl)->subdev;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int pr2000_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int pr2000_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
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
static int pr2000_s_ctrl(struct v4l2_ctrl *ctrl)
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
static int pr2000_core_s_power(struct v4l2_subdev *sd, int on)
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
static int pr2000_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = pr2000_sensor_read_id_func(NULL);   //no use IIC bus
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
static int pr2000_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pr2000_priv *priv = to_pr2000(client);

	ctrl->value = (int)&priv->cb_info;
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
static int pr2000_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = pr2000_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = pr2000_get_sensor_cb(sd, ctrl);
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
static int pr2000_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pr2000_priv *priv = to_pr2000(client);
	struct isp_timing_cb_info *isp_timing_cb_info = (void *)ctrl->value;

	memcpy(&priv->hcb.isp_timing_cb_info,
			isp_timing_cb_info,
			sizeof(struct isp_timing_cb_info));
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
static int pr2000_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = pr2000_set_isp_timing_cb(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static long pr2000_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pr2000_priv *priv = to_pr2000(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			/*pr2000 regs in driver, so returen success*/
			break;

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
static int pr2000_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct pr2000_priv *priv = to_pr2000(client);
	static struct pr2000_win_size win = {
		.name = "default",
		.width = 0,//SENSOR_OUTPUT_WIDTH,
		.height = 0,//SENSOR_OUTPUT_HEIGHT,
		.regs = NULL,
	};

	/*pr2000 width & height are auto dect, so fill them to again*/
	win.width = SENSOR_OUTPUT_WIDTH;
	win.height = SENSOR_OUTPUT_HEIGHT;

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
static int pr2000_set_fmt(struct v4l2_subdev *sd,
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
static int pr2000_get_selection(struct v4l2_subdev *sd,
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
static int pr2000_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(pr2000_codes))
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
static int pr2000_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	/*return sensor cut window size&offset*/
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
static int pr2000_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	pr2000_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
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
static int pr2000_video_probe(struct i2c_client *client)
{
	struct pr2000_priv *priv = to_pr2000(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops pr2000_ctrl_ops = {
	.g_volatile_ctrl	= pr2000_g_volatile_ctrl,
	.s_ctrl				= pr2000_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops pr2000_subdev_core_ops = {
	.s_power	= pr2000_core_s_power,
	.g_ctrl		= pr2000_core_g_ctrl,
	.s_ctrl		= pr2000_core_s_ctrl,
	.ioctl		= pr2000_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int pr2000_g_mbus_config(struct v4l2_subdev *sd,
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
static int pr2000_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	printk(KERN_ERR "%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops pr2000_subdev_video_ops = {
	.s_stream	= pr2000_s_stream,
	.cropcap	= pr2000_cropcap,
	.g_crop		= pr2000_g_crop,
	.g_mbus_config	= pr2000_g_mbus_config,
	.s_crop		= pr2000_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops pr2000_subdev_pad_ops = {
	.enum_mbus_code = pr2000_enum_mbus_code,
	.get_fmt	= pr2000_get_fmt,
	.set_fmt	= pr2000_set_fmt,
	.get_selection	= pr2000_get_selection,
};

/*sensor driver subdev ops*/
static struct v4l2_subdev_ops pr2000_subdev_ops = {
	.core	= &pr2000_subdev_core_ops,
	.video	= &pr2000_subdev_video_ops,
	.pad	= &pr2000_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct pr2000_priv *priv)
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
 * XXXX_detect- detect mode
 * @priv:			pointer to pirvate structure
 *
 * @RETURN:	none
 */
static void pr2000_detect(struct pr2000_priv *priv)
{
	struct i2c_client *client = priv->client;
	int fps = 0;
	int num = 0;

	priv->cb_info.cb->sensor_set_power_on_func(priv);

	/*auto detect video mode input*/
	priv->preg_info = get_init_sequence(client, priv, &fps, &num);
	priv->num = num;
	priv->fps_info.current_fps = fps;
}

/*
 * XXXX_match - check sensor id for match driver
 * @priv:			pointer to pirvate structure
 *
 * @RETURN:	0-match fail; 1-match success
 */
static int pr2000_match(struct pr2000_priv *priv)
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
static int pr2000_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct pr2000_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"pr2000: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct pr2000_priv), GFP_KERNEL);
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
	priv->cb_info.cb = &pr2000_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	pr2000_detect(priv);

	if (!pr2000_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	/*subdev init*/
	v4l2_i2c_subdev_init(&priv->subdev, client, &pr2000_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &pr2000_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &pr2000_ctrl_ops,
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

	ret = pr2000_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "pr2000 probe fail\n");
		goto err_videoprobe;
	}

	/*register async subdev*/
	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "pr2000 Probed success, subdev:%p\n", &priv->subdev);

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
static int pr2000_remove(struct i2c_client *client)
{
	struct pr2000_priv       *priv = to_pr2000(client);

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

static const struct i2c_device_id pr2000_id[] = {
	{ "pr2000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pr2000_id);

static const struct of_device_id pr2000_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, pr2000_of_match);

static struct i2c_driver pr2000_i2c_driver = {
	.driver = {
		.name = "pr2000",
		.of_match_table = of_match_ptr(pr2000_of_match),
	},
	.probe    = pr2000_probe,
	.remove   = pr2000_remove,
	.id_table = pr2000_id,
};

module_i2c_driver(pr2000_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for pr2000 sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.6");

/*sensor i2c write*/
static int pr2000_write(const struct i2c_client *client, int reg, int value)
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
static int pr2000_read(const struct i2c_client *client, int reg)
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

/*pr2000 detect analog singal input*/
static int detect_video_input(struct i2c_client *client, enum input_res *res, int *fps)
{
	int i;
	int value;
	int video_det;
	AK_ISP_SENSOR_REG_INFO *preg_info;

	preg_info = PR2000_INPUT_SOURCE_DET;
	for (i = 0; i < sizeof(PR2000_INPUT_SOURCE_DET) / sizeof(PR2000_INPUT_SOURCE_DET[0]); i++) {
		pr2000_write(client, preg_info->reg_addr, preg_info->value);
		preg_info++;
	}

	pr2000_write(client, 0xff,0x00);
	mdelay(60);/*delay for auto detect*/
	value = pr2000_read(client, 0x00);
	pr_err("%s read (page 0,reg 0) :0x%x, \n", __func__, value);
	video_det = (value >> 3)&0x1;
	*fps = (value >> 4)&0x3;
	*res = ((value >> 0)&0x3) + INPUT_SD480I;

	if (video_det) {
		switch (*fps) {
			case 0:
				*fps = 25;
				break;
			case 1:
				*fps = 30;
				break;
			case 2:
				*fps = 50;
				break;
			case 3:
				*fps = 60;
				break;
			default:
				*fps = 0;
				break;
		}
	}

	return !video_det;/*return 0 is dectetion ok*/
}

static void set_timing(struct pr2000_priv *priv, int fps_res)
{
	int i;

	for (i = 0; i < sizeof(res_to_timing) / sizeof(res_to_timing[0]); i++)
		if (((res_to_timing[i].fps << 8) | res_to_timing[i].res) == fps_res) {
			void *arg = priv->hcb.isp_timing_cb_info.isp_timing_arg;
			SET_ISP_MISC set_isp_timing = priv->hcb.isp_timing_cb_info.set_isp_timing;

			pr_err("set_isp_timing:%p, arg:%p, i:%d\n", set_isp_timing, arg, i);

			if (set_isp_timing)
				set_isp_timing(arg, &res_to_timing[i].isp_timing);
			break;
		}
}

/*get init sequence suite for current analog input signal*/
static AK_ISP_SENSOR_REG_INFO *get_init_sequence(
		struct i2c_client *client, struct pr2000_priv *priv, int *fps, int *num)
{
	int fps_res;
	enum input_res res;
	AK_ISP_SENSOR_REG_INFO *preg_info = NULL;

	if (detect_video_input(client, &res, fps))
		goto end;

	fps_res = (*fps << 8) | res;
	switch (fps_res) {
		case ((60 << 8) | INPUT_SD480I):
			preg_info = PR2000_NTSC_60FPS_INIT;
			*num = sizeof(PR2000_NTSC_60FPS_INIT) / sizeof(PR2000_NTSC_60FPS_INIT[0]);
			SENSOR_OUTPUT_WIDTH = 720;
			SENSOR_OUTPUT_HEIGHT = 240;
			priv->method = SCAN_METHOD_INTERLACED;
			break;
		case ((50 << 8) | INPUT_SD576I):
			preg_info = PR2000_PAL_50FPS_INIT;
			*num = sizeof(PR2000_PAL_50FPS_INIT) / sizeof(PR2000_PAL_50FPS_INIT[0]);
			SENSOR_OUTPUT_WIDTH = 720;
			SENSOR_OUTPUT_HEIGHT = 288;
			priv->method = SCAN_METHOD_INTERLACED;
			break;
		case ((25 << 8) | INPUT_HD720P):
			SENSOR_OUTPUT_WIDTH =1280;
			SENSOR_OUTPUT_HEIGHT=720;
			preg_info = PR2000_720P_25FPS_INIT;
			*num = sizeof(PR2000_720P_25FPS_INIT) / sizeof(PR2000_720P_25FPS_INIT[0]);
			priv->method = SCAN_METHOD_PROGRESSIVE;
			break;
		case ((30 << 8) | INPUT_HD720P):
			SENSOR_OUTPUT_WIDTH =1280;
			SENSOR_OUTPUT_HEIGHT=720;
			preg_info = PR2000_720P_30FPS_INIT;
			*num = sizeof(PR2000_720P_30FPS_INIT) / sizeof(PR2000_720P_30FPS_INIT[0]);
			priv->method = SCAN_METHOD_PROGRESSIVE;
			break;
		default:
			pr_err("%s cannot support res:%d, fps:%d\n", __func__, res, *fps);
			break;
	}

	/*store for using after soon*/
	priv->fps_res = fps_res;

end:
	return preg_info;
}

/*
 * The sensor must set callbacks
 *
 * */
static int pr2000_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *para)
{
	int i;
	struct pr2000_priv *priv = arg;
	int num = priv->num;
	AK_ISP_SENSOR_REG_INFO *preg_info = priv->preg_info;
	int fps_res = priv->fps_res;
	struct i2c_client *client = priv->client;

	for (i = 0; i < num; i++) {
		pr2000_write(client, preg_info->reg_addr, preg_info->value);
		preg_info++;
	}

	set_timing(priv, fps_res);

	return 0;
}

/*read sensor register*/
static int pr2000_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct pr2000_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return pr2000_read(client, reg_addr);
}
/*write sensor register*/
static int pr2000_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct pr2000_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return pr2000_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int pr2000_sensor_read_id_func(void *arg)   //no use IIC bus
{
	return SENSOR_ID;
}

/*set sensor again*/
static int pr2000_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	return 0;
}
/*set sensor dgain*/
static int pr2000_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
	return 0;
}
/*set sensor exp time*/
static int pr2000_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	return 0;
}
/*sensor timer*/
static int pr2000_sensor_timer_func(void *arg)
{
	return 0;
}
/*standby in*/
static int pr2000_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int pr2000_sensor_set_standby_out_func(void *arg)
{
	return 0;
}
/*low level read sensor ID, user i2c ops*/
static int __pr2000_sensor_probe_id_func(struct i2c_client *client)  //use IIC bus
{
	int id;
	int value;

	pr2000_write(client, 0xff, 0x00);
	value = pr2000_read(client, 0xfc);
	id = value << 8;

	value = pr2000_read(client, 0xfd);
	id |= value;

	if (id == SENSOR_ID) {
		pr_err("%s success\n", __func__);
		return SENSOR_ID;
	}

	pr_err("%s fail\n", __func__);
	return 0;
}
/*read sensor ID, user i2c ops*/
static int pr2000_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct pr2000_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return __pr2000_sensor_probe_id_func(client);
}
/*get resolution*/
static int pr2000_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int pr2000_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int pr2000_sensor_get_fps_func(void *arg)
{
	struct pr2000_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int pr2000_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type pr2000_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}
/*get self definded params*/
static int pr2000_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct pr2000_priv *priv = arg;

	switch (param) {
		case GET_INTERFACE:
			*((int *)value) = SENSOR_IO_INTERFACE;
			break;

		case GET_IO_LEVEL:
			*((int *)value) = SENSOR_IO_LEVEL;
			break;

		case GET_RESET_GPIO:
			*((int *)value) = priv->gpio_reset;
			break;

		case GET_PWDN_GPIO:
			*((int *)value) = priv->gpio_pwdn;
			break;

		case GET_SCAN_METHOD:
			*((int *)value) = priv->method;
			break;

		default:
			pr_err("%s param:%d not support\n", __func__, param);
			ret = -1;
			break;
	}

	return ret;
}
/*sensor power on*/
static int pr2000_sensor_set_power_on_func(void *arg)
{
	struct pr2000_priv *priv = arg;
	struct i2c_client *client = priv->client;

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

	priv->fps_info.to_fps = priv->fps_info.current_fps;
	priv->fps_info.to_fps_value = 0;

	return 0;
}
/*sensor power off*/
static int pr2000_sensor_set_power_off_func(void *arg)
{
	struct pr2000_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}
/*set sensor fps*/
static int pr2000_sensor_set_fps_func(void *arg, const int fps)
{
	return 0;
}

static AK_ISP_SENSOR_CB pr2000_cb = {
	.sensor_init_func = pr2000_sensor_init_func,
	.sensor_read_reg_func = pr2000_sensor_read_reg_func,
	.sensor_write_reg_func = pr2000_sensor_write_reg_func,
	.sensor_read_id_func = pr2000_sensor_read_id_func,
	.sensor_update_a_gain_func = pr2000_sensor_update_a_gain_func,
	.sensor_update_d_gain_func = pr2000_sensor_update_d_gain_func,
	.sensor_updata_exp_time_func = pr2000_sensor_updata_exp_time_func,
	.sensor_timer_func = pr2000_sensor_timer_func,
	.sensor_set_standby_in_func = pr2000_sensor_set_standby_in_func,
	.sensor_set_standby_out_func = pr2000_sensor_set_standby_out_func,
	.sensor_probe_id_func = pr2000_sensor_probe_id_func,
	.sensor_get_resolution_func = pr2000_sensor_get_resolution_func,
	.sensor_get_mclk_func = pr2000_sensor_get_mclk_func,
	.sensor_get_fps_func = pr2000_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func = pr2000_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func = pr2000_sensor_get_bus_type_func,
	.sensor_get_parameter_func = pr2000_sensor_get_parameter_func,
	.sensor_set_power_on_func = pr2000_sensor_set_power_on_func,
	.sensor_set_power_off_func = pr2000_sensor_set_power_off_func,
	.sensor_set_fps_func = pr2000_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return pr2000_sensor_read_id_func(NULL);
}

static char *sensor_if_func(void)
{
	static char ifstr[16] = "dvp"; 

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
