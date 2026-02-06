#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "../share/include/libxls/xls.h"
#include "lang_xls.h"
#include "language.h"
#include "user_data.h"


typedef struct
{
    bool xls_is_exist;     // xls文件存在标志位
    int  xls_null_str_num; // xls文件 在有效行列中 存在空字符的数量
    int  row_total;        // xls文档的有效总行数
    int  col_total;        // xls文档的有效总列数
} xls_info_t;

xls_info_t xls_info = {false, 0, 0, 0};

char ***buffer = NULL;

static bool language_xls_init_state = false;

/*******************************************************************
 * @brief  初始化xls文件，加载语言字符串至内存
 * @return {char ***} NULL：初始化失败  buffer地址：初始化成功，返回动态申请的三维字符数组
 * @param  {int} sheet_num excel表格中的工作表序号，从零开始
 *******************************************************************/
char ***lang_xls_init(int sheet_num)
{
    xlsWorkBook *pWb  = NULL;
    xlsWorkSheet *pWs = NULL;

    pWb = xls_open(XLS_MNT_TF_PATH, CODE);

    if (NULL == pWb)
    {
        // LOG_RED(XLS_TMP_PATH " File open error!\n");
        return NULL;
    }
    

    pWs = xls_getWorkSheet(pWb, sheet_num); // pWs指向第 sheet_num 个 Sheet
    xls_parseWorkSheet(pWs);

    xls_info.row_total = pWs->rows.lastrow + 1;
    xls_info.col_total = 1;    // 根据语言数量获取列数        //pWs->rows.lastcol;

    printf("===========================>>> 行数: %d   列数: %d \n", xls_info.row_total, xls_info.col_total);

    buffer = (char ***)malloc(sizeof(char ***) * xls_info.row_total);
    if (buffer == NULL)
        return NULL;

    for (int i = 0; i < xls_info.row_total; i++)
    {
        buffer[i] = (char **)malloc(sizeof(char **) * xls_info.col_total);
        if (buffer[i] == NULL)
            return NULL;
        for (int j = 0; j < xls_info.col_total; j++)
        {
            if ((&(pWs->rows.row[i]))->cells.cell[j].str == NULL)
            {
                buffer[i][j] = (char *)malloc(5);
                if (buffer[i][j] == NULL)
                    return NULL;

                sprintf(buffer[i][j], "NULL");
                xls_info.xls_null_str_num++;
            }
            else
            {
                buffer[i][j] = (char *)malloc(strlen((char *)(&(pWs->rows.row[i]))->cells.cell[j].str) + 1);
                if (buffer[i][j] == NULL)
                    return NULL;

                strcpy(buffer[i][j], (char *)((&(pWs->rows.row[i]))->cells.cell[j].str));
            }
            printf("%s\t", (char *)((&(pWs->rows.row[i]))->cells.cell[j].str));
        }
        printf("\n");
    }

    xls_close_WS(pWs);
    xls_close_WB(pWb);

    xls_info.xls_is_exist = true;

    return buffer;
}

/*******************************************************************
 * @brief  获取xls文件状态，查看文件 是否存在 并 初始化成功
 * @return {bool} false：文件不存在  true：文件存在
 *******************************************************************/
bool lang_xls_file_state_get(void)
{
    return xls_info.xls_is_exist;
}

/*******************************************************************
 * @brief  获取xls文件中 在有效行列中 含有的空字符数量
 * @return {int}
 *******************************************************************/
int lang_xls_null_str_num_get(void)
{
    return xls_info.xls_null_str_num;
}

/*******************************************************************
 * @brief  获取xls文件中语言的数量（即列数）
 * @return {int}
 *******************************************************************/
int lang_xls_language_num_get(void)
{
    return xls_info.col_total;
}

/*******************************************************************
 * @brief  获取xls文件中单个字符串的数量（即行数）
 * @return {int}
 *******************************************************************/
int lang_xls_str_num_get(void)
{
    return xls_info.row_total;
}



/* 初始化 */
bool init_language_xls_info(void)
{
    if(lang_xls_init(0) == NULL)
    {
        printf("lan init err..\n");
        language_xls_init_state = false;
        return false;
    }
    if (lang_xls_file_state_get() == false)
	{
		printf("No language file,use default language\n ");
        return false;
	}


    if(lang_xls_null_str_num_get() > 0)
    {
        printf("lan get NULL %d \n", lang_xls_null_str_num_get());
    }


    if(lang_xls_str_num_get() == 0)
    {
        printf("xls lan get str[] != LANGUAGE_STRING_TOTAL[] \n");
        return false;
    }

    if(lang_xls_language_num_get() == 0)
    {
        printf("The number of language is zero,use default language\n ");
        return false;
    }
    

    // LOG_B_GREEN("language xls init success \n");
    language_xls_init_state = true;
    return true;
}


/*******************************************************************
 * @brief  获取xls文件中指定单元格的字符串
 * @return {char *}
 * @param  {int} str_num 字符串的序号（行号） 从0起
 * @param  {int} lang_type 字符串的语言类型（列号） 从0起
 *******************************************************************/
char *lang_xls_str_get(int str_num, int lang_type)
{
    if (buffer[str_num][lang_type] == NULL)
    {
        printf("============[%s]======[%d]\n",__func__,__LINE__);
        return "NULL";
    }
        

    return buffer[str_num][lang_type];
}




bool is_language_xls_inited()
{
    return language_xls_init_state;
}
