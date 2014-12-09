#include "videoreg.h"


static videoTypeAttrs videoTypeArr[] =
{
	  {".mp4",MP4},
	  {".flv",FLV},
	  {".f4v",F4V},
	  {".hd2",HD2},
};

static int youkuExtractID(const char *url,char *id)
{
    char *youkubegin;
    char *youkuIDstart=NULL;
    char *youkuIDend=NULL;
    if( strstr(url,"/youku/")  &&  strstr(url,".flv?nk=") )/*有?nk=参数的，目前无法缓存*/
    	return 0;
    if( strstr(url,"/youku/")  &&  strstr(url,".mp4?nk=") )/*有?nk=参数的，目前无法缓存*/
    	return 0;
    if ( (youkubegin=strstr(url,"/youku/"))  && (  (youkuIDend= strstr(url,".flv") ) || (youkuIDend=strstr(url,".mp4") ) ))
    {
    	  //debug(20, 8) ("i am in youkuIDstart: \n");
    	  printf("find video url\n");
    	  youkuIDstart=youkubegin+7;
    	  while(  (*youkuIDstart++) != '/' && *youkuIDstart !='\0') ;//匹配下一个/
        if(youkuIDend+4-youkuIDstart>1024) 
    	  	return 0;
        strncpy(id,youkuIDstart,youkuIDend+4-youkuIDstart);//4是视频后缀
        id[youkuIDend+5-youkuIDstart]='\0';
        //debug(20, 8) ("youkuExtractID: %s \n",id);
        return 1;
    }
    return 0;  
}

static int letvExtractID(const char *url,char *id)
{
    char *letvIDstart=NULL;
    char *letvIDend=NULL;

    if ( strstr(url,"/letv-uts/")  && (letvIDstart=strstr(url,"ver_")) && (letvIDend=strstr(url,".ts")) )
     {
        strncpy(id,letvIDstart,letvIDend-letvIDstart+3);//
        id[letvIDend+4-letvIDstart]='\0';
       //debug(20, 8) ("letvExtractID: %s \n",id);
       return 1;
     }
     return 0;
}

pfFun pf[MAXFLAGNUM]={youkuExtractID,letvExtractID};/*A function pointer array,all video processing function register here*/

/*driver table,according to flag select related func*/
int selectFunc(const char *url,char *id,website_type_t flag )
{
	int ret=0;
	if(flag<100 || flag >999)
		return 0;
	if( pf[flag-100] !=NULL) 
	{
	   ret=pf[flag-100](url,id);
	}
	else{
		printf("siteflag:%d not register\n",flag);
	}
	return ret;
}