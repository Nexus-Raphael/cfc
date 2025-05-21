#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "cJSON.h"
#include <time.h>
#include <locale.h>

char handle[30];

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


//选手端---查询个人信息
void oneinfo(){
    // 1. 获取 JSON 字符串
    char *json = fetch_user_info(handle);
    if (!json) {
        fprintf(stderr, "fetch_user_info failed—请检查上面的 curl 错误输出。\n");
        return;
    }

    // 2. 打印原始返回，确认内容是不是合法 JSON
    printf(">>> Raw JSON returned:\n%s\n\n", json);

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

void yearlyCFlist() {
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
    upload_web_and_open("http://121.196.195.201/web/etest.html");

}

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
        time_t rating_time = (time_t)cJSON_GetObjectItem(entry, "ratingUpdateTimeSeconds")->valueint;
        struct tm *t = localtime(&rating_time);

        // 构造输出项
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", cid);
        cJSON_AddStringToObject(item, "name", cname);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        cJSON_AddStringToObject(item, "ratingUpdateTime", buf);

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

//教练端
void coach(){

}

//选手端
void player(){
    printf("输入用户名(handle):\n");
    scanf("%s",handle);
    printf("请选择功能(输入数字):\n1. 查询个人信息 \n2. 查询本年度比赛列表\n3. 查询你参加过的比赛列表\n4. 查询上次比赛情况\n5. 查询所有比赛情况\n");
    int tem=0;
    scanf("%d",&tem);
    switch (tem)
    {
    case 1:
        oneinfo();
        break;
    case 2:
        yearlyCFlist();
        break;
    case 3:
        patiCFlist();
        break;
    case 4:
        /* code */
        break;
    case 5:
        /* code */
        break;

    default:
        printf("输入无效!\n");
        break;
    }
}

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
