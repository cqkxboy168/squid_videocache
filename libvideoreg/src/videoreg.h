#ifndef _VIDEOREG_H
#define _VIDEOREG_H

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#define MAXFLAGNUM 999
typedef enum{
	YOUKU_VIDEO=100,
	LETV_VIDEO,
	TUDOU_VIDEO,
} website_type_t;


typedef enum{
	MP4,
	FLV,
	F4V,
	HD2,
} video_type_t;

typedef struct _videoTypeAttrs {
    const char *name;
    video_type_t type;
}videoTypeAttrs;



typedef struct _videoUrlDesc
{
	char * keywords;//the keywords of diffrent web site video url;
	unsigned keylen;//the length of keywords
	website_type_t website;//the video website
	video_type_t type;//the video type,e.g. flv
	char * other;//other info, e.g. parameter info
}videoUrlDesc;


typedef struct _videoProcessHandler
{
	int siteflag;
	int (*handler)(const char*,char*); //callback function
}videoProcessHandler;

typedef int (*pfFun)(const char*,char*);


int selectFunc(const char *url,char *id,website_type_t flag );

extern pfFun pf[MAXFLAGNUM];
#endif
