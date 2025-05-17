#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cJSON.h>

// 用于存储HTTP响应数据的结构体
struct ResponseData {
    char *data;
    size_t size;
};

// 回调函数，将响应数据存储到结构体中
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct ResponseData *mem = (struct ResponseData *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if(!ptr) {
        printf("内存分配错误!\n");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

// 解析JSON并打印rating变化
void parse_rating(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        printf("JSON解析错误: %s\n", cJSON_GetErrorPtr());
        return;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (strcmp(status->valuestring, "OK") != 0) {
        cJSON *comment = cJSON_GetObjectItem(root, "comment");
        printf("API错误: %s\n", comment->valuestring);
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsArray(result)) {
        printf("Rating变化记录:\n");
        printf("--------------------------------\n");
        
        for (int i = 0; i < cJSON_GetArraySize(result); i++) {
            cJSON *item = cJSON_GetArrayItem(result, i);
            cJSON *contest = cJSON_GetObjectItem(item, "contestName");
            cJSON *rating = cJSON_GetObjectItem(item, "newRating");
            cJSON *time = cJSON_GetObjectItem(item, "ratingUpdateTimeSeconds");
            
            printf("比赛: %s\n", contest->valuestring);
            printf("新Rating: %d\n", rating->valueint);
            printf("时间戳: %d\n", time->valueint);
            printf("--------------------------------\n");
        }
    }

    cJSON_Delete(root);
}

int main(void) {
    CURL *curl;
    CURLcode res;
    struct ResponseData chunk = {0};

    // 初始化CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if(curl) {
        // 设置API地址（替换handle参数值即可查询不同用户）
        char url[] = "https://codeforces.com/api/user.rating?handle=Nexus_Raphael";
        
        // 设置CURL选项
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");
        // 执行请求
        res = curl_easy_perform(curl);
        
        // 错误检查
        if(res != CURLE_OK) {
            fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
        } else {
            // 解析并处理返回的JSON数据
            parse_rating(chunk.data);
        }

        // 清理资源
        curl_easy_cleanup(curl);
        free(chunk.data);
    }

    curl_global_cleanup();
    return 0;
}