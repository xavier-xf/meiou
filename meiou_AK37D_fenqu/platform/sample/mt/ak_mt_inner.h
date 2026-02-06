#include "ak_mt.h"

#define FILTER_BIG_DEFAULT      12000     // default threshold that roughly detects motion region 
#define FILTER_SMALL_DEFAULT    6000      // default threshold that expands rough detection result

#define MAX_INVALID_FRAMES      200       // max frames to determine whether motion is invalid or not     
#define MAX_DECISION_FRAMES     7         // max frames to determine the position of largest size motion region
#define INVALID_FRAMES_CAP      400       // max frames to determine whether motion is invalid or not     
#define DECISION_FRAMES_CAP     20        // max frames to determine the position of largest size motion region
#define WAIT_FRAMES             20        // frames without motion region detection while camera rotates
#define WAIT_TIME               500       // waiting time for rotation

#define ANGLE_H                 30        // rotate angle in horizontal direction
#define ANGLE_V                 10        // rotate angle in vertical direction

#define MIN_ANGLE_H             0         // min access angle of camera in horizontal direction
#define MAX_ANGLE_H             350       // max access angle of camera in horizontal direction
#define MIN_ANGLE_V             0         // min access angle of camera in vertical direction
#define MAX_ANGLE_V             130       // max access angle of camera in vertical direction
#define DECISION_FRAMES_CAP_V   5         // max valid frames in vertical direction

#define INVALID_DISTANCE        8         // distance threshold to separate valid motion and invalid motion

typedef struct TRACKING_PARAM_STRU
{
	void *mt_handle;
	MT_RECTANGLE valid_rect_v[DECISION_FRAMES_CAP_V];
	unsigned char valid_rect_num_v;
	MT_RECTANGLE invalid_rect[INVALID_FRAMES_CAP];	 // invalid motion region 
	MT_RECTANGLE valid_rect[DECISION_FRAMES_CAP];   // valid motion region
	short invalid_rect_num;                      // invalid motion region count
	unsigned char valid_rect_num;                                 // valid motion region count
	unsigned char valid_rect_left_index[DECISION_FRAMES_CAP];     // valid motion region on the left 
	unsigned char valid_rect_left_num;                            // the count of valid motion region on the left 
	unsigned char valid_rect_right_index[DECISION_FRAMES_CAP];    // valid motion region on the right 
	unsigned char valid_rect_right_num;                           // the count of valid motion region on the right
	unsigned char valid_rect_botoom_index[DECISION_FRAMES_CAP];   // valid motion region on the botoom 
	unsigned char valid_rect_botoom_num;                          // the count of valid motion region on the botoom
	unsigned char valid_rect_top_index[DECISION_FRAMES_CAP];      // valid motion region on the top 
	unsigned char valid_rect_top_num;                             // the count of valid motion region on the top 
	unsigned char valid_rect_center_index[DECISION_FRAMES_CAP];   // valid motion region in the center 
	unsigned char valid_rect_center_num;                          // the count of valid motion region in the center
	short current_angle_h;     // current angle of camera  in horizontal direction    
	short current_angle_v;     // current angle of camera in vertical direction
	unsigned char current_max_invalid_rect_dis;   // current max distance between invalid motion region

	short fps;      // encode frames per second
	short size_min; // min and max size of valid motion region 
	short size_max;
	short boundary_size_h;  // horizontal and vertical distance between center region and video frame boundary
	short boundary_size_v;    
	signed char mt_enable;        // motion region detection switch
	signed char decision_enalbe;   // motion region changing switch
	signed char rotate_h_enable;   // camera horizontal rotation switch
	signed char rotate_v_enable;   // camera vertical rotation switch

	unsigned short max_invalid_fm;  // max frames to determine whether motion is invalid or not     
	unsigned char max_decision_fm;  // max frames to determine the position of largest size motion region
	unsigned char max_decision_fm_backup; // backup of max frames to determine the position of largest size motion region
	unsigned char wait_fm;          // frames without motion region detection while camera rotates
	unsigned short wait_time;        // waiting time for rotation
	
	short agl_h;  // rotate angle in horizontal direction
	short agl_v;  // rotate angle in vertical direction

	short rotation_dir_h; // camera rotation direction in horizontal and vertical direction
	short rotation_dir_v;
	short init_agl_h; // init camera angle position
	short init_agl_v;
	short min_agl_h;  // min accessible angle of camera in horizontal direction
	short max_agl_h;  // max accessible angle of camera in horizontal direction
	short min_agl_v;  // min accessible angle of camera in vertical direction
	short max_agl_v;  // max accessible angle of camera in vertical direction
	int mt_factor; // camera rotation factor
	short undetected_num; // undetected_num while making decision
	signed char main_decision_dir_v; // main decision direction in vertical direction

	unsigned char invalid_dis; // distance threshold to separate valid motion and invalid motion
}TRACKING_PARAM;

/**
 * mt_reset_tracking_param - reset tracking parameters
 * @param[IN]: motion tracking parameter handle
 * return: none
 * notes:
 */
void mt_reset_tracking_param(TRACKING_PARAM *param);

/**
 * mt_member_copy - copy member of struct rectangle
 * @rect_org[IN]: motion region
 * @rect_dst[OUT]: motion region
 * return: none
 * notes:
 */
void mt_member_copy(MT_RECTANGLE *rect_dst, MT_RECTANGLE *rect_org);

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
int mt_mrd_Isoutofcenter(TRACKING_PARAM *param, MT_RECTANGLE *rect, MT_POINT *distance, short len, short size_h, short size_v);

/**
 * mt_tracking_max_distance - calculate the max distance between recorded invalid motion region
 * @param[IN]: motion tracking parameter handle
 * @rect[IN]: motion region
 * return: none
 * notes:
 */
void mt_tracking_max_distance(TRACKING_PARAM *param, MT_RECTANGLE *rect);

/**
 * mt_rect_optimization - optimize the detected rectangles and return some useful information for speed calculation
 * @param[IN]: motion tracking parameter handle
 * return: -1 speed calculation is not required, otherwise, the index of the start rect to calculate speed 
 * notes:
 */
int mt_rect_optimization(TRACKING_PARAM *param);

/**
 * mt_get_rotation_angle - calculate the rotation angle based on the distance and speed of moving object
 * @param[IN]: motion tracking parameter handle
 * @len[IN]: VPSS_MD_DIMENSION in vertical direction
 * return: 1 ready to rotate, 0 wait for more frames
 * notes:
 */
int mt_get_rotation_angle(TRACKING_PARAM *param, short len);

/**
 * mt_get_rotation_angle_v - calculate the rotation angle based on the distance and speed of moving object
 * @param[IN]: motion tracking parameter handle
 * @len[IN]: VPSS_MD_DIMENSION in vertical direction
 * @rect[IN]: motion region
 * return: 1 ready to rotate, 0 no rotation is required
 * notes:
 */
int mt_get_rotation_angle_v(TRACKING_PARAM *param, short len, MT_RECTANGLE *rect);
