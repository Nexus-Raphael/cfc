#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "cJSON.h"

int main(void) {
    const char *handle = "Nexus_Raphael";

    // 1. 获取 JSON 字符串
    char *json = fetch_user_info(handle);
    if (!json) {
        fprintf(stderr, "fetch_user_info failed—请检查上面的 curl 错误输出。\n");
        return 1;
    }

    // 2. 打印原始返回，确认内容是不是合法 JSON
    printf(">>> Raw JSON returned:\n%s\n\n", json);

    // 3. 尝试解析
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        fprintf(stderr, "JSON parse error at: %s\n", cJSON_GetErrorPtr());
        free(json);
        return 1;
    }
    free(json);

    // 4. 检查 API status
    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        fprintf(stderr, "API returned non-OK status\n");
        cJSON_Delete(root);
        return 1;
    }

    // 5. 解析 result 数组中的第 0 项（用户信息）
    cJSON *resultArr = cJSON_GetObjectItem(root, "result");
    cJSON *user = cJSON_GetArrayItem(resultArr, 0);
    if (!user) {
        fprintf(stderr, "No user data in result\n");
        cJSON_Delete(root);
        return 1;
    }

    int rating    = cJSON_GetObjectItem(user, "rating")->valueint;
    int maxRating = cJSON_GetObjectItem(user, "maxRating")->valueint;
    const char *rank = cJSON_GetObjectItem(user, "rank")->valuestring;

    // 6. 打印用户关键信息
    printf("Handle: %s\n", handle);
    printf("Rating: %d (max %d)\n", rating, maxRating);
    printf("Rank: %s\n", rank);

    cJSON_Delete(root);
    return 0;
}
