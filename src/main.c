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
#endif
#define MAX_PROBLEMS 32
#define MAX_NAME_LEN 128

#define MAX_CATEGORY_NAME 16

#define MAX_CONTESTS 128
int pre;
char handle[30];

typedef struct {
    char *data;
    size_t length;
} HttpResponse;

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t real = size * nmemb;
    HttpResponse *r = (HttpResponse*)userp;
    char *tmp = realloc(r->data, r->length + real + 1);
    if (!tmp) return 0;
    r->data = tmp;
    memcpy(r->data + r->length, ptr, real);
    r->length += real;
    r->data[r->length] = '\0';
    return real;
}

// 并发拉取多场 contest.standings
int fetch_multiple_standings(int *contestIds, int n, char **responses) {
    CURLM *multi = curl_multi_init();
    CURL *easys[MAX_CONTESTS] = {0};
    HttpResponse bufs[MAX_CONTESTS] = {0};

    // 1) 创建 easy handles
    for (int i = 0; i < n; ++i) {
        char url[256];
        snprintf(url, sizeof(url),
                 "https://codeforces.com/api/contest.standings?contestId=%d&showUnofficial=true",
                 contestIds[i]);

        easys[i] = curl_easy_init();
        bufs[i].data = malloc(1);
        bufs[i].length = 0;

        curl_easy_setopt(easys[i], CURLOPT_URL,            url);
        curl_easy_setopt(easys[i], CURLOPT_WRITEFUNCTION,  write_callback);
        curl_easy_setopt(easys[i], CURLOPT_WRITEDATA,      &bufs[i]);
        curl_easy_setopt(easys[i], CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easys[i], CURLOPT_USERAGENT,      "libcurl-agent/1.0");
        curl_easy_setopt(easys[i], CURLOPT_CAINFO, "certs/cacert.pem");

        curl_multi_add_handle(multi, easys[i]);
    }

    // 2) 并发执行
    int still = 0;
    curl_multi_perform(multi, &still);
    while (still) {
        curl_multi_wait(multi, NULL, 0, 500, NULL);
        curl_multi_perform(multi, &still);
    }

    // 3) 收集结果
    for (int i = 0; i < n; ++i) {
        long code = 0;
        curl_easy_getinfo(easys[i], CURLINFO_RESPONSE_CODE, &code);
        if (code == 200) {
            responses[i] = bufs[i].data;
        } else {
            free(bufs[i].data);
            responses[i] = NULL;
        }
        curl_multi_remove_handle(multi, easys[i]);
        curl_easy_cleanup(easys[i]);
    }
    curl_multi_cleanup(multi);
    return 0;
}

// -------------------------
// 数据结构
// -------------------------
typedef struct {
    char index[8];
    char name[128];
    int submitted;
    int accepted;
    double passRate;
    double maxScore, minScore;
    double averageScore, scoreVariance;
    int hasScore;
} ProblemStats;

typedef struct {
    int contestId;
    char name[128];
    char category[MAX_CATEGORY_NAME];
    int problemCount;
    ProblemStats *problems;  // 动态分配
} ContestStats;

// -------------------------
// 从 JSON 字符串解析 standings
// -------------------------
int analyzeContestProblems_from_json(const char *json_str,
                                     const char *category,
                                     ContestStats *outStats) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return -1;
    cJSON *res = cJSON_GetObjectItem(root, "result");
    cJSON *probs = cJSON_GetObjectItem(res, "problems");
    cJSON *rows  = cJSON_GetObjectItem(res, "rows");
    if (!cJSON_IsArray(probs) || !cJSON_IsArray(rows)) {
        cJSON_Delete(root);
        return -1;
    }

    // 填基础
    outStats->contestId = cJSON_GetObjectItem(
        cJSON_GetObjectItem(res, "contest"), "id")->valueint;
    strncpy(outStats->name,
            cJSON_GetObjectItem(
              cJSON_GetObjectItem(res, "contest"), "name")->valuestring,
            sizeof(outStats->name)-1);
    strncpy(outStats->category, category, MAX_CATEGORY_NAME-1);

    int pcount = cJSON_GetArraySize(probs);
    outStats->problemCount = pcount;
    outStats->problems = calloc(pcount, sizeof(ProblemStats));

    // 初始化 ProblemStats
    for (int i = 0; i < pcount; ++i) {
        ProblemStats *ps = &outStats->problems[i];
        cJSON *p = cJSON_GetArrayItem(probs, i);
        cJSON *idx = cJSON_GetObjectItem(p, "index");
        cJSON *nm  = cJSON_GetObjectItem(p, "name");
        cJSON *pt  = cJSON_GetObjectItem(p, "points");
        if (cJSON_IsString(idx)) strncpy(ps->index, idx->valuestring, sizeof(ps->index)-1);
        if (cJSON_IsString(nm))  strncpy(ps->name,  nm->valuestring,  sizeof(ps->name)-1);
        if (cJSON_IsNumber(pt)) {
            ps->hasScore = 1;
            ps->maxScore = ps->minScore = pt->valuedouble;
        }
        ps->submitted = ps->accepted = 0;
        ps->averageScore = ps->scoreVariance = 0.0;
    }

    // 累积每个用户的结果
    int rcount = cJSON_GetArraySize(rows);
    for (int r = 0; r < rcount; ++r) {
        cJSON *row = cJSON_GetArrayItem(rows, r);
        cJSON *pr  = cJSON_GetObjectItem(row, "problemResults");
        for (int i = 0; i < pcount; ++i) {
            ProblemStats *ps = &outStats->problems[i];
            cJSON *pi = cJSON_GetArrayItem(pr, i);
            double score = cJSON_GetObjectItem(pi, "points")->valuedouble;
            int attempts = cJSON_GetObjectItem(pi, "rejectedAttemptCount")->valueint;
            if (attempts > 0 || score > 0.0) ps->submitted++;
            if (score > 0.0) ps->accepted++;
            if (ps->hasScore) {
                if (score > ps->maxScore) ps->maxScore = score;
                if (score < ps->minScore) ps->minScore = score;
                ps->averageScore    += score;
                ps->scoreVariance   += score * score;
            }
        }
    }

    // 计算率、均值、方差
    for (int i = 0; i < pcount; ++i) {
        ProblemStats *ps = &outStats->problems[i];
        if (ps->submitted > 0) ps->passRate = (double)ps->accepted / ps->submitted;
        if (ps->hasScore && ps->submitted > 0) {
            double sum = ps->averageScore;
            ps->averageScore  = sum / ps->submitted;
            double meanSq     = ps->scoreVariance / ps->submitted;
            ps->scoreVariance = meanSq - ps->averageScore * ps->averageScore;
        }
    }

    cJSON_Delete(root);
    return 0;
}

// -------------------------
// 主控：并发 + 分类输出
// -------------------------
void analyzeProblems(void) {
    // 1) 读 web/data.json
    FILE *fp = fopen("web/data.json", "r");
    if (!fp) { perror("open data.json"); return; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp); rewind(fp);
    char *buf = malloc(sz+1);
    fread(buf,1,sz,fp); buf[sz]='\0'; fclose(fp);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { fprintf(stderr, "parse data.json failed\n"); return; }

    const char *cats[] = {"Div1","Div2","Div3","Div4","Educational"};
    int nCats = sizeof(cats)/sizeof(cats[0]);

    for (int ci = 0; ci < nCats; ++ci) {
        const char *cat = cats[ci];
        cJSON *arr = cJSON_GetObjectItem(root, cat);
        if (!cJSON_IsArray(arr)) continue;

        int n = cJSON_GetArraySize(arr);
        int *ids = malloc(n * sizeof(int));
        char **resps = calloc(n, sizeof(char*));

        // 收集 IDs
        cJSON *it; int idx=0;
        cJSON_ArrayForEach(it, arr) {
            ids[idx++] = cJSON_GetObjectItem(it, "id")->valueint;
        }

        // 并发拉取
        fetch_multiple_standings(ids, n, resps);

        // 解析并汇总到 JSON 数组
        cJSON *outArr = cJSON_CreateArray();
        for (int i = 0; i < n; ++i) {
            if (!resps[i]) continue;
            ContestStats stats;
            memset(&stats,0,sizeof(stats));
            analyzeContestProblems_from_json(resps[i], cat, &stats);
            free(resps[i]);

            // 序列化 stats
            cJSON *cobj = cJSON_CreateObject();
            cJSON_AddNumberToObject(cobj, "contestId",   stats.contestId);
            cJSON_AddStringToObject(cobj, "contestName", stats.name);
            cJSON_AddStringToObject(cobj, "category",    stats.category);
            cJSON *parr = cJSON_CreateArray();
            for (int j = 0; j < stats.problemCount; ++j) {
                ProblemStats *ps = &stats.problems[j];
                cJSON *pobj = cJSON_CreateObject();
                cJSON_AddStringToObject(pobj, "index",      ps->index);
                cJSON_AddStringToObject(pobj, "name",       ps->name);
                cJSON_AddNumberToObject(pobj, "submitted",  ps->submitted);
                cJSON_AddNumberToObject(pobj, "accepted",   ps->accepted);
                cJSON_AddNumberToObject(pobj, "passRate",   ps->passRate);
                if (ps->hasScore) {
                    cJSON_AddNumberToObject(pobj, "minScore",     ps->minScore);
                    cJSON_AddNumberToObject(pobj, "maxScore",     ps->maxScore);
                    cJSON_AddNumberToObject(pobj, "averageScore", ps->averageScore);
                    cJSON_AddNumberToObject(pobj, "scoreVariance",ps->scoreVariance);
                }
                cJSON_AddItemToArray(parr, pobj);
            }
            free(stats.problems);
            cJSON_AddItemToObject(cobj, "problems", parr);
            cJSON_AddItemToArray(outArr, cobj);
        }

        // 写文件
        char path[64];
        snprintf(path, sizeof(path), "web/%s_stats.json", cat);
        char *out = cJSON_PrintUnformatted(outArr);
        FILE *of = fopen(path, "w");
        if (of) { fprintf(of, "%s\n", out); fclose(of); printf("Wrote %s\n", path); }
        free(out);
        cJSON_Delete(outArr);

        free(ids);
        free(resps);
    }

    cJSON_Delete(root);
}
/**
 * 分析单场比赛题目统计
 * contestId: 比赛 ID
 * category: 比赛类别字符串
 * outStats: 指向已分配的 ContestStats 结构
 */


typedef struct {
    int year;
    int month;
    int total;     // 有效比赛总数
    int attended;  // 用户参加的有效比赛数
} Attendance;


const char* get_rank_name(int rating) {
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

void upload_web_and_open(const char *url_to_open) {
    int ret = system("scp -r web root@121.196.195.201:/usr/share/nginx/html/");
    if (ret != 0) {
        fprintf(stderr, "上传 web 目录失败，请检查网络或免密登录配置。\n");
    } else {
        printf("✅ 上传成功！打开网页：%s\n", url_to_open);

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "start %s", url_to_open);  // Windows 专用
        system(cmd);
    }
}


time_t upt(time_t original) {

    struct tm *tm_info = localtime(&original);

 
    tm_info->tm_mday -= 1;

    tm_info->tm_hour = 22;
    tm_info->tm_min = 35;
    tm_info->tm_sec = 0;

    time_t new_time = mktime(tm_info);

    return new_time;
}

//选手端---查询个人信息
void oneinfo(){
    // 1. 获取 JSON 字符串
    char *json = fetch_user_info(handle);
    if (!json) {
        fprintf(stderr, "fetch_user_info failed—请检查上面的 curl 错误输出。\n");
        return;
    }



    // 3. 尝试解析
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        fprintf(stderr, "JSON parse error at: %s\n", cJSON_GetErrorPtr());
        free(json);
        return;
    }
    free(json);

    // 4. 检查 API status
    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        fprintf(stderr, "API returned non-OK status\n");
        cJSON_Delete(root);
        return;
    }

    // 5. 解析 result 数组中的第 0 项（用户信息）
    cJSON *resultArr = cJSON_GetObjectItem(root, "result");
    cJSON *user = cJSON_GetArrayItem(resultArr, 0);
    if (!user) {
        fprintf(stderr, "No user data in result\n");
        cJSON_Delete(root);
        return;
    }

    int rating    = cJSON_GetObjectItem(user, "rating")->valueint;
    int maxRating = cJSON_GetObjectItem(user, "maxRating")->valueint;
    const char *rank = cJSON_GetObjectItem(user, "rank")->valuestring;

    // 6. 打印用户关键信息
    printf("Handle: %s\n", handle);
    printf("Rating: %d (max %d)\n", rating, maxRating);
    printf("Rank: %s\n", rank);

    cJSON_Delete(root);
    return;
}

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//选手端---查询年度比赛列表
void yearlyCFlist(int pre) {
    int year_to_filter=2025;
    setlocale(LC_TIME, "");

    // 1. 拉下全部比赛 JSON
    char *json = fetch_contest_list();
    if (!json) {
        fprintf(stderr, "获取比赛列表失败。\n");
        return;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        fprintf(stderr, "JSON 解析失败：%s\n", cJSON_GetErrorPtr());
        return;
    }
    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        fprintf(stderr, "API 返回状态异常。\n");
        cJSON_Delete(root);
        return;
    }
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        fprintf(stderr, "比赛列表格式异常。\n");
        cJSON_Delete(root);
        return;
    }

    // 2. 创建 5 个组数组
    cJSON *out = cJSON_CreateObject();
    cJSON *arrDiv1 = cJSON_CreateArray();
    cJSON *arrDiv2 = cJSON_CreateArray();
    cJSON *arrDiv3 = cJSON_CreateArray();
    cJSON *arrDiv4 = cJSON_CreateArray();
    cJSON *arrEdu  = cJSON_CreateArray();

    int cnt = cJSON_GetArraySize(result);
    for (int i = 0; i < cnt; ++i) {
        cJSON *ct = cJSON_GetArrayItem(result, i);
        if (!ct) continue;

        // 仅处理已结束的比赛
        cJSON *phase = cJSON_GetObjectItem(ct, "phase");
        if (!cJSON_IsString(phase) || strcmp(phase->valuestring, "FINISHED") != 0) {
            continue;
        }

        // 年份检查：遇到第一个非本年度，提前退出
        cJSON *start = cJSON_GetObjectItem(ct, "startTimeSeconds");
        time_t st = (time_t)start->valuedouble;
        struct tm *t = localtime(&st);
        if ((t->tm_year + 1900) != year_to_filter) {
            break;
        }

        // 构造输出项，只取 id/name/start/division
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", cJSON_GetObjectItem(ct,"id")->valueint);
        cJSON_AddStringToObject(item, "name", cJSON_GetObjectItem(ct,"name")->valuestring);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        cJSON_AddStringToObject(item, "startTime", buf);

        // 判断是 Educational 还是 Div.X
        const char *nm = cJSON_GetObjectItem(ct, "name")->valuestring;
        char *p = strstr(nm, "(Div. ");
        if (p) {
            // p 指向 "(Div. X...)"，直接解析
            int divnum = 0;
            if (sscanf(p, "(Div. %d)", &divnum) == 1) {
                // 正常的 Div 1-4
                switch (divnum) {
                    case 1: cJSON_AddItemToArray(arrDiv1, item); break;
                    case 2: cJSON_AddItemToArray(arrDiv2, item); break;
                    case 3: cJSON_AddItemToArray(arrDiv3, item); break;
                    case 4: cJSON_AddItemToArray(arrDiv4, item); break;
                    default: cJSON_Delete(item); break;
                }
            } else {
                // 虽然有 "(Div. " 字样，但解析失败也丢弃
                cJSON_Delete(item);
            }
        } else if (strstr(nm, "Educational") != NULL) {
            // 教育赛
            cJSON_AddItemToArray(arrEdu, item);
        } else {
            // 其它不关心的 Round
            cJSON_Delete(item);
        }

    }
    // 3. 挂到顶层对象
    cJSON_AddItemToObject(out, "Div1", arrDiv1);
    cJSON_AddItemToObject(out, "Div2", arrDiv2);
    cJSON_AddItemToObject(out, "Div3", arrDiv3);
    cJSON_AddItemToObject(out, "Div4", arrDiv4);
    cJSON_AddItemToObject(out, "Educational", arrEdu);

    // 4. 写文件
    char *out_str = cJSON_Print(out);
    FILE *fp = fopen("web/data.json", "w");
    if (fp) {
        fprintf(fp, "%s", out_str);
        fclose(fp);
    } else {
        perror("无法打开 web/data.json");
    }
    free(out_str);
    cJSON_Delete(out);
    cJSON_Delete(root);

    printf("已生成 web/data.json，包含 %d 年比赛分组数据。\n", year_to_filter);
    if(pre) upload_web_and_open("http://121.196.195.201/web/etest.html");
    else return;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//选手端---查询参与比赛列表
void patiCFlist() {
    char *json = fetch_user_cflist(handle);
    if (!json) {
        fprintf(stderr, "获取用户参加过的比赛列表失败。\n");
        return;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        fprintf(stderr, "JSON 解析失败：%s\n", cJSON_GetErrorPtr());
        return;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        fprintf(stderr, "API 返回状态异常。\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        fprintf(stderr, "比赛列表格式异常。\n");
        cJSON_Delete(root);
        return;
    }

    // 创建输出对象和分组数组
    cJSON *out = cJSON_CreateObject();
    cJSON *arrDiv1 = cJSON_CreateArray();
    cJSON *arrDiv2 = cJSON_CreateArray();
    cJSON *arrDiv3 = cJSON_CreateArray();
    cJSON *arrDiv4 = cJSON_CreateArray();
    cJSON *arrEdu  = cJSON_CreateArray();

    int cnt = cJSON_GetArraySize(result);
    for (int i = 0; i < cnt; ++i) {
        cJSON *entry = cJSON_GetArrayItem(result, i);
        if (!entry) continue;

        int cid = cJSON_GetObjectItem(entry, "contestId")->valueint;
        const char *cname = cJSON_GetObjectItem(entry, "contestName")->valuestring;
        time_t rating_time = (time_t)cJSON_GetObjectItem(entry, "ratingUpdateTimeSeconds")->valueint;//需要转换为UTC+8前一天的22：35
        rating_time=upt(rating_time);
        struct tm *t = localtime(&rating_time);

        // 构造输出项
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", cid);
        cJSON_AddStringToObject(item, "name", cname);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        cJSON_AddStringToObject(item, "starttime", buf);

        // 按类别分类（Educational 或 Div. X）
        const char *nm = cname;
        char *p = strstr(nm, "(Div. ");
        if (p) {
            int divnum = 0;
            if (sscanf(p, "(Div. %d)", &divnum) == 1) {
                switch (divnum) {
                    case 1: cJSON_AddItemToArray(arrDiv1, item); break;
                    case 2: cJSON_AddItemToArray(arrDiv2, item); break;
                    case 3: cJSON_AddItemToArray(arrDiv3, item); break;
                    case 4: cJSON_AddItemToArray(arrDiv4, item); break;
                    default: cJSON_Delete(item); break;
                }
            } else {
                cJSON_Delete(item);
            }
        } else if (strstr(nm, "Educational") != NULL) {
            cJSON_AddItemToArray(arrEdu, item);
        } else {
            cJSON_Delete(item); // 其他比赛类型不处理
        }
    }

    // 添加到顶层对象
    cJSON_AddItemToObject(out, "Div1", arrDiv1);
    cJSON_AddItemToObject(out, "Div2", arrDiv2);
    cJSON_AddItemToObject(out, "Div3", arrDiv3);
    cJSON_AddItemToObject(out, "Div4", arrDiv4);
    cJSON_AddItemToObject(out, "Educational", arrEdu);

    // 输出到文件
    char *out_str = cJSON_Print(out);
    FILE *fp = fopen("web/user_data.json", "w");
    if (fp) {
        fprintf(fp, "%s", out_str);
        fclose(fp);
    } else {
        perror("无法打开 web/user_data.json");
    }

    free(out_str);
    cJSON_Delete(out);
    cJSON_Delete(root);

    printf("已生成 web/user_data.json，包含该用户参与的比赛分组数据。\n");
    upload_web_and_open("http://121.196.195.201/web/ucflist.html");

}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 判断一个 contest 是否应被统计到 total
static int isValidContest(const cJSON *contest, time_t now) {
    // phase == FINISHED
    cJSON *cj_phase = cJSON_GetObjectItem(contest, "phase");
    if (!cj_phase || !cJSON_IsString(cj_phase)
        || strcmp(cj_phase->valuestring, "FINISHED") != 0) {
        return 0;
    }
    // startTimeSeconds <= now
    cJSON *cj_ts = cJSON_GetObjectItem(contest, "startTimeSeconds");
    if (!cj_ts || !cJSON_IsNumber(cj_ts)
        || (time_t)cj_ts->valuedouble > now) {
        return 0;
    }
    // 名称匹配
    cJSON *cj_name = cJSON_GetObjectItem(contest, "name");
    if (!cj_name || !cJSON_IsString(cj_name)) return 0;
    const char *name = cj_name->valuestring;
    if (strstr(name, "Rated for Div.")
        || strstr(name, "Educational Codeforces Round")) {
        return 1;
    }
    if (strstr(name, "Div.") && !strstr(name, "Unrated")) {
        return 1;
    }
    return 0;
}

//选手端---用户出勤率查询
void calcAttendance(void) {
    // 1. 拉取 contest.list 和 user.rating
    char *contest_json = fetch_contest_list();
    if (!contest_json) { fprintf(stderr, "Failed to fetch contest list\n"); return; }
    char *user_json = fetch_user_cflist(handle);
    if (!user_json) { fprintf(stderr, "Failed to fetch user rating history\n"); free(contest_json); return; }

    // 2. 解析 JSON
    cJSON *cj_contests = cJSON_Parse(contest_json);
    cJSON *cj_user     = cJSON_Parse(user_json);
    free(contest_json);
    free(user_json);
    if (!cj_contests || !cj_user) { fprintf(stderr, "Failed to parse JSON\n"); goto cleanup; }
    
    cJSON *resultA = cJSON_GetObjectItem(cj_contests, "result");
    cJSON *resultB = cJSON_GetObjectItem(cj_user,     "result");
    if (!cJSON_IsArray(resultA) || !cJSON_IsArray(resultB)) {
        fprintf(stderr, "Unexpected JSON structure\n");
        goto cleanup;
    }

    // 3. 找到用户第一场比赛的时间戳
    cJSON *firstRating = cJSON_GetArrayItem(resultB, 0);
    int first_cid = cJSON_GetObjectItem(firstRating, "contestId")->valueint;
    time_t first_ts = 0;
    cJSON *contest;
    cJSON_ArrayForEach(contest, resultA) {
        cJSON *cj_id = cJSON_GetObjectItem(contest, "id");
        if (cj_id && cj_id->valueint == first_cid) {
            first_ts = (time_t)cJSON_GetObjectItem(contest, "startTimeSeconds")->valuedouble;
            break;
        }
    }
    if (first_ts == 0) {
        fprintf(stderr, "Cannot find first contest in contest.list\n");
        goto cleanup;
    }
    struct tm first_tm;
    localtime_r(&first_ts, &first_tm);
    int first_year  = first_tm.tm_year + 1900;
    int first_month = first_tm.tm_mon + 1;

    // 4. 初始化
    time_t now = time(NULL);
    cJSON *attendance = cJSON_CreateObject();
    cJSON *seenRounds = cJSON_CreateObject();

    // 5. 遍历 contest.list 统计 total（含去重 & 时间范围）
    cJSON_ArrayForEach(contest, resultA) {
        // a) 获取开始时间
        cJSON *cj_ts2 = cJSON_GetObjectItem(contest, "startTimeSeconds");
        if (!cj_ts2 || !cJSON_IsNumber(cj_ts2)) continue;
        time_t ts = (time_t)cj_ts2->valuedouble;
        struct tm tm_st;
        localtime_r(&ts, &tm_st);
        int y = tm_st.tm_year + 1900;
        int m = tm_st.tm_mon + 1;
        // b) 忽略首次比赛之前的
        if (y < first_year || (y == first_year && m < first_month)) continue;
        // c) 过滤有效赛
        if (!isValidContest(contest, now)) continue;

        // d) 去重同一期
        const char *name = cJSON_GetObjectItem(contest, "name")->valuestring;
        const char *p = strchr(name, '(');
        char baseName[64] = {0};
        if (p) {
            size_t len = p - name;
            if (len >= sizeof(baseName)) len = sizeof(baseName)-1;
            strncpy(baseName, name, len);
            baseName[len] = '\0';
        } else {
            strncpy(baseName, name, sizeof(baseName)-1);
        }
        if (cJSON_GetObjectItem(seenRounds, baseName)) continue;
        cJSON_AddTrueToObject(seenRounds, baseName);

        // e) 累加 total
        char monthKey[8];
        snprintf(monthKey, sizeof(monthKey), "%04d-%02d", y, m);
        cJSON *mo = cJSON_GetObjectItem(attendance, monthKey);
        if (!mo) {
            mo = cJSON_CreateObject();
            cJSON_AddNumberToObject(mo, "total",    1);
            cJSON_AddNumberToObject(mo, "attended", 0);
            cJSON_AddItemToObject(attendance, monthKey, mo);
        } else {
            cJSON_GetObjectItem(mo, "total")->valuedouble += 1;
        }
    }

    // 6. 遍历 user.rating 统计 attended
    cJSON *rating;
    cJSON_ArrayForEach(rating, resultB) {
        int cid = cJSON_GetObjectItem(rating, "contestId")->valueint;
        cJSON *found = NULL;
        cJSON_ArrayForEach(contest, resultA) {
            cJSON *cj_id = cJSON_GetObjectItem(contest, "id");
            if (cj_id && cj_id->valueint == cid) { found = contest; break; }
        }
        if (!found) continue;
        if (!isValidContest(found, now)) continue;
        time_t ts2; struct tm tm2;
        ts2 = (time_t)cJSON_GetObjectItem(found, "startTimeSeconds")->valuedouble;
        localtime_r(&ts2, &tm2);
        char monthKey[8];
        snprintf(monthKey, sizeof(monthKey), "%04d-%02d", tm2.tm_year+1900, tm2.tm_mon+1);
        cJSON *mo = cJSON_GetObjectItem(attendance, monthKey);
        if (mo) cJSON_GetObjectItem(mo, "attended")->valuedouble += 1;
    }

    // 7. 计算 rate 并写入 out
    cJSON *out = cJSON_CreateObject();
    cJSON *monthObj;
    cJSON_ArrayForEach(monthObj, attendance) {
        const char *key = monthObj->string;
        double total    = cJSON_GetObjectItem(monthObj, "total")->valuedouble;
        double attended = cJSON_GetObjectItem(monthObj, "attended")->valuedouble;
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "total",    total);
        cJSON_AddNumberToObject(entry, "attended", attended);
        cJSON_AddNumberToObject(entry, "rate",     total>0 ? attended/total : 0.0);
        cJSON_AddItemToObject(out, key, entry);
    }

    // 8. 输出到文件
    char *out_str = cJSON_PrintUnformatted(out);
    FILE *fp = fopen("web/attendance.json", "w");
    if (fp) { fprintf(fp, "%s\n", out_str); fclose(fp); printf("attendance.json 已生成\n"); }
    else    { fprintf(stderr, "Failed to open web/attendance.json\n"); }
    free(out_str);

    // 9. 清理
    cJSON_Delete(out);
    cJSON_Delete(attendance);
    cJSON_Delete(seenRounds);

cleanup:
    cJSON_Delete(cj_contests);
    cJSON_Delete(cj_user);
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//选手端---用户成长曲线
void userevo() {
    char *json_list = fetch_user_cflist(handle);
    if (!json_list) {
        fprintf(stderr, "获取用户 %s 的比赛列表失败。\n", handle);
        return;
    }

    cJSON *root = cJSON_Parse(json_list);
    free(json_list);
    if (!root) {
        fprintf(stderr, "JSON 解析失败：%s\n", cJSON_GetErrorPtr());
        return;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        fprintf(stderr, "API 返回状态异常。\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        fprintf(stderr, "比赛列表格式异常。\n");
        cJSON_Delete(root);
        return;
    }

    int count = cJSON_GetArraySize(result);
    if (count == 0) {
        fprintf(stderr, "错误：用户 %s 未参加任何比赛或用户名无效！\n", handle);
        cJSON_Delete(root);
        return;
    }

    cJSON *out_arr = cJSON_CreateArray();
    printf("找到 %d 场参赛记录\n", count);

    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_GetArrayItem(result, i);
        if (!entry) continue;

        int contestId = cJSON_GetObjectItem(entry, "contestId")->valueint;
        const char *contestName = cJSON_GetObjectItem(entry, "contestName")->valuestring;
        int oldRating = cJSON_GetObjectItem(entry, "oldRating") ? cJSON_GetObjectItem(entry, "oldRating")->valueint : 0;
        int newRating = cJSON_GetObjectItem(entry, "newRating") ? cJSON_GetObjectItem(entry, "newRating")->valueint : 0;
        int ratingUpdateTime = 0;
        cJSON *upd_time = cJSON_GetObjectItem(entry, "ratingUpdateTimeSeconds");
        if (upd_time && cJSON_IsNumber(upd_time)) {
            ratingUpdateTime = upd_time->valueint;
        }

        printf("处理第 %d/%d 场比赛：%s (ID=%d)\n", i + 1, count, contestName, contestId);

        char *json_standings = fetch_contest_standings(handle, contestId);
        if (!json_standings) {
            fprintf(stderr, "获取比赛 %d 的详细数据失败，跳过...\n", contestId);
            continue;
        }

        cJSON *standings_root = cJSON_Parse(json_standings);
        free(json_standings);
        if (!standings_root) {
            fprintf(stderr, "比赛 %d JSON 解析失败，跳过...\n", contestId);
            continue;
        }

        cJSON *status2 = cJSON_GetObjectItem(standings_root, "status");
        if (!cJSON_IsString(status2) || strcmp(status2->valuestring, "OK") != 0) {
            fprintf(stderr, "比赛 %d API 返回状态异常，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *res2 = cJSON_GetObjectItem(standings_root, "result");
        if (!res2) {
            fprintf(stderr, "比赛 %d result 缺失，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *problems = cJSON_GetObjectItem(res2, "problems");
        cJSON *rows = cJSON_GetObjectItem(res2, "rows");
        if (!problems || !rows) {
            fprintf(stderr, "比赛 %d 缺失 problems 或 rows，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *user_row = NULL;
        int rows_count = cJSON_GetArraySize(rows);
        for (int j = 0; j < rows_count; j++) {
            cJSON *row = cJSON_GetArrayItem(rows, j);
            cJSON *party = cJSON_GetObjectItem(row, "party");
            if (!party) continue;
            cJSON *members = cJSON_GetObjectItem(party, "members");
            if (!members || cJSON_GetArraySize(members) == 0) continue;

            cJSON *member0 = cJSON_GetArrayItem(members, 0);
            cJSON *m_handle = cJSON_GetObjectItem(member0, "handle");
            if (m_handle && strcmp(m_handle->valuestring, handle) == 0) {
                user_row = row;
                break;
            }
        }

        if (!user_row) {
            fprintf(stderr, "比赛 %d 未找到用户行，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        int rank = cJSON_GetObjectItem(user_row, "rank")->valueint;
        cJSON *problemResults = cJSON_GetObjectItem(user_row, "problemResults");
        int problem_count = cJSON_GetArraySize(problems);

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "contestId", contestId);
        cJSON_AddStringToObject(obj, "contestName", contestName);
        cJSON_AddNumberToObject(obj, "ratingUpdateTime", ratingUpdateTime);
        cJSON_AddNumberToObject(obj, "rank", rank);
        cJSON_AddNumberToObject(obj, "oldRating", oldRating);
        cJSON_AddNumberToObject(obj, "newRating", newRating);
        cJSON_AddStringToObject(obj, "rankBefore", get_rank_name(oldRating));
        cJSON_AddStringToObject(obj, "rankAfter", get_rank_name(newRating));

        cJSON *problems_arr = cJSON_CreateArray();
        bool isSimpleContest = strstr(contestName, "Div. 3") || strstr(contestName, "Div. 4") || strstr(contestName, "Educational");
        for (int k = 0; k < problem_count; k++) {
            cJSON *problem = cJSON_GetArrayItem(problems, k);
            if (!problem) continue;

            const char *index = cJSON_GetObjectItem(problem, "index")->valuestring;

            cJSON *pr = (problemResults && k < cJSON_GetArraySize(problemResults)) ? cJSON_GetArrayItem(problemResults, k) : NULL;

            if (isSimpleContest) {
            // 只记录 AC 的题目 index
            bool accepted = false;
            if (pr) {
                cJSON *pr_pts = cJSON_GetObjectItem(pr, "points");
                accepted = (pr_pts && cJSON_IsNumber(pr_pts) && pr_pts->valuedouble > 0.0);
            }
            if (accepted) {
                cJSON_AddItemToArray(problems_arr, cJSON_CreateString(index));
            }
            } else {
                // 保留详细得分信息
                cJSON *prob_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(prob_obj, "index", index);

                cJSON *p_pts = cJSON_GetObjectItem(problem, "points");
                int maxPoints = (p_pts && cJSON_IsNumber(p_pts)) ? (int)p_pts->valuedouble : 0;
                cJSON_AddNumberToObject(prob_obj, "maxPoints", maxPoints);

                int userPoints = 0;
                if (pr) {
                    cJSON *pr_pts = cJSON_GetObjectItem(pr, "points");
                    if (pr_pts && cJSON_IsNumber(pr_pts)) {
                        userPoints = (int)pr_pts->valuedouble;
                    }
                }
                cJSON_AddNumberToObject(prob_obj, "points", userPoints);
                cJSON_AddItemToArray(problems_arr, prob_obj);
            }
        }

        cJSON_AddItemToObject(obj, "problems", problems_arr);
        cJSON_AddItemToArray(out_arr, obj);

        cJSON_Delete(standings_root);
    }

    // 写出 JSON 文件
    char *out_str = cJSON_Print(out_arr);
    FILE *fp = fopen("web/user_evolution.json", "w");
    if (fp) {
        fprintf(fp, "%s", out_str);
        fclose(fp);
        printf("✅ 已生成用户参赛进化数据：web/user_evolution.json\n");
    } else {
        perror("❌ 写入文件失败");
    }

    free(out_str);
    cJSON_Delete(out_arr);
    cJSON_Delete(root);
}

// //分析每一场比赛
// int analyzeContestProblems(int contestId, const char* category, ContestStats* outStats) {
//     char url[256];
//     snprintf(url, sizeof(url),
//              "https://codeforces.com/api/contest.standings?contestId=%d&showUnofficial=true",
//              contestId);
//     char* resp = http_get(url);
//     if (!resp) {
//         fprintf(stderr, "Failed to fetch standings for contest %d\n", contestId);
//         return -1;
//     }

//     cJSON* root = cJSON_Parse(resp);
//     free(resp);
//     if (!root) {
//         fprintf(stderr, "Failed to parse JSON for contest %d\n", contestId);
//         return -1;
//     }
//     cJSON* result = cJSON_GetObjectItem(root, "result");
//     cJSON* problems = cJSON_GetObjectItem(result, "problems");
//     cJSON* rows = cJSON_GetObjectItem(result, "rows");
//     if (!cJSON_IsArray(problems) || !cJSON_IsArray(rows)) {
//         fprintf(stderr, "Unexpected API structure for contest %d\n", contestId);
//         cJSON_Delete(root);
//         return -1;
//     }

//     // 填充 ContestStats 基本信息
//     outStats->contestId = contestId;
//     strncpy(outStats->category, category, sizeof(outStats->category)-1);
//     snprintf(outStats->name, MAX_NAME_LEN, "Contest %d", contestId);
//     cJSON* contestInfo = cJSON_GetObjectItem(result, "contest");
//     if (contestInfo) {
//         cJSON* cname = cJSON_GetObjectItem(contestInfo, "name");
//         if (cJSON_IsString(cname)) strncpy(outStats->name, cname->valuestring, MAX_NAME_LEN-1);
//     }

//     int pcount = cJSON_GetArraySize(problems);
//     outStats->problemCount = pcount;

//     // 初始化题目统计
//     for (int i = 0; i < pcount; ++i) {
//         ProblemStats* ps = &outStats->problems[i];
//         memset(ps, 0, sizeof(*ps));
//         cJSON* prob = cJSON_GetArrayItem(problems, i);
//         cJSON* idx = cJSON_GetObjectItem(prob, "index");
//         cJSON* pname = cJSON_GetObjectItem(prob, "name");
//         cJSON* pp = cJSON_GetObjectItem(prob, "points");
//         if (cJSON_IsString(idx)) strncpy(ps->index, idx->valuestring, sizeof(ps->index)-1);
//         if (cJSON_IsString(pname)) strncpy(ps->name, pname->valuestring, sizeof(ps->name)-1);
//         if (cJSON_IsNumber(pp)) {
//             ps->hasScore = 1;
//             ps->maxScore = pp->valuedouble;
//             ps->minScore = pp->valuedouble;
//         } else {
//             ps->hasScore = 0;
//             ps->maxScore = 0;
//             ps->minScore = 0;
//         }
//         ps->accepted = ps->submitted = 0;
//         ps->averageScore = ps->scoreVariance = 0.0;
//     }

//     // 遍历 rows，统计每道题
//     int rcount = cJSON_GetArraySize(rows);
//     for (int r = 0; r < rcount; ++r) {
//         cJSON* row = cJSON_GetArrayItem(rows, r);
//         cJSON* results = cJSON_GetObjectItem(row, "problemResults");
//         if (!cJSON_IsArray(results)) continue;
//         for (int i = 0; i < pcount; ++i) {
//             ProblemStats* ps = &outStats->problems[i];
//             cJSON* pr = cJSON_GetArrayItem(results, i);
//             double score = cJSON_GetObjectItem(pr, "points")->valuedouble;
//             int attempts = cJSON_GetObjectItem(pr, "rejectedAttemptCount")->valueint;

//             // 判断是否提交
//             if (attempts > 0 || score > 0.0) {
//                 ps->submitted++;
//             }
//             // 判断是否通过（得分>0）
//             if (score > 0.0) {
//                 ps->accepted++;
//             }
//             // 分值统计
//             if (ps->hasScore) {
//                 if (score > ps->maxScore) ps->maxScore = score;
//                 if (score < ps->minScore) ps->minScore = score;
//                 ps->averageScore += score;
//                 ps->scoreVariance += score * score;
//             }
//         }
//     }

//     // 计算平均与方差
//     for (int i = 0; i < pcount; ++i) {
//         ProblemStats* ps = &outStats->problems[i];
//         if (ps->submitted > 0)
//             ps->passRate = (double)ps->accepted / ps->submitted;
//         if (ps->hasScore && ps->submitted > 0) {
//             double sum = ps->averageScore;
//             ps->averageScore = sum / ps->submitted;
//             // E[X^2] - E[X]^2
//             double meanSq = ps->scoreVariance / ps->submitted;
//             ps->scoreVariance = meanSq - ps->averageScore * ps->averageScore;
//         }
//     }

//     cJSON_Delete(root);
//     return 0;
// }


// void monana(){

// }

// void analyzeProblems(void) {
//     // 1. 读取 web/data.json
//     const char* data_path = "web/data.json";
//     FILE* fp = fopen(data_path, "r");
//     if (!fp) {
//         fprintf(stderr, "Failed to open %s\n", data_path);
//         return;
//     }
//     fseek(fp, 0, SEEK_END);
//     long fsize = ftell(fp);
//     fseek(fp, 0, SEEK_SET);
//     char* text = malloc(fsize + 1);
//     if (!text) { fclose(fp); return; }
//     fread(text, 1, fsize, fp);
//     text[fsize] = '\0';
//     fclose(fp);

//     cJSON* root = cJSON_Parse(text);
//     free(text);
//     if (!root) {
//         fprintf(stderr, "Failed to parse data.json\n");
//         return;
//     }

//     // 2. 准备类别列表
//     const char* categories[] = {"Div1","Div2","Div3","Div4","Educational"};
//     size_t catCount = sizeof(categories)/sizeof(categories[0]);

//     for (size_t ci = 0; ci < catCount; ++ci) {
//         const char* cat = categories[ci];
//         cJSON* list = cJSON_GetObjectItem(root, cat);
//         if (!cJSON_IsArray(list)) continue;

//         // 为该类别创建输出数组
//         cJSON* outArr = cJSON_CreateArray();

//         // 遍历比赛
//         cJSON* item;
//         cJSON_ArrayForEach(item, list) {
//             cJSON* cid_json = cJSON_GetObjectItem(item, "id");
//             if (!cJSON_IsNumber(cid_json)) continue;
//             int contestId = cid_json->valueint;

//             // 调用分析函数
//             ContestStats stats;
//             memset(&stats, 0, sizeof(stats));
//             if (analyzeContestProblems(contestId, cat, &stats) != 0) {
//                 continue;
//             }

//             // 将 stats 转为 JSON
//             cJSON* cobj = cJSON_CreateObject();
//             cJSON_AddNumberToObject(cobj, "contestId", stats.contestId);
//             cJSON_AddStringToObject(cobj, "contestName", stats.name);
//             cJSON_AddStringToObject(cobj, "category", stats.category);
//             cJSON* parr = cJSON_CreateArray();
//             for (int i = 0; i < stats.problemCount; ++i) {
//                 ProblemStats* ps = &stats.problems[i];
//                 cJSON* pobj = cJSON_CreateObject();
//                 cJSON_AddStringToObject(pobj, "index", ps->index);
//                 cJSON_AddStringToObject(pobj, "name", ps->name);
//                 cJSON_AddNumberToObject(pobj, "submitted", ps->submitted);
//                 cJSON_AddNumberToObject(pobj, "accepted",  ps->accepted);
//                 cJSON_AddNumberToObject(pobj, "passRate",  ps->passRate);
//                 if (ps->hasScore) {
//                     cJSON_AddNumberToObject(pobj, "minScore",       ps->minScore);
//                     cJSON_AddNumberToObject(pobj, "maxScore",       ps->maxScore);
//                     cJSON_AddNumberToObject(pobj, "averageScore",   ps->averageScore);
//                     cJSON_AddNumberToObject(pobj, "scoreVariance",  ps->scoreVariance);
//                 }
//                 cJSON_AddItemToArray(parr, pobj);
//             }
//             cJSON_AddItemToObject(cobj, "problems", parr);
//             cJSON_AddItemToArray(outArr, cobj);
//         }

//         // 3. 写入文件 web/<category>_stats.json
//         char outpath[128];
//         snprintf(outpath, sizeof(outpath), "web/%s_stats.json", cat);
//         char* out_text = cJSON_PrintUnformatted(outArr);
//         FILE* ofp = fopen(outpath, "w");
//         if (ofp) {
//             fprintf(ofp, "%s\n", out_text);
//             fclose(ofp);
//             printf("Wrote %s\n", outpath);
//         } else {
//             fprintf(stderr, "Failed to write %s\n", outpath);
//         }
//         free(out_text);
//         cJSON_Delete(outArr);
//     }

//     cJSON_Delete(root);
// }
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//教练端
void coach(){

}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//选手端
void player(){
    printf("输入用户名(handle):\n");
    scanf("%s",handle);
    printf("请选择功能(输入数字):\n1. 查询个人信息 \n2. 查询本年度比赛列表\n3. 查询你参加过的比赛列表\n4. 查询出勤率\n5. 查询所有比赛情况\n6. 分析本年度比赛题目\n");
    int tem=0;
    scanf("%d",&tem);
    switch (tem)
    {
    case 1:
        oneinfo();
        break;
    case 2:
        yearlyCFlist(1);
        break;
    case 3:
        patiCFlist();
        break;
    case 4:
        calcAttendance();
        break;
    case 5:
        userevo();
        break;
    case 6:
        curl_global_init(CURL_GLOBAL_DEFAULT);
        analyzeProblems();
        curl_global_cleanup();
        break;
    default:
        printf("输入无效!\n");
        break;
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//主函数
int main(){
    printf("请选择你的身份(输入1或2):\n1. 教练 \n2. 选手\n");
    int choice=0;
    scanf("%d",&choice);
    if(choice==1){
        coach();
    }else if(choice==2){
        player();
    }else printf("输入有效值!");
    // system("start \"\" \"web\\etest.html\"");//加了这一行
    return 0;
}