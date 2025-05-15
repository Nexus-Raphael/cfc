#include <stdio.h>
#include <curl/curl.h>
#include <cJSON.h>//添加了这一行


size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t real_size = size * nmemb;
    printf("%.*s", (int)real_size, ptr);
    return real_size;
}

int main(void) {
    CURL *curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://baidu.com");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        
        curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");
        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "Error: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }
    printf("1+1=2\n");
    return 0;
}