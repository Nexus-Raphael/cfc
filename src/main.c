#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include <cJSON.h>
#include <time.h>
#include <locale.h>
#include <stdbool.h>
#include<curl/curl.h>

#if defined(_WIN32) || defined(_WIN64)
    #define localtime_r(ts, tm) localtime_s((tm), (ts))
    #include <windows.h>
    #define SLEEP(seconds) Sleep((seconds) * 1000)
#else
    #include <unistd.h>
    #define SLEEP(seconds) sleep(seconds)
#endif
#define MAX_PROBLEMS 32

#define MAX_NAME_LEN 128

#define MAX_CATEGORY_NAME 16

#define MAX_CONTESTS 128

int pre;
char handle[30];


void open(const char *url) {
    printf("打开网页：%s\n", url);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "start %s", url);
    system(cmd);
}
typedef struct {
    char index[4];
    char name[128];
    int accepted;
    int submitted;
    double passrate;

    double maxsco;
    double minsco;
    double avesco;
    double variance;

    bool hassco;
} probstats;

typedef struct {
    int contestId;
    char name[128];
    char cate[16];
    int probcnt;
    probstats problems[32];
} contstats;

int anacontprob(int contestId, const char* cate, contstats* outstats) {
    char url[256];
    snprintf(url, sizeof(url),"https://codeforces.com/api/contest.standings?contestId=%d&showUnofficial=true",contestId);
    char* resp=httpget(url);
    if(!resp){
        fprintf(stderr,"拉取比赛%d失败\n",contestId);
        return -1;
    }

    cJSON* root=cJSON_Parse(resp);
    free(resp);
    if(!root){
        fprintf(stderr,"解析比赛%d的JSON失败\n",contestId);
        return -1;
    }
    cJSON* result=cJSON_GetObjectItem(root,"result");
    cJSON* problems=cJSON_GetObjectItem(result,"problems");
    cJSON* rows=cJSON_GetObjectItem(result,"rows");
    if(!cJSON_IsArray(problems)||!cJSON_IsArray(rows)){
        fprintf(stderr,"ID为%d的比赛API错误\n",contestId);
        cJSON_Delete(root);
        return -1;
    }

    outstats->contestId=contestId;
    strncpy(outstats->cate,cate,sizeof(outstats->cate)-1);
    snprintf(outstats->name,MAX_NAME_LEN,"Contest %d",contestId);
    cJSON* contestInfo=cJSON_GetObjectItem(result,"contest");
    if(contestInfo){
        cJSON* cname=cJSON_GetObjectItem(contestInfo,"name");
        if (cJSON_IsString(cname)) strncpy(outstats->name,cname->valuestring,MAX_NAME_LEN-1);
    }

    int pcount=cJSON_GetArraySize(problems);
    outstats->probcnt=pcount;

    for (int i=0;i<pcount;++i) {
        probstats* ps=&outstats->problems[i];
        memset(ps,0,sizeof(*ps));
        cJSON* prob=cJSON_GetArrayItem(problems,i);
        cJSON* idx=cJSON_GetObjectItem(prob,"index");
        cJSON* pname=cJSON_GetObjectItem(prob,"name");
        cJSON* pp=cJSON_GetObjectItem(prob,"points");
        if(cJSON_IsString(idx)) strncpy(ps->index,idx->valuestring,sizeof(ps->index)-1);
        if(cJSON_IsString(pname)) strncpy(ps->name,pname->valuestring,sizeof(ps->name)-1);
        if(cJSON_IsNumber(pp)){
            ps->hassco=1;
            ps->maxsco=pp->valuedouble;
            ps->minsco=pp->valuedouble;
        } else {
            ps->hassco=0;
            ps->maxsco=0;
            ps->minsco=0;
        }
        ps->accepted=ps->submitted=0;
        ps->avesco=ps->variance=0.0;
    }

    int rcount=cJSON_GetArraySize(rows);
    for (int r=0;r<rcount;++r) {
        cJSON* row=cJSON_GetArrayItem(rows, r);
        cJSON* results=cJSON_GetObjectItem(row,"problemresults");
        if (!cJSON_IsArray(results)) continue;
        for (int i = 0; i < pcount; ++i) {
            probstats* ps = &outstats->problems[i];
            cJSON* pr = cJSON_GetArrayItem(results, i);
            double score=cJSON_GetObjectItem(pr,"points")->valuedouble;
            int attempts=cJSON_GetObjectItem(pr,"rejectedAttemptCount")->valueint;

            if (attempts>0||score>0.0) {
                ps->submitted++;
            }
            if (score > 0.0) {
                ps->accepted++;
            }
            if (ps->hassco) {
                if(score>ps->maxsco) ps->maxsco=score;
                if(score<ps->minsco) ps->minsco=score;
                ps->avesco+=score;
                ps->variance+=score*score;
            }
        }
    }

    for (int i=0;i<pcount;++i) {
        probstats* ps=&outstats->problems[i];
        if (ps->submitted>0)
            ps->passrate=(double)ps->accepted/ps->submitted;
        if (ps->hassco&&ps->submitted>0){
            double sum=ps->avesco;
            ps->avesco=sum/ps->submitted;
            double meanSq=ps->variance/ps->submitted;
            ps->variance=meanSq-ps->avesco*ps->avesco;
        }
    }

    cJSON_Delete(root);
    return 0;
}

void anaprob(void) {
    const char* datasrc="web/data.json";
    FILE* fp=fopen(datasrc,"r");//从web/data.json获取要解析的比赛列表
    if(!fp){
        fprintf(stderr,"无法打开%s\n",datasrc);//判断能否打开
        return;
    }
    fseek(fp,0,SEEK_END);
    long fsize=ftell(fp);
    fseek(fp,0,SEEK_SET);
    char* text=malloc(fsize+1);
    if (!text){ fclose(fp);return;}
    fread(text,1,fsize,fp);
    text[fsize]='\0';
    fclose(fp);

    cJSON* root=cJSON_Parse(text);
    free(text);
    if(!root){
        fprintf(stderr,"无法解析data.json\n");//判断文件里面的JSON能不能解析
        return;
    }

    const char* cate[]={"Div1","Div2","Div3","Div4","Educational"};//分成五类存
    size_t catcnt=sizeof(cate)/sizeof(cate[0]);

    for (size_t ci=0;ci<catcnt;++ci){
        const char* cat=cate[ci];
        cJSON* list=cJSON_GetObjectItem(root, cat);
        if (!cJSON_IsArray(list)) continue;

        cJSON* outArr=cJSON_CreateArray();

        cJSON* item;
        cJSON_ArrayForEach(item,list){
            cJSON* cidjson=cJSON_GetObjectItem(item,"id");
            if (!cJSON_IsNumber(cidjson)) continue;
            int contestId=cidjson->valueint;

            contstats stats;
            memset(&stats,0,sizeof(stats));
            if(anacontprob(contestId,cat,&stats)!=0){//调用anacontprob(),返回0表示比赛可解析
                continue;//不能解析就跳过
            }

            cJSON* cobj=cJSON_CreateObject();//进行比赛解析
            cJSON_AddNumberToObject(cobj,"contestId",stats.contestId);
            cJSON_AddStringToObject(cobj,"contestName",stats.name);
            cJSON_AddStringToObject(cobj,"category",stats.cate);
            cJSON* parr=cJSON_CreateArray();
            for (int i=0;i<stats.probcnt;++i) {
                probstats* ps=&stats.problems[i];
                cJSON* pobj=cJSON_CreateObject();
                cJSON_AddStringToObject(pobj,"index",ps->index);
                cJSON_AddStringToObject(pobj,"name",ps->name);
                cJSON_AddNumberToObject(pobj,"submitted",ps->submitted);
                cJSON_AddNumberToObject(pobj,"accepted",ps->accepted);
                cJSON_AddNumberToObject(pobj,"passRate",ps->passrate);
                if (ps->hassco) {//题目有具体分数,比如Div1/2的,就详细记录
                    cJSON_AddNumberToObject(pobj,"minScore",ps->minsco);
                    cJSON_AddNumberToObject(pobj,"maxScore",ps->maxsco);
                    cJSON_AddNumberToObject(pobj,"averageScore",ps->avesco);
                    cJSON_AddNumberToObject(pobj,"scoreVariance",ps->variance);
                }
                cJSON_AddItemToArray(parr, pobj);
            }
            cJSON_AddItemToObject(cobj,"problems",parr);
            cJSON_AddItemToArray(outArr,cobj);
        }

        char outpath[128];
        snprintf(outpath,sizeof(outpath),"web/%sstats.json",cat);//写入5个文件
        char* outtext=cJSON_Print(outArr);
        FILE* ofp=fopen(outpath,"w");
        if(ofp){//判断是否写入成功
            fprintf(ofp,"%s\n",outtext);
            fclose(ofp);
            printf("写入 %s\n",outpath);
        } else {
            fprintf(stderr, "无法写入%s\n",outpath);
        }
        free(outtext);
        cJSON_Delete(outArr);
    }

    cJSON_Delete(root);//释放
    int ret = system("scp web/Div1stats.json web/Div2stats.json web/Div3stats.json web/Div4stats.json web/Educationalstats.json web/anaprob.html root@121.196.195.201:/usr/share/nginx/html/web/");
    if (ret != 0) fprintf(stderr, "文件上传失败\n");
    open("http://121.196.195.201/web/anaprob.html");
}
typedef struct {
    int year;
    int month;
    int total;     // 有效比赛总数
    int attended;  // 用户参加的有效比赛数
} attendance;


const char* gettitle(int rating) {
    if (rating < 1200) return "Newbie";
    if (rating < 1400) return "Pupil";
    if (rating < 1600) return "Specialist";
    if (rating < 1900) return "Expert";
    if (rating < 2100) return "Candidate Master";
    if (rating < 2300) return "Master";
    if (rating < 2400) return "International Master";
    if (rating < 2600) return "Grandmaster";
    if (rating < 3000) return "International Grandmaster";
    return "Legendary Grandmaster";
}



time_t upt(time_t original) {

    struct tm *tminfo = localtime(&original);

    tminfo->tm_mday -= 1;

    tminfo->tm_hour = 22;
    tminfo->tm_min = 35;
    tminfo->tm_sec = 0;

    time_t new = mktime(tminfo);

    return new;
}

//选手端---查询个人信息
void oneinfo(){
    char *json=getusr(handle);//调用API函数获取字符串JSON
    if (!json){
        fprintf(stderr,"用户信息获取失败\n");
        return;
    }
    cJSON *root = cJSON_Parse(json);//解析JSON字符串
    if (!root) {
        fprintf(stderr, "JSON解析失败%s\n", cJSON_GetErrorPtr());
        free(json);
        return;
    }
    free(json);

    cJSON *status = cJSON_GetObjectItem(root, "status");//判断API返回的状态是不是OK
    if (!cJSON_IsString(status)||strcmp(status->valuestring,"OK")!=0){
        fprintf(stderr, "API返回状态异常。\n");
        cJSON_Delete(root);
        return;
    }
    cJSON *resarr=cJSON_GetObjectItem(root, "result");//找指定用户，看是否存在
    cJSON *user=cJSON_GetArrayItem(resarr, 0);//API本身支持找多个，但是这里找一个即可
    if(!user){
        fprintf(stderr,"用户数据不存在\n");
        cJSON_Delete(root);
        return;
    }//解析JSON数据里面的信息，并输出到终端
    int rating=cJSON_GetObjectItem(user,"rating")->valueint;
    int maxrating=cJSON_GetObjectItem(user,"maxRating")->valueint;
    const char *rank=cJSON_GetObjectItem(user,"rank")->valuestring;
    printf("Handle: %s\n", handle);
    printf("Rating: %d (max %d)\n", rating, maxrating);
    printf("Rank: %s\n", rank);
    cJSON_Delete(root);
    return;
}

//选手端---查询年度比赛列表（已废弃）
// void yearlyCFlists0000(int pre) {//已废弃，查询指定年要在函数里改
//     int choseny=2025;
//     setlocale(LC_TIME,"");
//
//     char *json=getcontests();
//     if (!json){
//         fprintf(stderr,"获取比赛列表失败。\n");
//         return;
//     }
//     cJSON *root=cJSON_Parse(json);
//     free(json);
//     if (!root){
//         fprintf(stderr,"JSON 解析失败 %s\n",cJSON_GetErrorPtr());
//         return;
//     }
//     cJSON *status=cJSON_GetObjectItem(root,"status");
//     if (!cJSON_IsString(status)||strcmp(status->valuestring,"OK")!=0) {
//         fprintf(stderr,"API返回状态异常。\n");
//         cJSON_Delete(root);
//         return;
//     }
//     cJSON *result=cJSON_GetObjectItem(root, "result");
//     if (!cJSON_IsArray(result)) {
//         fprintf(stderr,"比赛列表格式异常。\n");
//         cJSON_Delete(root);
//         return;
//     }
//     cJSON *out=cJSON_CreateObject();
//     cJSON *div1arr=cJSON_CreateArray();
//     cJSON *div2arr=cJSON_CreateArray();
//     cJSON *div3arr=cJSON_CreateArray();
//     cJSON *div4arr=cJSON_CreateArray();
//     cJSON *eduarr=cJSON_CreateArray();
//
//     int cnt=cJSON_GetArraySize(result);
//     for (int i=0;i<cnt;++i) {
//         cJSON *ct=cJSON_GetArrayItem(result, i);
//         if (!ct) continue;
//         cJSON *phase = cJSON_GetObjectItem(ct, "phase");
//         if (!cJSON_IsString(phase) || strcmp(phase->valuestring, "FINISHED") != 0) {
//             continue;
//         }
//         cJSON *start=cJSON_GetObjectItem(ct,"startTimeSeconds");
//         time_t st=(time_t)start->valuedouble;
//         struct tm *t=localtime(&st);
//         if ((t->tm_year+1900)!=choseny) {
//             break;
//         }
//
//         cJSON *item=cJSON_CreateObject();
//         cJSON_AddNumberToObject(item,"id",cJSON_GetObjectItem(ct,"id")->valueint);
//         cJSON_AddStringToObject(item,"name",cJSON_GetObjectItem(ct,"name")->valuestring);
//         char buf[64];
//         strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",t);
//         cJSON_AddStringToObject(item,"startTime",buf);
//
//         // 判断是Educational还是Div.
//         const char *nm=cJSON_GetObjectItem(ct, "name")->valuestring;
//         char *p=strstr(nm,"(Div. ");
//         if (p){
//             int divnum = 0;
//             if (sscanf(p, "(Div. %d)", &divnum) == 1){
//                 switch (divnum) {
//                     case 1: cJSON_AddItemToArray(div1arr, item);break;
//                     case 2: cJSON_AddItemToArray(div2arr, item);break;
//                     case 3: cJSON_AddItemToArray(div3arr, item);break;
//                     case 4: cJSON_AddItemToArray(div4arr, item);break;
//                     default: cJSON_Delete(item); break;
//                 }
//             } else {
//                 cJSON_Delete(item);
//             }
//         } else if (strstr(nm, "Educational") != NULL) {
//             cJSON_AddItemToArray(eduarr, item);
//         } else {
//             cJSON_Delete(item);
//         }
//     }
//
//     cJSON_AddItemToObject(out, "Div1", div1arr);
//     cJSON_AddItemToObject(out, "Div2", div2arr);
//     cJSON_AddItemToObject(out, "Div3", div3arr);
//     cJSON_AddItemToObject(out, "Div4", div4arr);
//     cJSON_AddItemToObject(out, "Educational", eduarr);
//
//     char *outs=cJSON_Print(out);
//     FILE *fp=fopen("web/data.json","w");
//     if (fp){
//         fprintf(fp,"%s",outs);
//         fclose(fp);
//     } else {
//         perror("无法打开 web/data.json");
//     }
//     free(outs);
//     cJSON_Delete(out);
//     cJSON_Delete(root);
//
//     printf("%d年的数据已经生成到web/data.json\n", choseny);
//     if(pre) open("http://121.196.195.201/web/etest.html");
//     else return;
// }

//选手端---查询指定时间比赛列表
void cfbytime(int pre) {
    int choseny;
    int chosenm;
    setlocale(LC_TIME, "");//用户输入日期(年与月)

    printf("请输入年份: ");
    if (scanf("%d", &choseny) != 1) {
        fprintf(stderr, "无效的年份输入。\n");//输入无效就打印信息并退出
        return;
    }
    printf("请输入月份(1-12,0表示全年): ");
    if (scanf("%d", &chosenm) != 1||chosenm<0||chosenm>12){
        fprintf(stderr,"无效的月份输入。\n");
        return;
    }

    char *json=getcontests();//判断json字符串是否返回成功
    if (!json){
        fprintf(stderr,"获取比赛列表失败。\n");
        return;
    }

    cJSON *root=cJSON_Parse(json);//解析JSON字符串并判断是否成功
    free(json);
    if (!root){
        fprintf(stderr,"JSON解析失败：%s\n",cJSON_GetErrorPtr());
        return;
    }
    cJSON *status=cJSON_GetObjectItem(root,"status");//判断得到的JSON状态是不是OK
    if (!cJSON_IsString(status) || strcmp(status->valuestring,"OK")!=0) {
        fprintf(stderr, "API返回状态异常。\n");
        cJSON_Delete(root);
        return;
    }
    cJSON *result=cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {//判断JSON是不是比赛的数组
        fprintf(stderr,"比赛列表格式异常。\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *out=cJSON_CreateObject();
    cJSON *div1arr=cJSON_CreateArray();//创建5个类,把比赛数据对应存入
    cJSON *div2arr=cJSON_CreateArray();
    cJSON *div3arr=cJSON_CreateArray();
    cJSON *div4arr=cJSON_CreateArray();
    cJSON *eduarr=cJSON_CreateArray();

    int cnt=cJSON_GetArraySize(result);
    for (int i = 0; i < cnt; ++i) {
        cJSON *ct=cJSON_GetArrayItem(result,i);
        if (!ct) continue;
        cJSON *phase=cJSON_GetObjectItem(ct, "phase");//只处理已经结束的比赛
        if (!cJSON_IsString(phase)||strcmp(phase->valuestring,"FINISHED")!=0) continue;

        time_t st=(time_t)cJSON_GetObjectItem(ct, "startTimeSeconds")->valuedouble;
        struct tm t;//把比赛时间转成结构体时间,用来筛指定时间段的比赛
        localtime_r(&st,&t);
        int y=t.tm_year+1900;
        int m=t.tm_mon+1;
        if (y!=choseny) continue;
        if (chosenm!=0&&m!=chosenm) continue;

        cJSON *item=cJSON_CreateObject();//构造比赛项
        cJSON_AddNumberToObject(item,"id",cJSON_GetObjectItem(ct, "id")->valueint);
        cJSON_AddStringToObject(item,"name",cJSON_GetObjectItem(ct, "name")->valuestring);
        char buf[64];//添加格式化后的时间
        strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&t);
        cJSON_AddStringToObject(item,"startTime",buf);

        const char *nm=cJSON_GetObjectItem(ct,"name")->valuestring;
        char *p=strstr(nm,"(Div. ");
        if (p){//判断比赛类型并分类存入数组,其他比赛不处理
            int divnum=0;
            if (sscanf(p,"(Div. %d)",&divnum) == 1) {
                switch (divnum) {
                    case 1: cJSON_AddItemToArray(div1arr, item); break;
                    case 2: cJSON_AddItemToArray(div2arr, item); break;
                    case 3: cJSON_AddItemToArray(div3arr, item); break;
                    case 4: cJSON_AddItemToArray(div4arr, item); break;
                    default: cJSON_Delete(item); break;
                }
            }else{
                cJSON_Delete(item);
            }
        } else if(strstr(nm, "Educational")!=NULL) {
            cJSON_AddItemToArray(eduarr, item);
        } else {
            cJSON_Delete(item);
        }
    }
//写入对象
    cJSON_AddItemToObject(out,"Div1",div1arr);
    cJSON_AddItemToObject(out,"Div2",div2arr);
    cJSON_AddItemToObject(out,"Div3",div3arr);
    cJSON_AddItemToObject(out,"Div4",div4arr);
    cJSON_AddItemToObject(out,"Educational",eduarr);

    char *outs=cJSON_Print(out);
    FILE *fp=fopen("web/data.json","w");
    if(fp){
        fprintf(fp,"%s",outs);
        fclose(fp);
        printf("%d年%s比赛分组数据已写入web/data.json。\n", choseny,chosenm==0?"全年":(char[]){0});
    } else {
        perror("无法打开web/data.json");
    }
    free(outs);
    cJSON_Delete(out);
    cJSON_Delete(root);

    if (pre) {
        int ret = system("scp web/data.json web/etest.html root@121.196.195.201:/usr/share/nginx/html/web/");
        if (ret != 0) fprintf(stderr, "文件上传失败\n");
        open("http://121.196.195.201/web/etest.html");
    }
}

//选手端---查询参与比赛列表
void paticflist() {
    char *json = paticf(handle);//通过paticf调用API返回数据
    if (!json) {
        fprintf(stderr, "获取用户参加过的比赛列表失败。\n");
        return;
    }

    cJSON *root=cJSON_Parse(json);//解析JSON字符串
    free(json);
    if (!root){
        fprintf(stderr,"JSON解析失败：%s\n",cJSON_GetErrorPtr());
        return;
    }

    cJSON *status=cJSON_GetObjectItem(root,"status");//判断请求状态是不是OK
    if (!cJSON_IsString(status)||strcmp(status->valuestring,"OK")!=0){
        fprintf(stderr,"API返回状态异常。\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *result=cJSON_GetObjectItem(root,"result");//判断数据个数是不是比赛数组
    if (!cJSON_IsArray(result)){
        fprintf(stderr,"比赛列表格式异常。\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *out=cJSON_CreateObject();
    //创建5个类,把比赛数据对应存入
    cJSON *div1arr=cJSON_CreateArray();
    cJSON *div2arr=cJSON_CreateArray();
    cJSON *div3arr=cJSON_CreateArray();
    cJSON *div4arr=cJSON_CreateArray();
    cJSON *eduarr=cJSON_CreateArray();

    int cnt=cJSON_GetArraySize(result);
    for (int i=0;i<cnt;++i) {
        cJSON *entry=cJSON_GetArrayItem(result, i);
        if (!entry) continue;

        int cid=cJSON_GetObjectItem(entry,"contestId")->valueint;
        const char *cname=cJSON_GetObjectItem(entry,"contestName")->valuestring;
        time_t rating_time=(time_t)cJSON_GetObjectItem(entry,"ratingUpdateTimeSeconds")->valueint;
        rating_time=upt(rating_time);//需要转换为UTC+8前一天的22：35
        struct tm *t=localtime(&rating_time);

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", cid);//构造比赛对象
        cJSON_AddStringToObject(item, "name", cname);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);//格式化时间
        cJSON_AddStringToObject(item, "starttime", buf);

        const char *nm=cname;
        char *p=strstr(nm,"(Div. ");
        if (p){
            int divnum=0;
            if (sscanf(p,"(Div. %d)",&divnum)==1) {
                switch(divnum){//判断比赛类型并分类存入数组,其他比赛不处理
                    case 1: cJSON_AddItemToArray(div1arr,item); break;
                    case 2: cJSON_AddItemToArray(div2arr,item); break;
                    case 3: cJSON_AddItemToArray(div3arr,item); break;
                    case 4: cJSON_AddItemToArray(div4arr,item); break;
                    default: cJSON_Delete(item); break;
                }
            }else{
                cJSON_Delete(item);
            }
        }else if(strstr(nm, "Educational") != NULL) {
            cJSON_AddItemToArray(eduarr, item);
        }else{
            cJSON_Delete(item);
        }
    }

    cJSON_AddItemToObject(out,"Div1",div1arr);//解析后写入对象
    cJSON_AddItemToObject(out,"Div2",div2arr);
    cJSON_AddItemToObject(out,"Div3",div3arr);
    cJSON_AddItemToObject(out,"Div4",div4arr);
    cJSON_AddItemToObject(out,"Educational",eduarr);

    char *outs = cJSON_Print(out);
    FILE *fp = fopen("web/usrdata.json", "w");//写入.json
    if (fp) {
        fprintf(fp, "%s", outs);
        fclose(fp);
    } else {
        perror("无法打开web/usrdata.json");
    }

    free(outs);
    cJSON_Delete(out);
    cJSON_Delete(root);

    printf("该用户参与的比赛分组数据已写入web/usrdata.json。\n");//上传服务器并打开网页
    int ret = system("scp web/usrdata.json web/ucflist.html root@121.196.195.201:/usr/share/nginx/html/web/");
    if (ret != 0) fprintf(stderr, "文件上传失败\n");
    open("http://121.196.195.201/web/ucflist.html");
}

//判断一个contest是否应改被统计
static int contval(const cJSON *contest,time_t now) {
    cJSON *cj_phase=cJSON_GetObjectItem(contest, "phase");
    if (!cj_phase||!cJSON_IsString(cj_phase)||strcmp(cj_phase->valuestring,"FINISHED")!=0){
        return 0;
    }
    cJSON *cj_ts=cJSON_GetObjectItem(contest,"startTimeSeconds");
    if (!cj_ts||!cJSON_IsNumber(cj_ts)||(time_t)cj_ts->valuedouble>now){
        return 0;
    }
    cJSON *cj_name=cJSON_GetObjectItem(contest,"name");
    if (!cj_name||!cJSON_IsString(cj_name)) return 0;
    const char *name=cj_name->valuestring;
    if(strstr(name,"Rated for Div.")||strstr(name,"Educational Codeforces Round")){
        return 1;
    }
    if (strstr(name,"Div.")&&!strstr(name,"Unrated")) {
        return 1;
    }
    return 0;
}

void atrate(void) {
    char *contjs=getcontests();
    if (!contjs){fprintf(stderr,"比赛列表获取失败\n"); return; }//获取总比赛列表
    char *userjs=paticf(handle);
    if (!userjs){fprintf(stderr, "用户比赛记录获取失败\n"); free(contjs); return; }//获取用户参赛列表

    cJSON *cjcont=cJSON_Parse(contjs);
    cJSON *cjusr=cJSON_Parse(userjs);
    free(contjs);
    free(userjs);
    if (!cjcont||!cjusr){fprintf(stderr,"JSON解析失败\n"); goto cleanup; }//解析JSON时的调试信息
    
    cJSON *resA=cJSON_GetObjectItem(cjcont,"result");
    cJSON *resB=cJSON_GetObjectItem(cjusr,"result");
    if (!cJSON_IsArray(resA)||!cJSON_IsArray(resB)) {
        fprintf(stderr,"JSON格式错误\n");
        goto cleanup;
    }

    cJSON *firrating=cJSON_GetArrayItem(resB,0);//获取第一次参赛数据,以便算出有效出勤率
    int firstcid=cJSON_GetObjectItem(firrating,"contestId")->valueint;
    time_t firts=0;
    cJSON *contest;
    cJSON_ArrayForEach(contest,resA){
        cJSON *cjid=cJSON_GetObjectItem(contest,"id");
        if (cjid&&cjid->valueint==firstcid) {
            firts=(time_t)cJSON_GetObjectItem(contest,"startTimeSeconds")->valuedouble;
            break;
        }
    }
    if (firts==0){
        fprintf(stderr,"列表里找不到第一场比赛\n");
        goto cleanup;
    }
    struct tm firtm;
    localtime_r(&firts,&firtm);
    int firy=firtm.tm_year+1900;//格式化为结构体时间
    int firm=firtm.tm_mon+1;

    time_t now=time(NULL);
    cJSON *attendance=cJSON_CreateObject();
    cJSON *seenRounds=cJSON_CreateObject();

    cJSON_ArrayForEach(contest,resA){
        cJSON *cjts2=cJSON_GetObjectItem(contest,"startTimeSeconds");//统计比赛列表中的有效比赛,通过时间key存储
        if (!cjts2||!cJSON_IsNumber(cjts2)) continue;
        time_t ts=(time_t)cjts2->valuedouble;
        struct tm tm_st;
        localtime_r(&ts,&tm_st);
        int y=tm_st.tm_year+1900;
        int m=tm_st.tm_mon+1;
        if (y < firy || (y == firy && m < firm)) continue;
        if (!contval(contest, now)) continue;

        const char *name=cJSON_GetObjectItem(contest, "name")->valuestring;
        const char *p=strchr(name, '(');
        char baseName[64]={0};
        if(p){
            size_t len=p-name;
            if (len>=sizeof(baseName)) len=sizeof(baseName)-1;
            strncpy(baseName,name,len);
            baseName[len]='\0';
        } else {
            strncpy(baseName,name,sizeof(baseName)-1);
        }
        if (cJSON_GetObjectItem(seenRounds,baseName)) continue;
        cJSON_AddTrueToObject(seenRounds,baseName);

        char monthKey[8];
        snprintf(monthKey,sizeof(monthKey), "%04d-%02d", y, m);
        cJSON *mo=cJSON_GetObjectItem(attendance, monthKey);
        if(!mo){
            mo=cJSON_CreateObject();
            cJSON_AddNumberToObject(mo,"total",1);
            cJSON_AddNumberToObject(mo,"attended",0);
            cJSON_AddItemToObject(attendance,monthKey,mo);
        } else {
            cJSON_GetObjectItem(mo,"total")->valuedouble+=1;
        }
    }

    cJSON *rating;
    cJSON_ArrayForEach(rating,resB){
        int cid=cJSON_GetObjectItem(rating, "contestId")->valueint;//统计参赛列表,通过时间key存储
        cJSON *found=NULL;
        cJSON_ArrayForEach(contest,resA) {
            cJSON *cjid=cJSON_GetObjectItem(contest,"id");
            if (cjid&&cjid->valueint==cid) {found=contest;break;}
        }
        if(!found) continue;
        if(!contval(found,now)) continue;//统计参加的有效的比赛
        time_t ts2; 
        struct tm tm2;
        ts2=(time_t)cJSON_GetObjectItem(found,"startTimeSeconds")->valuedouble;
        localtime_r(&ts2,&tm2);
        char monthKey[8];
        snprintf(monthKey,sizeof(monthKey),"%04d-%02d",tm2.tm_year+1900, tm2.tm_mon+1);
        cJSON *mo=cJSON_GetObjectItem(attendance,monthKey);
        if (mo) cJSON_GetObjectItem(mo, "attended")->valuedouble += 1;
    }

    cJSON *out=cJSON_CreateObject();
    cJSON *monthObj;
    cJSON_ArrayForEach(monthObj, attendance) {//根据每个月进行出勤率统计
        const char *key=monthObj->string;
        double total=cJSON_GetObjectItem(monthObj, "total")->valuedouble;
        double attended=cJSON_GetObjectItem(monthObj, "attended")->valuedouble;
        cJSON *entry=cJSON_CreateObject();
        cJSON_AddNumberToObject(entry,"total",total);
        cJSON_AddNumberToObject(entry,"attended",attended);
        cJSON_AddNumberToObject(entry,"rate",total>0?attended/total:0.0);
        cJSON_AddItemToObject(out,key,entry);
    }

    char *outs=cJSON_PrintUnformatted(out);//统计的结果写入web/attendance.json
    FILE *fp=fopen("web/attendance.json", "w");
    if (fp) {fprintf(fp,"%s\n",outs);fclose(fp);printf("attendance.json 已生成\n"); }
    else    {fprintf(stderr,"无法打开web/attendance.json\n"); }
    free(outs);

    cJSON_Delete(out);
    cJSON_Delete(attendance);
    cJSON_Delete(seenRounds);

cleanup:
    cJSON_Delete(cjcont);
    cJSON_Delete(cjusr);//上传服务器并打开HTML
    int ret = system("scp web/attendance.json web/atrate.html root@121.196.195.201:/usr/share/nginx/html/web/");
    if (ret != 0) fprintf(stderr, "文件上传失败\n");
    open("http://121.196.195.201/web/atrate.html");

}

//选手端---用户成长曲线
void userevo() {
    char *jsonlist=paticf(handle);
    if (!jsonlist) {
        fprintf(stderr,"获取用户 %s 的比赛列表失败。\n", handle);
        return;
    }

    cJSON *root = cJSON_Parse(jsonlist);
    free(jsonlist);
    if (!root) {
        fprintf(stderr,"JSON解析失败：%s\n",cJSON_GetErrorPtr());
        return;
    }

    cJSON *status=cJSON_GetObjectItem(root,"status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring,"OK")!=0) {
        fprintf(stderr,"API返回状态异常\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *result=cJSON_GetObjectItem(root,"result");
    if (!cJSON_IsArray(result)) {
        fprintf(stderr,"比赛列表格式异常。\n");
        cJSON_Delete(root);
        return;
    }

    int count=cJSON_GetArraySize(result);
    if (count==0){
        fprintf(stderr,"用户%s未参加任何比赛或用户名无效！\n",handle);
        cJSON_Delete(root);
        return;
    }

    cJSON *outarr=cJSON_CreateArray();
    printf("找到 %d 场参赛记录\n", count);

    for (int i=0;i<count;i++){
        cJSON *entry=cJSON_GetArrayItem(result,i);
        if (!entry) continue;

        int contestId=cJSON_GetObjectItem(entry,"contestId")->valueint;
        const char *contestName=cJSON_GetObjectItem(entry,"contestName")->valuestring;
        int oldRating=cJSON_GetObjectItem(entry,"oldRating")?cJSON_GetObjectItem(entry,"oldRating")->valueint:0;
        int newRating=cJSON_GetObjectItem(entry,"newRating")?cJSON_GetObjectItem(entry,"newRating")->valueint:0;
        int ratingUpdateTime=0;
        cJSON *updtime=cJSON_GetObjectItem(entry,"ratingUpdateTimeSeconds");
        if (updtime&&cJSON_IsNumber(updtime)) {
            ratingUpdateTime = updtime->valueint;
        }

        printf("处理第 %d/%d 场比赛：%s (ID=%d)\n",i+1,count,contestName,contestId);

        char *json_standings=getconteststandings(handle,contestId);
        if (!json_standings) {
            fprintf(stderr, "获取比赛%d的详细数据失败，跳过...\n", contestId);
            continue;
        }

        cJSON *standings_root = cJSON_Parse(json_standings);
        free(json_standings);
        if (!standings_root) {
            fprintf(stderr, "比赛%dJSON解析失败，跳过...\n", contestId);
            continue;
        }

        cJSON *status2 = cJSON_GetObjectItem(standings_root, "status");
        if (!cJSON_IsString(status2) || strcmp(status2->valuestring, "OK") != 0) {
            fprintf(stderr, "比赛 %d API返回状态异常，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *res2 = cJSON_GetObjectItem(standings_root,"result");
        if (!res2) {
            fprintf(stderr, "比赛 %d result缺失，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *problems=cJSON_GetObjectItem(res2,"problems");
        cJSON *rows=cJSON_GetObjectItem(res2,"rows");
        if (!problems||!rows){
            fprintf(stderr,"比赛 %d 缺失 problems 或 rows，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *userrow=NULL;
        int rows_count=cJSON_GetArraySize(rows);
        for (int j=0;j<rows_count;j++) {
            cJSON *row=cJSON_GetArrayItem(rows, j);
            cJSON *party=cJSON_GetObjectItem(row, "party");
            if (!party) continue;
            cJSON *members=cJSON_GetObjectItem(party, "members");
            if (!members||cJSON_GetArraySize(members)==0) continue;

            cJSON *member0=cJSON_GetArrayItem(members,0);
            cJSON *mhandle=cJSON_GetObjectItem(member0,"handle");
            if (mhandle&&strcmp(mhandle->valuestring,handle) == 0) {
                userrow=row;
                break;
            }
        }

        if(!userrow){
            fprintf(stderr,"比赛 %d 未找到用户行，跳过...\n",contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        int rank=cJSON_GetObjectItem(userrow, "rank")->valueint;
        cJSON *problemresults=cJSON_GetObjectItem(userrow, "problemresults");
        int problemcount=cJSON_GetArraySize(problems);

        cJSON *obj=cJSON_CreateObject();
        cJSON_AddNumberToObject(obj,"contestId",contestId);
        cJSON_AddStringToObject(obj,"contestName",contestName);
        cJSON_AddNumberToObject(obj,"ratingUpdateTime",ratingUpdateTime);
        cJSON_AddNumberToObject(obj,"rank",rank);
        cJSON_AddNumberToObject(obj,"oldRating",oldRating);
        cJSON_AddNumberToObject(obj,"newRating",newRating);
        cJSON_AddStringToObject(obj,"rankBefore",gettitle(oldRating));
        cJSON_AddStringToObject(obj,"rankAfter",gettitle(newRating));

        cJSON *problems_arr=cJSON_CreateArray();
        bool issimple=strstr(contestName,"Div. 3")||strstr(contestName,"Div. 4")||strstr(contestName,"Educational");
        for (int k=0;k<problemcount;k++) {
            cJSON *problem=cJSON_GetArrayItem(problems,k);
            if (!problem) continue;

            const char *index=cJSON_GetObjectItem(problem,"index")->valuestring;

            cJSON *pr=(problemresults && k < cJSON_GetArraySize(problemresults))?cJSON_GetArrayItem(problemresults, k):NULL;

            if (issimple){
            bool accepted=false;
            if (pr) {
                cJSON *probpoints = cJSON_GetObjectItem(pr, "points");
                accepted=(probpoints && cJSON_IsNumber(probpoints) && probpoints->valuedouble > 0.0);
            }
            if(accepted){
                cJSON_AddItemToArray(problems_arr, cJSON_CreateString(index));
            }
            }else{
                cJSON *probobj=cJSON_CreateObject();
                cJSON_AddStringToObject(probobj,"index",index);

                cJSON *probpoints=cJSON_GetObjectItem(problem,"points");
                int maxPoints=(probpoints && cJSON_IsNumber(probpoints))?(int)probpoints->valuedouble:0;
                cJSON_AddNumberToObject(probobj, "maxPoints", maxPoints);

                int userpoints=0;
                if(pr){
                    cJSON *probpoints=cJSON_GetObjectItem(pr, "points");
                    if (probpoints&&cJSON_IsNumber(probpoints)) {
                        userpoints=(int)probpoints->valuedouble;
                    }
                }
                cJSON_AddNumberToObject(probobj,"points",userpoints);
                cJSON_AddItemToArray(problems_arr,probobj);
            }
        }

        cJSON_AddItemToObject(obj,"problems",problems_arr);
        cJSON_AddItemToArray(outarr,obj);

        cJSON_Delete(standings_root);
    }

    char *outs=cJSON_Print(outarr);
    FILE *fp=fopen("web/userevo.json", "w");
    if (fp) {
        fprintf(fp, "%s", outs);
        fclose(fp);
        printf("已生成用户参赛成长数据：web/userevo.json\n");
    } else {
        perror("写入文件失败");
    }

    free(outs);
    cJSON_Delete(outarr);
    cJSON_Delete(root);
    int ret = system("scp web/userevo.json web/userevo.html root@121.196.195.201:/usr/share/nginx/html/web/");
    if (ret != 0) fprintf(stderr, "文件上传失败\n");
    open("http://121.196.195.201/web/userevo.html");
}


int main(){

    printf("输入用户名(handle):\n");//输入用户名，是全局变量
    scanf("%s",handle);
    while(1){//选择功能，通过switch-case实现
        printf("请选择功能(输入数字):\n1. 查询个人信息 \n2. 查询指定时间比赛列表\n3. 查询你参加过的比赛列表\n4. 查询出勤率\n5. 查询所有比赛情况\n6. 分析本年度比赛题目\n7. 重置用户名\n8. 退出程序\n");
        int tem=0;
        scanf("%d",&tem);
        switch (tem)
        {
        case 1:
            oneinfo();//查询个人信息
            break;
        case 2:
            cfbytime(1);//查询指定时间比赛列表
            break;
        case 3:
            paticflist();//查询你参加过的比赛列表
            break;
        case 4:
            atrate();//查询出勤率
            break;
        case 5:
            userevo();//查询所有比赛情况
            break;
        case 6:
            curl_global_init(CURL_GLOBAL_DEFAULT);
            anaprob();//分析本年度比赛题目
            curl_global_cleanup();
            break;
        case 7:
        printf("输入新的handle:\n");//重置用户名
        scanf("%s",handle);
        printf("handle已经成功修改为%s\n",handle);
        break;
        case 8:
            printf("程序即将结束...\n");//退出程序
            return 0;
            break;
        default:
            printf("输入无效!\n");
            break;
        }
        printf("按回车键继续:\n");
        getchar();
        while (getchar() != '\n');//只管是否回车
    }
    return 0;
}