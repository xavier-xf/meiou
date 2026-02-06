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
#define LOGO_PARTTION "logo_454x61.rgb"
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

    // src = fb_base + (y * 1024 + x + progress_width) * 3;
    // printf("progress_width = %d w = %d weights=%d,total = %d\n", progress_width, w, weights, total);
    // progress_width = w - progress_width;

    // for (i = 0; i < h; i++)
    // {
    //     for (j = 0; j < progress_width; j++)
    //     {
    //         src[i * 1024 * 3 + j * 3] = bcolor & 0xFF;
    //         src[i * 1024 * 3 + j * 3 + 1] = (bcolor >> 8) & 0xFF;
    //         src[i * 1024 * 3 + j * 3 + 2] = (bcolor >> 16) & 0xFF;
    //     }
    // }
    return 0;
}

static int upgrade_partitions_update(const struct upgrade_partitions_param *upgrade)
{
    void *fb_base = getenv_ulong("lcdloadaddr", 16, 0x83d02000);
    while(1)
    {
        upgrade_progress_bar_draw(fb_base, 0, 0, 1024, 600, 100, 100, 0x000000, 0xFFFFFF);
        mdelay(1000);
        upgrade_progress_bar_draw(fb_base, 0, 0, 1024, 600, 100, 100, 0x000000, 0x0A14FF);
        mdelay(1000);
        upgrade_progress_bar_draw(fb_base, 0, 0, 1024, 600, 100, 100, 0x000000, 0x00FD0A);
        mdelay(1000);
    }
   
    // run_command("reset", 0);
    printf("\n***need to reboot***\n");
    return 0;
}
int uboot_display(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{

    int ret = 0;
    struct upgrade_partitions_param upgrade;
    memset(&upgrade, 0, sizeof(struct upgrade_partitions_param));

    upgrade_partitions_update(&upgrade);

finish:
    return ret;
}

U_BOOT_CMD(
    uboot_display, 7, 0, uboot_display,
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