
#ifndef _LANG_XLS_H_
#define _LANG_XLS_H_

#include <stdbool.h>
// #include "../../_wasApp/wasApp.h"

#define XLS_MNT_TF_PATH    "/mnt/tf/tuya_pid.xls"         // xls文件的路径
#define XLS_NFS_PATH    "/mnt/nfs/language.xls"     // 

#define CODE "UTF-8"                    // xls文件的编码格式


char ***lang_xls_init(int sheet_num);

bool lang_xls_file_state_get(void);

int lang_xls_null_str_num_get(void);

int lang_xls_language_num_get(void);

int lang_xls_str_num_get(void);


bool init_language_xls_info(void);



char *lang_xls_str_get(int str_num, int lang_type);

bool is_language_xls_inited();

#endif // _LANG_XLS_H_
