#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <getopt.h>
#include <net/if.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <signal.h>
#include <proto.h>
#include "server_conf.h"
#include "medialib.h"
#include "thr_channel.h"
#include "thr_list.h"

/*
    -M    指定多播组
    -P    指定接收端口
    -F    指定前台运行
    -D    指定媒体库位置
    -I    指定网络设备
    -H    显示帮助
*/
struct server_conf_st server_conf = {
    .rcvport = DEFAULT_RCVPORT,
    .mgroup = DEFAULT_MGROUP,
    .media_dir = DEFAULT_MEDIA_DIR,
    .runmode = RUN_DAEMON,
    .ifname = DEFAULT_IF
};

int serverfd;

struct sockaddr_in sndaddr;
static struct mlib_listentry_st *mlib_list;

static void exit_handler(void)
{
    close(serverfd);
}

static void printhelp(void)
{
    printf("-M    指定多播组\n");
    printf("-P    指定接收端口\n");
    printf("-F    指定前台运行\n");
    printf("-D    指定媒体库位置\n");
    printf("-I    指定网络设备\n");
    printf("-H    显示帮助\n");
}

static void daemon_exit(int sig)
{
    thr_list_destroy();
    thr_channel_destroyall();
    mlib_freechnlist(mlib_list);

    syslog(LOG_WARNING, "signal-%d caught, exit now.", sig);
    closelog();
    exit(0);
}

static int daemonize(void)
{
    int fd;
    pid_t pid;
    if((pid = fork()) < 0)
    {
        // perror("fork()");
        syslog(LOG_ERR, "fork():%s", strerror(errno));
        return -1;
    }
    else if(pid > 0)
    {
        exit(0);
    }

    if((fd = open("/dev/null", O_RDWR)) < 0)
    {
        // perror("open()");
        syslog(LOG_WARNING, "open():%s", strerror(errno));
        return -2;
    }
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if(fd > 2)
        close(fd);
    setsid();
    chdir("/");
    umask(0);
    return 0;
}

static int socket_init(void)
{
    struct ip_mreqn mreq;
    if((serverfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        syslog(LOG_ERR, "socket():%s", strerror(errno));
        exit(1);
    }
    atexit(exit_handler);
    inet_pton(AF_INET, server_conf.mgroup, &mreq.imr_address);
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname);
    if(setsockopt(serverfd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0)
    {
        syslog(LOG_ERR, "setsockopt(IP_MULTICAST_IF) failed.");
        exit(1);
    }
    // bind();
    sndaddr.sin_family = AF_INET;
    sndaddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET, server_conf.mgroup, &sndaddr.sin_addr);
    
    return 0;
}

int main(int argc, char *argv[])
{
    /*命令行分析*/
    int i, c;
    struct sigaction sa;

    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGQUIT);
    sigaddset(&sa.sa_mask, SIGTERM);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    openlog("netradio", LOG_PID|LOG_PERROR, LOG_DAEMON);
    while(1)
    {
        if((c = getopt(argc, argv, "M:P:FD:I:H")) < 0)
            break;
        switch(c)
        {
            case 'M':
                server_conf.mgroup = optarg;
                break; 
            case 'P':
                server_conf.rcvport = optarg;
                break; 
            case 'F':
                server_conf.runmode = RUN_FOREGROUND;
                break; 
            case 'D':
                server_conf.media_dir = optarg;
                break; 
            case 'I':
                server_conf.ifname = optarg;
                break; 
            case 'H':
                printhelp();
                break; 
            default:
                abort();
                break;        
        }
    }
    /*守护进程实现*/
    if(server_conf.runmode == RUN_DAEMON)
    {
        if(daemonize() != 0)
        {
            exit(1);
        }
    }
    else if(server_conf.runmode == RUN_FOREGROUND)
    {
        /*do nothing*/
    }
    else
    {
        // fprintf(stderr, "EINVAL\n");
        syslog(LOG_ERR, "EINVAL server_conf.runmode.");
        exit(1);
    }

    /*socket初始化*/
    socket_init();

    /*获取频道信息*/
    int list_size, err;
    err = mlib_getchnlist(&mlib_list, &list_size);
    if(err)
    {
        syslog(LOG_ERR, "mlib_getchnlist() failed.");
        exit(1);
    }

    /*创建节目单线程*/
    thr_list_create(mlib_list, list_size);

    /*创建频道线程*/
    for(i = 0; i < list_size; i++)
    {
        err = thr_channel_create(mlib_list+i);
        if(err)
        {
            fprintf(stderr, "thr_channel_create():%s\n", strerror(err));
            exit(1);
        }
        /*if error*/
    }
    
    syslog(LOG_DEBUG, "%d channel threads created.", i);

    while(1)
    {
        pause();
    }
    closelog();
    exit(0);
}