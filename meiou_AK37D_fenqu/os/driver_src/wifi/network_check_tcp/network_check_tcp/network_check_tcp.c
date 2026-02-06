#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/select.h>
#include <signal.h>

#define BUFFER_SIZE_MAX (1024 * 1024)

static int server_sockfd = -1;
static int client_sockfd = -1;
static uint32_t test_type = 0;
static char server_ip[32] = {0};
static int server_port = 8080;
static int packet_delay = 25;
static int packet_min = 1024;
static int packet_max = 1024 * 50;
static int packet_type = 0;
static char buffer[BUFFER_SIZE_MAX];
uint64_t received_bytes = 0;
uint64_t sent_bytes = 0;
unsigned long long durations = 0;

static unsigned long long user_timestamp_get(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void usage(const char *name)
{
    printf("Usage: %s -c 192.168.0.1\n", name);
    printf("     : %s -s\n", name);
    printf("-s:服务器\n");
    printf("        tcp服务端\n");
    printf("-c:客户端\n");
    printf("        tcp客户端，需要指定服务器IP，如：-c 192.168.0.1\n");
    printf("-d:发包延时\n");
    printf("        发一个包后延时时长，单位ms，默认值：25\n");
    printf("-l:最小包\n");
    printf("        包大小最小值，客户端有效，默认值：1KB\n");
    printf("-b:最大包\n");
    printf("        包大小最大值，客户端有效，默认值：50KB\n");
    printf("-t:包类型\n");
    printf("        0 - 固定包，每个包的大小都是最大值\n");
    printf("        1 - 随机包，根据指定范围随机产生包大小\n");
    printf("        def - 固定包\n");
    printf("-p:端口号\n");
    printf("        指定端口号，默认8080\n");
    printf("-h:显示帮助信息\n");
}

static void parse_option(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            switch (argv[i][1])
            {
            case 'h': // help
                usage(argv[0]);
                exit(0);
            case 's':
                test_type = 0;
                break;
            case 'c':
                test_type = 1;
                strcpy(server_ip, argv[i + 1]);
                break;
            case 'd':
                packet_delay = atoi(argv[i + 1]);
                break;
            case 'l':
                packet_min = 1024 * atoi(argv[i + 1]);
                break;
            case 'b':
                packet_max = 1024 * atoi(argv[i + 1]);
                break;
            case 't':
                packet_type = atoi(argv[i + 1]);
                break;
            case 'p':
                server_port = atoi(argv[i + 1]);
                break;
            default:
                usage(argv[0]);
                exit(0);
            }
        }
    }
}

static void wifi_ssid_and_signal_level_print(void)
{
    char buffer[1024] = {0};
    char ssid[128] = {0};
    int level = 0;

    if (access("/sys/class/net/wlan0", F_OK) != 0)
    {
        return;
    }

    FILE *fp = popen("wpa_cli -i wlan0 -p /var/run/wpa_supplicant status", "r");
    if (fp == NULL)
    {
        printf("popen wpa_cli -i wlan0 status failed \n");
        return;
    }
    while (fgets(buffer, sizeof(buffer), fp) > 0)
    {
        if (strncmp(buffer, "ssid=", 5) == 0)
        {
            char *p = strchr(buffer, '=') + 1;
            strcpy(ssid, p);
            ssid[strlen(ssid) - 1] = '\0';
        }
    }
    pclose(fp);

    if (strlen(ssid) == 0)
        return;

    system("wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan > /dev/null");
    usleep(1000 * 200);
    system("wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan > /dev/null");
    usleep(1000 * 200);
    system("wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan > /dev/null");
    usleep(1000 * 200);
    system("wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan > /dev/null");
    usleep(1000 * 200);
    system("wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan > /dev/null");
    usleep(1000 * 200);

    sprintf(buffer, "wpa_cli -i wlan0 -p /var/run/wpa_supplicant scan_result | grep %s", ssid);
    fp = popen(buffer, "r");
    if (fp == NULL)
    {
        printf("popen wpa_cli scan_result \n\r");
        return;
    }
    memset(buffer, 0, sizeof(buffer));

    int line_index = 0;
    while (fgets(buffer, sizeof(buffer), fp) > 0)
    {
        if (line_index++ != 0)
        {
            break;
        }
        char c_buf[128] = {0};
        int i_buf = 0;
        sscanf(buffer, "%s	%d	%d	%s	%s", c_buf, &i_buf, &level, c_buf, c_buf);
    }
    pclose(fp);

    printf("WIFI SSID:[%s] 信号强度:[%d db]\n", ssid, level);
}

static void transmission_data_statistics(void)
{
    durations = user_timestamp_get() - durations;
    printf("\n发送包总数: %.2f MB \n接收包总数: %.2f MB \n时长: %.2f秒 \n发送速率: %.2f MB/s \n接收速率: %.2f MB/s \n",
           (double)sent_bytes / (1024 * 1024),
           (double)received_bytes / (1024 * 1024),
           (double)durations / 1000,
           (double)sent_bytes / (1024 * 1024) / ((double)durations / 1000),
           (double)received_bytes / (1024 * 1024) / ((double)durations / 1000));
    wifi_ssid_and_signal_level_print();
}

static void *receive_thread(void *arg)
{
    int *sockfd_ptr = (int *)arg;
    int client_sockfd = *sockfd_ptr;
    uint64_t received = 0;
    unsigned long long timestamp = 0;
    bool first = true;

    while (1)
    {
        int valread = read(client_sockfd, buffer, BUFFER_SIZE_MAX);
        if (valread <= 0)
        {
            // 连接关闭或发生错误
            transmission_data_statistics();
            break;
        }
        if (first)
        {
            first = false;
            durations = user_timestamp_get();
        }
        received += valread;
        received_bytes += valread;
        if (user_timestamp_get() - timestamp >= 1000)
        {
            printf("\r接收: %.2f MB 速率: %.2f MB/s ", (double)received_bytes / (1024 * 1024), (double)received / (1024 * 1024) / ((double)(user_timestamp_get() - timestamp) / 1000));
            fflush(stdout);
            timestamp = user_timestamp_get();
            received = 0;
        }
    }

    pthread_exit(NULL);
}

static void *send_thread(void *arg)
{
    int *sockfd_ptr = (int *)arg;
    int client_sockfd = *sockfd_ptr;

    while (1)
    {
        int send_len = packet_type == 0 ? packet_max : (packet_min + rand() % (packet_max - packet_min + 1));
        int valread = send(client_sockfd, buffer, send_len, 0);
        if (valread < 0)
        {
            transmission_data_statistics();
            break;
        }
        sent_bytes += valread;
        if (packet_delay > 0)
        {
            usleep(packet_delay * 1000);
        }
    }

    pthread_exit(NULL);
}

static void server_tcp_test(void)
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    pthread_t receive_thread_id, send_thread_id;

    // 创建套接字
    if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 初始化服务器地址结构
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    // 绑定套接字到端口
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 监听连接
    if (listen(server_sockfd, 3) < 0)
    {
        perror("listen failed");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 接受客户端连接
    if ((client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen)) < 0)
    {
        perror("accept failed");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 关闭监听套接字，因为我们已经接受了一个连接
    close(server_sockfd);

    // 创建接收和发送线程
    if (pthread_create(&receive_thread_id, NULL, receive_thread, (void *)&client_sockfd) < 0)
    {
        perror("could not create receiving thread");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&send_thread_id, NULL, send_thread, (void *)&client_sockfd) < 0)
    {
        perror("could not create sending thread");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // 等待线程结束（实际使用中可能需要更复杂的线程管理）
    pthread_join(receive_thread_id, NULL);
    pthread_join(send_thread_id, NULL);

    // 关闭套接字
    close(client_sockfd);
}

static void client_tcp_test(void)
{
    struct sockaddr_in serv_addr;
    pthread_t receive_thread_id, send_thread_id;

    // 创建套接字
    if ((client_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);

    // 将服务器地址转换为二进制形式
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // 连接到服务器
    if (connect(client_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // 创建接收和发送线程
    if (pthread_create(&receive_thread_id, NULL, receive_thread, (void *)&client_sockfd) < 0)
    {
        perror("could not create receiving thread");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&send_thread_id, NULL, send_thread, (void *)&client_sockfd) < 0)
    {
        perror("could not create sending thread");
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // 等待线程结束（在实际应用中，可能需要更复杂的线程管理）
    pthread_join(receive_thread_id, NULL);
    pthread_join(send_thread_id, NULL);

    // 关闭套接字
    close(client_sockfd);
}

static void _finish(int signal)
{
    transmission_data_statistics();
    exit(0);
}

int main(int argc, char *argv[])
{
    signal(SIGTERM, _finish); // 终止信号Termination Signal
    signal(SIGINT, _finish);  // 中断信号Interrupt Signal
    if (argc == 1)
    {
        usage(argv[0]);
        exit(0);
    }
    parse_option(argc, argv);
    srand(time(0));
    if (test_type == 0)
    {
        printf("TCP双向传输数据包测试-服务端\n");
        printf("端口号:%d\n", server_port);
        server_tcp_test();
    }
    else
    {
        printf("TCP双向传输数据包测试-客户端\n");
        printf("服务器IP: %s \n", server_ip);
        printf("发包延时: %d ms\n", packet_delay);
        printf("最小包: %d KB\n", packet_min / 1024);
        printf("最大包: %d KB\n", packet_max / 1024);
        printf("包类型: %s \n", packet_type == 0 ? "固定包" : "随机包");
        printf("端口号: %d \n", server_port);
        client_tcp_test();
    }
    return 0;
}
