#include "api.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <curl/multi.h>

struct string {
    char *ptr;
    size_t len;
};

static size_t write_callback(void *data, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    struct string *s = (struct string *)userp;
    s->ptr = realloc(s->ptr, s->len + real_size + 1);
    memcpy(s->ptr + s->len, data, real_size);
    s->len += real_size;
    s->ptr[s->len] = '\0';
    return real_size;
}

char *fetch_user_info(const char *handle) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct string s = { .ptr = malloc(1), .len = 0 };
    char errbuf[CURL_ERROR_SIZE] = {0};
    char url[256];
    snprintf(url, sizeof(url),
             "https://codeforces.com/api/user.info?handles=%s",
             handle);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");
    // 临时调试时可以关 SSL 验证
    // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n",
                errbuf[0] ? errbuf : curl_easy_strerror(res));
        if (s.len > 0) {
            fprintf(stderr, "  >>> raw response:\n%s\n", s.ptr);
        }
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);
    return s.ptr;
}

char *fetch_contest_list(void) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct string s = { .ptr = malloc(1), .len = 0 };
    char errbuf[CURL_ERROR_SIZE] = {0};
    const char *url = "https://codeforces.com/api/contest.list";

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", errbuf[0] ? errbuf : curl_easy_strerror(res));
        if (s.len > 0) {
            fprintf(stderr, "  >>> raw response:\n%s\n", s.ptr);
        }
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);
    return s.ptr;
}

char *fetch_user_cflist(const char *handle){
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct string s = { .ptr = malloc(1), .len = 0 };
    char errbuf[CURL_ERROR_SIZE] = {0};
    char url[256];

    snprintf(url,sizeof(url),"https://codeforces.com/api/user.rating?handle=%s",handle);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", errbuf[0] ? errbuf : curl_easy_strerror(res));
        if (s.len > 0) {
            fprintf(stderr, "  >>> raw response:\n%s\n", s.ptr);
        }
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);
    return s.ptr;
}

char *fetch_contest_standings(const char *handle, int contestId) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct string s = { .ptr = malloc(1), .len = 0 };
    char errbuf[CURL_ERROR_SIZE] = {0};
    char url[512];

    // 构造请求 URL，传入比赛 ID 和用户名
    snprintf(url, sizeof(url),
        "https://codeforces.com/api/contest.standings?contestId=%d&handles=%s&showUnofficial=true",
        contestId, handle);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", errbuf[0] ? errbuf : curl_easy_strerror(res));
        if (s.len > 0) {
            fprintf(stderr, "  >>> raw response:\n%s\n", s.ptr);
        }
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);
    return s.ptr;
}

char **fetch_contest_standings_multi(const char *handle, const int *contestIds, int count) {
    if (count <= 0) return NULL;

    CURLM *multi = curl_multi_init();
    if (!multi) return NULL;

    CURL **easys = calloc(count, sizeof(CURL*));
    struct string *buffers = calloc(count, sizeof(struct string));
    char **responses = calloc(count, sizeof(char*));

    for (int i = 0; i < count; i++) {
        buffers[i].ptr = malloc(1);  // 初始化 buffer
        buffers[i].len = 0;

        easys[i] = curl_easy_init();
        if (!easys[i]) continue;

        char *url = malloc(512);
        snprintf(url, 512,
                 "https://codeforces.com/api/contest.standings?contestId=%d&handles=%s&showUnofficial=false",
                 contestIds[i], handle);

        curl_easy_setopt(easys[i], CURLOPT_URL, url);
        curl_easy_setopt(easys[i], CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(easys[i], CURLOPT_WRITEDATA, &buffers[i]);
        curl_easy_setopt(easys[i], CURLOPT_CAINFO, "certs/cacert.pem");

        curl_multi_add_handle(multi, easys[i]);
        free(url);  // curl 会拷贝 URL，不需要保存
    }

    int still_running;
    curl_multi_perform(multi, &still_running);
    while (still_running) {
        int numfds;
        curl_multi_wait(multi, NULL, 0, 1000, &numfds);
        curl_multi_perform(multi, &still_running);
    }

    for (int i = 0; i < count; i++) {
        curl_multi_remove_handle(multi, easys[i]);
        curl_easy_cleanup(easys[i]);

        responses[i] = buffers[i].ptr;  // 提取响应字符串
    }

    free(easys);
    free(buffers);  // 仅释放 struct string 数组本身，里面的 .ptr 是有效数据
    curl_multi_cleanup(multi);

    return responses;
}
