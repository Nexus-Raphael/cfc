#ifndef API_H
#define API_H

char *getusr(const char *handle);//用户信息
char *getcontests();//比赛列表
char *paticf(const char *handle);//参赛记录
char *getconteststandings(const char *handle, int contestId);//比赛信息
char* httpget(const char* url);//url
#endif