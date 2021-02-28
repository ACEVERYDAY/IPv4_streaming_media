#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "medialib.h"
#include "server_conf.h"
#include "mytbf.h"

#define PATHSIZE     1024
#define LINEBUFSIZE  1024
#define MP3_BITRATE  64*1024 //correct bps:128*1024

/*存放数据：存放打开媒体库的所有资源信息*/

struct channel_context_st
{
    chnid_t chnid;    /*channel id*/
    char *desc;       /*media descrition*/
    glob_t mp3glob;   /*using to save the file name found by glob()*/
    int pos;          /*music number*/ 
    int fd;           /*file descriptor e.g. 1.mp3 2.mp3 3.mp3*/
    off_t offset;     /*offet*/
    mytbf_t *tbf;     /*using to do flow-control*/
};

static struct channel_context_st channel[MAXCHNID+1];


static struct channel_context_st *path2entry(const char* path)
{
    // path/desc.text  path/*.mp3
    syslog(LOG_INFO, "current path :%s\n", path);
    char pathstr[PATHSIZE] = {'\0'};
    char linebuf[LINEBUFSIZE];
    FILE *fp;
    struct channel_context_st *me;
    static chnid_t curr_id = MINCHNID;
    strcat(pathstr, path);
    strcat(pathstr, "/desc.txt");
    fp = fopen(pathstr, "r");
    syslog(LOG_INFO, "channel dir:%s\n", pathstr);
    if(fp == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(can't find desc.txt)", path);
        return NULL;
    }
    if(fgets(linebuf, LINEBUFSIZE, fp) == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(cant get the desc.text)", path);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    me = malloc(sizeof(*me));
    if(me == NULL)
    {
        syslog(LOG_ERR, "malloc():%s", strerror(errno));
        return NULL;
    }

    me->tbf = mytbf_init(MP3_BITRATE/8*3, MP3_BITRATE/8*10*3);
    if(me->tbf == NULL)
    {
        syslog(LOG_ERR, "mytbf_init():%s", strerror(errno));
        free(me);
        return NULL;
    }
    me->desc = strdup(linebuf);
    strncpy(pathstr, path, PATHSIZE);
    strncat(pathstr, "/*.mp3", PATHSIZE);
    if(glob(pathstr, 0, NULL, &me->mp3glob) != 0)
    {
        curr_id++;
        syslog(LOG_ERR, "%s is not a channel dir(can not find mp3 files", path);
        free(me);
        return NULL;
    }
    me->pos = 0;
    me->offset = 0;
    me->fd = open(me->mp3glob.gl_pathv[me->pos], O_RDONLY);
    if(me->fd < 0)
    {
        syslog(LOG_WARNING, "%s open failed.",me->mp3glob.gl_pathv[me->pos]);
        free(me);
        return NULL;
    }
    me->chnid = curr_id;
    curr_id++;
    return me;
}


int mlib_getchnlist(struct mlib_listentry_st **result, int *resnum)
{
    int num = 0;
    char path[PATHSIZE];
    glob_t globres;

    struct mlib_listentry_st *ptr;
    struct channel_context_st *res;

    for(int i = MINCHNID; i < MAXCHNID + 1; i++)
    {
        channel[i].chnid = -1;
    }

    snprintf(path, PATHSIZE, "%s/*", server_conf.media_dir);

    if(glob(path, 0, NULL, &globres) != 0)
    {
        return -1;
    }
    ptr = malloc(sizeof(struct mlib_listentry_st) * globres.gl_pathc);
    if(ptr == NULL)
    {
        syslog(LOG_ERR, "malloc() failed.");
        exit(1);
    }
    for(int i = 0; i < globres.gl_pathc; i++)
    {
        // globres.gl_pathv[i] -> "/var/media/ch1"
        res = path2entry(globres.gl_pathv[i]);//path-->record
        if(res != NULL)
        {
            syslog(LOG_ERR, "path2entry() return : %d %s.", res->chnid, res->desc);
            memcpy(channel+res->chnid, res, sizeof(*res));
            ptr[num].chnid = res->chnid;
            ptr[num].desc = strdup(res->desc);
            num++;
        }
    }
    *result = realloc(ptr, sizeof(struct mlib_listentry_st) * num);
    if(*result == NULL)
    {
        syslog(LOG_ERR, "realloc() failed.");
    }    
    *resnum = num;
    return 0;
}

int mlib_freechnlist(struct mlib_listentry_st *ptr)
{
    free(ptr);
    return 0;
}

static int open_next(chnid_t chnid)
{
    for(int i = 0; i < channel[chnid].mp3glob.gl_pathc; i++)
    {
        channel[chnid].pos++;
        if(channel[chnid].pos == channel[chnid].mp3glob.gl_pathc)
        {
            channel[chnid].pos = 0;
            break;
        }
        close(channel[chnid].fd);
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], O_RDONLY);
        if(channel[chnid].fd < 0)
        {
            syslog(LOG_WARNING, "open(%s):%s.", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
        }
        else
        {
            channel[chnid].offset = 0;
            return 0;
        }        
    }
    syslog(LOG_ERR, "None of mp3s in channel %d is available.", chnid);
}

ssize_t mlib_readchn(chnid_t chnid, void *buf, size_t size)
{
    int tbfsize, len;
    tbfsize = mytbf_fetchtoken(channel[chnid].tbf, size);

    while(1)
    {
        len = pread(channel[chnid].fd, buf, tbfsize, channel[chnid].offset);
        if(len < 0)
        {
            syslog(LOG_WARNING, "media file %s pread():%s.", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
            open_next(chnid);
        }
        else if(len == 0)
        {
            syslog(LOG_DEBUG, "media file %s is over:%s.", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
            // if(open_next(chnid) < 0)
            syslog(LOG_ERR, "channel %d: There is no successed open.", chnid);
        }
        else
        {
            channel[chnid].offset += len;
            break;
        }
    }
    if(tbfsize - len > 0)
        mytbf_returntoken(channel[chnid].tbf, tbfsize-len);
    return len;

    /*
    mytbf_fetchtoken(channel[chnid].tbf, size);
    */
}