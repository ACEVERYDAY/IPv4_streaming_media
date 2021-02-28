#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>


#include "client.h"
#include <proto.h>

struct client_conf_st client_conf = {
    .rcvport = DEFAULT_RCVPORT,
    .mgroup = DEFAULT_MGROUP,
    .player_cmd = DEFAULT_PLATERCMD
};

static int sd;

static void hook_handler(void)
{
    close(sd);
}

static void printhelp(void)
{
    printf("-M  --mgroup  指定多播组\n");
    printf("-P  --port    指定接收端口\n");
    printf("-p  --player  指定播放器\n");
    printf("-H  --help    显示帮助\n");
}

static ssize_t writen(int fd, const char *buf, size_t len)
{
    int ret, pos = 0;
    while(len > 0)
    {
        ret = write(fd, buf+pos, len);
        if(ret < 0)
        {
            if(errno == EINTR)
                continue;
            perror("write()");
            return -1;
        }
        len -= ret;
        pos += ret;
    }
    return pos;
}

int main(int argc, char *argv[])
{
    int c, index = 0, val, len;
    int pipefd[2];
    socklen_t serveraddr_len, raddr_len;
    pid_t pid;
    struct option argarr[] = {{"port,1,NULL",'P'}, {"mgroup,1,NULL",'M'},
                            {"player,1,NULL",'p'}, {"help,0,NULL",'H'}, {"NULL,0,NULL",0}};
    struct ip_mreqn mreq;
    struct sockaddr_in laddr, serveraddr, raddr;
    struct msg_channel_st *msg_channel;
    /*初始化
        级别：默认值，配置文件，环境变量，命令行参数
    */
    while(1)
    {
        if((c = getopt_long(argc, argv, "P:M:p:H", argarr, &index)) < 0)
            break;
        switch (c)
        {
            case 'P':
                client_conf.rcvport = optarg;
                break;
            case 'M':
                client_conf.mgroup = optarg;
                break;        
            case 'p':
                client_conf.player_cmd = optarg;
                break;
            case 'H':
                printhelp();
                exit(0);
            default:
                abort();
                break;
        }
    }
    
    if((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket()");
        exit(1);
    }
    atexit(hook_handler);

    inet_pton(AF_INET, client_conf.mgroup, &mreq.imr_multiaddr);
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex("ens33");
    
    if(setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("setsockopt()");
        exit(1);
    }
    val = 1;
    if(setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)) < 0)
    {
        perror("setsockopt()");
        exit(1);
    }   

    inet_pton(AF_INET, "0.0.0.0", &laddr.sin_addr);
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(atoi(client_conf.rcvport));
    
    if(bind(sd, (void *)&laddr, sizeof(laddr)) < 0)
    {
        perror("bind()");
        exit(1);
    }

    if(pipe(pipefd) < 0)
    {
        perror("pipe()");
        exit(0);
    }

    if((pid = fork()) < 0)
    {
        perror("fork()");
        exit(1);
    }
    else if(pid == 0)
    {
        // 子进程调用解码器
        close(sd);
        close(pipefd[1]);
        dup2(pipefd[0], 0);
        if(pipefd[0] > 0)
        {
            close(pipefd[0]);
        }
        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL);
        perror("execl()");
        exit(1);
    }
    else
    {
        // 父进程收包并发送给子进程
        // 收节目单->选择频道->收频道包并发送给子进程
        struct msg_list_st *msg_list;
        msg_list = malloc(MSG_LIST_MAX);
        if(msg_list == NULL)
        {
            perror("malloc()");
            exit(1);
        }
        while(1)
        {
            len = recvfrom(sd, msg_list, MSG_LIST_MAX, 0, (void *)&serveraddr, &serveraddr_len);
            if(len < sizeof(struct msg_list_st))
            {
                fprintf(stderr, "message is too small.\n");
                continue;
            }
            if(msg_list->chnid != LISTCHNID)
            {
                fprintf(stderr, "channel id %d is not match.\n", msg_list->chnid);
                continue;
            }
            break;
        }
        // 打印节目单并选择频道
        struct msg_listentry_st *pos;
        for(pos = msg_list->entry; (char *)pos < ((char *)msg_list + len); pos = (void *)((char *)pos)+ ntohs(pos->len))
        {
            printf("channel %d:%s\n", pos->chnid, pos->desc);
        }
        /*case1: 节目单
          case2: 指定的频道包
        */
        free(msg_list);
        int chosenid, ret;
        while(ret < 1)
        {
            ret = scanf("%d", &chosenid);
            if(ret != 1)
                exit(1);
        }
        // 收频道包并发送给子进程
        fprintf(stdout, "chosenid = %d\n", ret);

        msg_channel = malloc(MSG_CHANNEL_MAX);
        if(msg_channel == NULL)
        {
            perror("malloc()");
            exit(1);
        }
        char ipstr_raddr[30];
        char ipstr_server_addr[30];
        raddr_len = sizeof(serveraddr);
        while(1)
        {
            len = recvfrom(sd, msg_channel, MSG_CHANNEL_MAX, 0, (void *)&raddr, &raddr_len);

            if(raddr.sin_addr.s_addr != serveraddr.sin_addr.s_addr)
            {
                inet_ntop(AF_INET, &raddr.sin_addr.s_addr, ipstr_raddr, 30);
                inet_ntop(AF_INET, &serveraddr.sin_addr.s_addr, ipstr_server_addr, 30);
                fprintf(stderr, "Ignore:addr not match. raddr:%s server_addr:%s.\n", ipstr_raddr, ipstr_server_addr);
                continue;
            }
            if(  (raddr.sin_port != serveraddr.sin_port))
            {
                continue;
            }
            if(len < sizeof(struct msg_channel_st))
            {
                fprintf(stderr, "Ignore:message too small.\n");
                continue;                
            }
            if(msg_channel->chnid == chosenid)
            {
                fprintf(stdout, "accept mesage:%d received.\n", msg_channel->chnid);
                if(writen(pipefd[1], msg_channel->data, len-sizeof(chnid_t)) < 0)
                    exit(1);
            }
            
        }
    }

    free(msg_channel);
    /*
    getopt();
    socket();
    setsockopt();
    bind();
    pipe();
    fork(); // 子进程调用解码器，父进程从网络收包并发送给子进程while(1)
    */

    exit(0);
}