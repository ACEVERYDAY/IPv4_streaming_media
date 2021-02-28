#ifndef PROTO_H__
#define PROTO_H__

#include "site_type.h"

#define DEFAULT_MGROUP     "224.2.2.2"         /*multi-broadcast group IP address*/
#define DEFAULT_RCVPORT    "8080"			   /*port of the recver*/

#define CHNNR              100				   /*channel number*/
#define LISTCHNID          0				   /*the ID which is used to send channel-list */
#define MINCHNID       	   1				   /*minimum channel ID*/
#define MAXCHNID	       (MINCHNID+CHNNR-1)  /*maximum channel ID*/

#define MSG_CHANNEL_MAX    (65536-20-8)
#define MAX_DATA		   (MSG_CHANNEL_MAX-sizeof(chnid_t))

#define MSG_LIST_MAX	   (65536-20-8)
#define MAX_ENTRY		   (MSG_LIST_MAX-sizeof(chnid_t))

struct msg_channel_st  		/*The type of packet that the channel sends out per second*/
{
	chnid_t chnid; 		    /*must between [MINCHNID, MAXCHNID]*/
	uint8_t data[1];
}__attribute__((packed));

struct msg_listentry_st		/*containing the describe of each program*/
{
	chnid_t chnid;
	uint16_t len;           /*the 'len' represents the whole length of */
	uint8_t desc[1];
}__attribute__((packed));

struct msg_list_st
{
	chnid_t chnid;			/*must be LISTCHNID*/
	struct msg_listentry_st entry[1];

}__attribute__((packed));

#endif
