#include <common.h>
#include <command.h>
#include <s_record.h>
#include <net.h>
#include <ata.h>
#include <part.h>
#include <fat.h>
#include <fs.h>

#define UBOOT_PARTTION "u-boot.bin"
#define ENV_PARTTION "env_ak3761d_nor.img"
#define ENVBK_PARTTION "env_ak3761d_nor.img"
#define DTB_PARTTION "c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb"
#define KERNEL_PARTTION "uImage"
#define LOGO_PARTTION "logo.jpg"
#define ROOTFS_PARTTION "root.sqsh4"
#define USR_PARTTION "usr.sqsh4"
#define CONFIG_PARTTION "config.jffs2"
#define APP_PARTTION "app.sqsh4"
// #define RESOURCE_PARTTION "resource.sqsh4"
#define DATA_PARTTION "data.jffs2"
#define TUYA_PARTTION "tuya.jffs2"
// #define ASTERISK_PARTTION "asterisk.sqsh4"

#define ENV_SD_UPGRADEIMAGE "sd_uprade_image"
#define ENV_UPGRADEIMAGE_VERSION "uprade_image_version"
#define UPGRADE_SCRIPT_END_STR "\n# <- this is end of image parttion\n"
#define SD_UPGRDE_SCRIPT_BUF_SIZE (16 * 1024)
#define UPGARDE_PARTITIONS_NUM_MAX (12)

static char upgrade_type = 0;

struct upgrade_partitions_info
{
    unsigned long offset;
    unsigned long size;

    unsigned long weights;
};
struct partition_mtdparts
{
    char name[32];
    unsigned long offset;
    unsigned long size;
};

struct upgrade_partitions_param
{
    char *buffer;
    unsigned long buf_size;
    char *img_offset_base;
    char version[32];
    struct upgrade_partitions_info partitions[UPGARDE_PARTITIONS_NUM_MAX];
    struct partition_mtdparts mtdparts[UPGARDE_PARTITIONS_NUM_MAX];

    int upgrade_total;
};

typedef int (*upgrade_file_partitions_check_func)(const char *buffer, struct upgrade_partitions_info *upgrade);

static int upgarde_file_version_check(const char *buffer, char *version)
{
    char *p = strstr(buffer, "#<upgrade_bin_version=");
    if (p == NULL)
    {
        printf("file error:%s\n", buffer);
        return -1;
    }
    p += 22;
    char *e = strstr(p, ">");
    if (e == NULL)
    {
        printf("file error:%s\n", buffer);
        return -1;
    }
    strncpy(version, p, e - p);
    char *env_version = getenv(ENV_UPGRADEIMAGE_VERSION);
    printf("upgarde version:%s cur version:%s\n", version, env_version);
    if (env_version == NULL)
    {
        return 1;
    }
    return strcmp(version, env_version) ? 1 : 0;
}
static char *str_skip_sapce(const char *str)
{
    char *p = str;
    while ((p != NULL) && ((*p) == ' '))
    {
        p++;
    }
    return p;
}
static int upgarde_file_partitions_check(const char *buffer, const char *parttions_name, struct upgrade_partitions_info *info)
{
    char *line = buffer;
    char *endptr = NULL;
    char *nextline = NULL;
    char partitons_line[256] = {0};
    char *s = NULL;
    while ((nextline = strstr(line, "\n")) != NULL)
    {
        if ((nextline + 1) == NULL)
        {
            break;
        }
        nextline += 1;
        if (nextline == '\n')
        {
            line = nextline + 1;
            continue;
        }
        memset(partitons_line, 0, sizeof(partitons_line));

        strncpy(partitons_line, line, nextline - line - 1);
        if ((s = strstr(partitons_line, parttions_name)) != NULL)
        {
            s += strlen(parttions_name);
            if (s)
            {
                info->offset = simple_strtol(str_skip_sapce(s), &endptr, 10);
                s = endptr;
                info->size = simple_strtol(str_skip_sapce(s), &endptr, 10);
                printf("imgage:%s %d %d\n", parttions_name, info->offset, info->size);
            }
            break;
        }
        line = nextline + 1;
    }
    return 0;
}

static int upgarde_file_uboot_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, UBOOT_PARTTION, partition);
}
static int upgrade_file_envimg_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, ENV_PARTTION, partition);
}
static int upgrade_file_envbkimg_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, ENVBK_PARTTION, partition);
}
static int upgrade_file_dtb_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, DTB_PARTTION, partition);
}
static int upgrade_file_uImage_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, KERNEL_PARTTION, partition);
}
static int upgrade_file_logo_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, LOGO_PARTTION, partition);
}
static int upgrade_file_rootfs_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, ROOTFS_PARTTION, partition);
}
static int upgrade_file_usr_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, USR_PARTTION, partition);
}
static int upgrade_file_config_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, CONFIG_PARTTION, partition);
}
static int upgrade_file_app_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, APP_PARTTION, partition);
}
// static int upgrade_file_resource_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
// {
//     return upgarde_file_partitions_check(buffer, RESOURCE_PARTTION, partition);
// }
static int upgrade_file_data_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, DATA_PARTTION, partition);
}
static int upgrade_file_tuya_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
{
    return upgarde_file_partitions_check(buffer, TUYA_PARTTION, partition);
}
// static int upgrade_file_asterisk_partition_check(const char *buffer, struct upgrade_partitions_info *partition)
// {
//     return upgarde_file_partitions_check(buffer, ASTERISK_PARTTION, partition);
// }



static int sd_logo_upgrade_file_parttions(const char *dev, const char *part, const char *file,
                                         unsigned long long offset, int partitions_index,
                                         struct upgrade_partitions_param *upgrade)
{
    int ret = 0;
    char *argc[6];
    unsigned long actual_size = 0;
    
    // 1. 获取加载地址
    char *addr_str = getenv("loadaddr");
    if (addr_str != NULL) {
        upgrade->buffer = (char *)simple_strtoul(addr_str, NULL, 16);
    } else {
        ret = -1;
        printf("[%s:%d] not find loadaddr\n", __func__, __LINE__);
        goto finish;
    }
    
    // 2. 设置加载基地址
    if (upgrade->img_offset_base == 0) {
        upgrade->img_offset_base = upgrade->buffer;
    }
    
    char addr_str_offset[24] = {0};
    sprintf(addr_str_offset, "0x%lx", (unsigned long)upgrade->buffer + offset);
    
    // 3. 构建加载命令参数
    argc[1] = "mmc";
    argc[2] = "0";
    argc[3] = addr_str_offset;
    argc[4] = file;
    
    // 4. 清除旧的filesize环境变量
    setenv("filesize", NULL);
    
    // 5. 加载文件
    printf("Loading %s to %s\n", file, addr_str_offset);
    if (do_load(NULL, 0, 5, argc, FS_TYPE_FAT)) {
        printf("load file failed: %s\n", file);
        ret = -1;
        goto finish;
    }
    
    // 6. 获取实际加载的文件大小
    char *filesize_str = getenv("filesize");
    if (filesize_str) {
        actual_size = simple_strtoul(filesize_str, NULL, 16);
        printf("Loaded %s, actual size: 0x%lx (%lu bytes)\n", file, actual_size, actual_size);
    } else {
        printf("Warning: cannot get filesize for %s\n", file);
        // 使用分区大小作为后备
        actual_size = upgrade->mtdparts[partitions_index].size;
    }
    
    // 7. 验证文件大小
    if (actual_size == 0) {
        printf("Error: %s is empty or load failed\n", file);
        ret = -1;
        goto finish;
    }
    
    // 8. 检查文件大小是否超过分区大小
    unsigned long partition_size = upgrade->mtdparts[partitions_index].size;
    if (actual_size > partition_size) {
        printf("Error: %s too large: %lu > %lu\n", file, actual_size, partition_size);
        ret = -1;
        goto finish;
    }
    
    // 9. 如果Logo文件小于分区大小，需要填充
    if (actual_size < partition_size) {
        printf("Logo file smaller than partition (%lu < %lu), padding with 0xFF\n", 
               actual_size, partition_size);
        
        // 在内存中填充0xFF（Flash擦除后的状态）
        // 注意：需要确保buffer后面有足够的空间
        memset(upgrade->buffer + actual_size, 0xFF, partition_size - actual_size);
        
        printf("Padded logo data from %lu to %lu bytes\n", actual_size, partition_size);
        
        // 使用分区大小作为写入大小
        upgrade->partitions[partitions_index].size = partition_size;
    } else {
        // 正好匹配或文件更大（已在上面检查）
        upgrade->partitions[partitions_index].size = actual_size;
    }
    
    // 10. 设置升级参数
    upgrade->partitions[partitions_index].offset = (unsigned long)offset;
    upgrade->partitions[partitions_index].weights = upgrade->upgrade_total + upgrade->partitions[partitions_index].size;
    upgrade->upgrade_total += upgrade->partitions[partitions_index].size;
    
    printf("Logo upgrade prepared: partition=%d, size=%lu (partition=%lu)\n", 
           partitions_index, upgrade->partitions[partitions_index].size, partition_size);
    
finish:
    return ret;
}



static int sd_upgrade_file_parttions(const char *dev, const char *part, const char *file, struct upgrade_partitions_param *upgrade)
{
    int ret = 0;
    char version[32] = {0};
    int i = 0;
    char *argc[6];
    char *addr_str = getenv("loadaddr");
    if (addr_str != NULL)
    {
        upgrade->buffer = simple_strtoul(addr_str, NULL, 16);
    }
    else
    {
        ret = -1;
        printf("[%s:%d]not find uprade file name:%s\n", __func__, __LINE__, file);
        goto finish;
    }
    argc[1] = "mmc";
    argc[2] = "0";
    argc[3] = addr_str;
    argc[4] = file;
    argc[5] = "0x4000";
    if (do_load(NULL, 0, 6, argc, FS_TYPE_FAT))
    {
        printf("load file failed :%s\n", __func__);
        ret = -1;
        goto finish;
    }
    // 检查版本是否一致
    if (upgarde_file_version_check(upgrade->buffer, version) <= 0)
    {
        printf("file version :%s\n", version);
        ret = -1;
        goto finish;
    }
    argc[1] = "mmc";
    argc[2] = "0";
    argc[3] = addr_str;
    argc[4] = file;
    argc[5] = "0x1000000";
    do_load(NULL, 0, 6, argc, FS_TYPE_FAT);

    char *end = strstr(upgrade->buffer, UPGRADE_SCRIPT_END_STR);
    if (end == NULL)
    {
        ret = -1;
        printf("file error:%s\n", file);
        goto finish;
    }
    *end = 0;

    memset(upgrade->version, 0, sizeof(upgrade->version));
    strcpy(upgrade->version, version);

    upgrade_file_partitions_check_func partitions_check_funs[UPGARDE_PARTITIONS_NUM_MAX] =
        {
            upgarde_file_uboot_partition_check,
            upgrade_file_envimg_partition_check,
            upgrade_file_envbkimg_partition_check,
            upgrade_file_dtb_partition_check,
            upgrade_file_uImage_partition_check,
            upgrade_file_logo_partition_check,
            upgrade_file_rootfs_partition_check, 
            upgrade_file_config_partition_check,
            upgrade_file_usr_partition_check,
            upgrade_file_app_partition_check,
            // upgrade_file_resource_partition_check,
            upgrade_file_tuya_partition_check,
            upgrade_file_data_partition_check,
            /*upgrade_file_asterisk_partition_check*/};
    // 检查u-boot分区是否存在
    for (i = 0; i < UPGARDE_PARTITIONS_NUM_MAX; i++)
    {
        partitions_check_funs[i](upgrade->buffer, &upgrade->partitions[i]);

        upgrade->partitions[i].weights = upgrade->upgrade_total + upgrade->partitions[i].size;
        upgrade->upgrade_total = upgrade->partitions[i].weights;
    }
    upgrade->img_offset_base = end + strlen(UPGRADE_SCRIPT_END_STR);
    // printf("img offsetbase:%lu \n", upgrade->img_offset_base - upgrade->buffer);
finish:
    return ret;
}

static int partition_string_parse(const char *input, struct partition_mtdparts *partition)
{
    char *p = NULL;
    // Parse partition name
    const char *openParen = strchr(input, '(');
    if (openParen == NULL)
    {
        printf("[%s:%d] failed\n", __func__, __LINE__);
        return -1;
    }
    const char *closeParen = strchr(openParen, ')');
    if (closeParen == NULL)
    {
        printf("[%s:%d] failed\n", __func__, __LINE__);
        return -1;
    }
    strncpy(partition->name, openParen + 1, closeParen - input - 2);

    // Parse size and offset
    partition->offset = 0;
    const char *at = strchr(input, '@');
    if (at == NULL)
    {
        printf("[%s:%d] failed\n", __func__, __LINE__);
        return -1;
    }
    partition->offset = 0;
    for (p = at + 3; p < openParen; ++p)
    {
        if (*p >= '0' && *p <= '9')
        {
            partition->offset = partition->offset * 16 + (*p - '0');
        }
        else if (*p >= 'a' && *p <= 'f')
        {
            partition->offset = partition->offset * 16 + (*p - 'a' + 10);
        }
        else if (*p >= 'A' && *p <= 'F')
        {
            partition->offset = partition->offset * 16 + (*p - 'A' + 10);
        }
        else
        {
            break;
        }
    }

    // Manually convert offset from hex string to unsigned long
    partition->size = 0;
    for (p = input; p < at; ++p)
    {
        if (*p >= '0' && *p <= '9')
        {
            partition->size = partition->size * 10 + (*p - '0');
        }
        else
        {
            break;
        }
    }
    partition->size *= 1024;
    printf("[%s] %d %d \n", partition->name, partition->offset, partition->size);
}
static int partitions_mtdparts_parse(const char *mtdparts, struct upgrade_partitions_param *upgrade)
{
    char *parts = strstr(mtdparts, ":");
    if (parts == NULL)
    {
        printf("[%s:%d] failed\n", __func__, __LINE__);
        return -1;
    }

    char *token = strtok(parts + 1, ",");
    if (token == NULL)
    {
        printf("[%s:%d] failed\n", __func__, __LINE__);
        return -1;
    }
    int index = 0;
    while (token != NULL)
    {
        // Parse each token to extract name, size, and offset
        partition_string_parse(token, &upgrade->mtdparts[index]);

        // Move to the next token
        token = strtok(NULL, ",");
        index++;
    }
    return 0;
}

static int partitions_mtdparts_parse_by_sdcard(const char *dev, const char *part, const char *file, struct upgrade_partitions_param *upgrade)
{

    char *env_addres = upgrade->img_offset_base + upgrade->partitions[1].offset;
    char *s = NULL;
    char *e = NULL;
    int ret = 0;
    char mtdparts[1024] = {0};
    int offset = 0;
    s = env_addres;
    while ((s) && (offset < upgrade->partitions[1].size))
    {
        // printf("%s\n",s);
        e = strstr(s, "mtdparts=");
        if (e == NULL)
        {
            offset += strlen(s) + 1;
            s = env_addres + offset;
        }
        else
        {
            break;
        }
    }
    strcpy(mtdparts, s);
    ret = partitions_mtdparts_parse(mtdparts, upgrade);
finish:
    return ret;
}
static int partitions_mtdparts_parse_by_envimg(struct upgrade_partitions_param *upgrade)
{
    char mtdparts[1024] = {0};
    strcpy(mtdparts, getenv("mtdparts"));
    return partitions_mtdparts_parse(mtdparts, upgrade);
}

static int upgrade_partitions_mtdparts(const char *dev, const char *part, const char *file, struct upgrade_partitions_param *upgrade)
{
    if (upgrade->partitions[1].size != 0)
    {
        return partitions_mtdparts_parse_by_sdcard(dev, part, file, upgrade);
    }

    return partitions_mtdparts_parse_by_envimg(upgrade);
}

static int upgrade_progress_bar_draw(void *fb_base, int x, int y, int w, int h, unsigned long weights, unsigned long total, unsigned int fcolor, unsigned int bcolor)
{

    int i, j;
    if (total == 0)
    {
        return 0;
    }
    char *src = fb_base + (y * 1024 + x) * 3;
    unsigned long progress_width = weights * w / total;
    if (weights != 0)
    {
        for (i = 0; i < h; i++)
        {
            for (j = 0; j < progress_width; j++)
            {
                src[i * 1024 * 3 + j * 3] = fcolor & 0xFF;
                src[i * 1024 * 3 + j * 3 + 1] = (fcolor >> 8) & 0xFF;
                src[i * 1024 * 3 + j * 3 + 2] = (fcolor >> 16) & 0xFF;
            }
        }
    }
    if (weights == total)
    {
        return 0;
    }

    src = fb_base + (y * 1024 + x + progress_width) * 3;
    printf("progress_width = %d w = %d weights=%d,total = %d\n", progress_width, w, weights, total);
    progress_width = w - progress_width;

    for (i = 0; i < h; i++)
    {
        for (j = 0; j < progress_width; j++)
        {
            src[i * 1024 * 3 + j * 3] = bcolor & 0xFF;
            src[i * 1024 * 3 + j * 3 + 1] = (bcolor >> 8) & 0xFF;
            src[i * 1024 * 3 + j * 3 + 2] = (bcolor >> 16) & 0xFF;
        }
    }
    return 0;
}

static int upgrade_partitions_update(const struct upgrade_partitions_param *upgrade)
{
    int i = 0, j = 0;
    int ret = 0;
    long offset = 0;
    char cmd[256] = {0};
    void *fb_base = getenv_ulong("lcdloadaddr", 16, 0x83d02000);
    // printf(" \n\n\n   gd->fb_base:0x%x\n", fb_base);
    upgrade_progress_bar_draw(fb_base, 312, 505, 400, 10, 0, 100, 0xFFFFFF, 0xFFFFFF);
    run_command("sf probe", 0);
    for (i = 0; i < UPGARDE_PARTITIONS_NUM_MAX; ++i)
    {
        if (upgrade->partitions[i].size > upgrade->mtdparts[i].size)
        {
            printf("[%s:%d] %s: imge file size(%d) > env partitions(%d)\n", __func__, __LINE__, upgrade->mtdparts[i].name, upgrade->partitions[i].size, upgrade->mtdparts[i].size);
            continue;
        }

        if (upgrade->partitions[i].size != 0)
        {
            printf("\nErase:%s addres:%d,partition size:%d image file size:%d\n", upgrade->mtdparts[i].name, upgrade->mtdparts[i].offset, upgrade->mtdparts[i].size, upgrade->partitions[i].size);
            memset(cmd, 0, sizeof(cmd));
            sprintf(cmd, "sf erase 0x%x 0x%x", upgrade->mtdparts[i].offset, upgrade->mtdparts[i].size);
            run_command(cmd, 0);
            upgrade_progress_bar_draw(fb_base, 312, 505, 400, 10, upgrade->partitions[i].weights - upgrade->partitions[i].size / 2, upgrade->upgrade_total, 0xff9326, 0xFFFFFF);
            sprintf(cmd, "sf write 0x%x 0x%x 0x%x", upgrade->img_offset_base + upgrade->partitions[i].offset, upgrade->mtdparts[i].offset, upgrade->partitions[i].size);
            run_command(cmd, 0);
            upgrade_progress_bar_draw(fb_base, 312, 505, 400, 10, upgrade->partitions[i].weights, upgrade->upgrade_total, 0xff9326, 0xFFFFFF);
        }
    }
    /*如果没有更新env.img则需要保存版本号*/
    if ((upgrade->partitions[1].size == 0) && (upgrade->partitions[2].size == 0)&& (upgrade_type == 0))
    {
        setenv(ENV_UPGRADEIMAGE_VERSION, upgrade->version);
        saveenv();
    }
    printf("\nparttion update finish\n");
    for (i = 0; i < 10; i++)
    {
        upgrade_progress_bar_draw(fb_base, 312, 505, 400, 10, 100, 100, 0xff9326, 0xFFFFFF);
        mdelay(500);
        upgrade_progress_bar_draw(fb_base, 312, 505, 400, 10, 100, 100, 0x000000, 0xFFFFFF);
        mdelay(500);
    }
    run_command("reset", 0);
    return 0;
}
int do_sd_upgrade(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
    int ret = 0;
    struct upgrade_partitions_param upgrade;
    char *upgrade_filename = getenv(ENV_SD_UPGRADEIMAGE);
    if (upgrade_filename == NULL)
    {
        upgrade_filename = "ME_AHD_ANYKA.IMG";
        setenv(ENV_SD_UPGRADEIMAGE, upgrade_filename);
        saveenv();
    }
    upgrade_type = 0;
    memset(&upgrade, 0, sizeof(struct upgrade_partitions_param));
    if (sd_upgrade_file_parttions("mmc", "0", upgrade_filename, &upgrade) < 0)
    {
        upgrade_partitions_mtdparts("mmc", "0", upgrade_filename, &upgrade);
        upgrade_filename = "logo.jpg";
        if (sd_logo_upgrade_file_parttions("mmc", "0", upgrade_filename,0,5,&upgrade) >=0)
        {       
            upgrade_type |= 0x01;
        }
        else
        {
            printf("[%s:%d] sd_upgrade_file_parttions failed\n", __func__, __LINE__);
            ret = 0;
            goto finish;
        }
        
        
    }
    else
    {
        upgrade_partitions_mtdparts("mmc", "0", upgrade_filename, &upgrade);
    }
    

    upgrade_partitions_update(&upgrade);

finish:
    return ret;
}

U_BOOT_CMD(
    sd_upgrade, 7, 0, do_sd_upgrade,
    "upgrade file from a dos filesystem",
    "<interface> [<dev[:part]>]  <addr> <filename> [bytes [pos]]\n"
    "    - Load binary file 'filename' from 'dev' on 'interface'\n"
    "      to address 'addr' from dos filesystem.\n"
    "      'pos' gives the file position to start loading from.\n"
    "      If 'pos' is omitted, 0 is used. 'pos' requires 'bytes'.\n"
    "      'bytes' gives the size to load. If 'bytes' is 0 or omitted,\n"
    "      the load stops on end of file.\n"
    "      If either 'pos' or 'bytes' are not aligned to\n"
    "      ARCH_DMA_MINALIGN then a misaligned buffer warning will\n"
    "      be printed and performance will suffer for the load.");