#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "cJSON.h"
#include <time.h>
#include <locale.h>

char handle[30];


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

void yearlyCFlist(void) {
    // 设置本地化，确保时间格式正常
    setlocale(LC_TIME, "");

    // 获取 JSON 数据
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

    int year_to_filter = 2025;
    printf("比赛列表（%d 年）：\n", year_to_filter);

    int count = cJSON_GetArraySize(result);
    for (int i = 0; i < count; ++i) {
        cJSON *contest = cJSON_GetArrayItem(result, i);
        if (!contest) continue;

        cJSON *id = cJSON_GetObjectItem(contest, "id");
        cJSON *name = cJSON_GetObjectItem(contest, "name");
        cJSON *phase = cJSON_GetObjectItem(contest, "phase");
        cJSON *start = cJSON_GetObjectItem(contest, "startTimeSeconds");

        if (!id || !name || !phase || !start) continue;

        time_t start_time = (time_t)start->valuedouble;
        struct tm *tm_info = localtime(&start_time);
        if ((tm_info->tm_year + 1900) != year_to_filter) continue;

        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        printf("ID: %d | %s | 状态: %s | 开始时间: %s\n",
               id->valueint,
               name->valuestring,
               phase->valuestring,
               time_buf);
    }

    cJSON_Delete(root);
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
        /* code */
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
    return 0;
}
