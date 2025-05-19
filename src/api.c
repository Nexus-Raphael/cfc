#include "api.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

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
