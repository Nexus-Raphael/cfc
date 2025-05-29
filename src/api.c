#include "api.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <curl/multi.h>
#include<cJSON.h>

struct string {
    char *ptr;
    size_t len;
};

static size_t write_callback(void *data,size_t size,size_t nmemb,void *userp) {
    size_t realsize=size * nmemb;
    struct string *s=(struct string *)userp;
    char *newptr=realloc(s->ptr, s->len + realsize + 1);
    if (!newptr) return 0;
    s->ptr=newptr;
    memcpy(s->ptr+s->len,data,realsize);
    s->len+=realsize;
    s->ptr[s->len]='\0';
    return realsize;
}


char *getusr(const char *handle) {
    CURL *curl=curl_easy_init();
    if (!curl) return NULL;

    struct string s={.ptr=malloc(1),.len=0};
    char errbuf[CURL_ERROR_SIZE]={0};
    char url[256];
    snprintf(url,sizeof(url),"https://codeforces.com/api/user.info?handles=%s",handle);

    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_callback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&s);
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,errbuf);
    curl_easy_setopt(curl,CURLOPT_CAINFO,"certs/cacert.pem");


    CURLcode res=curl_easy_perform(curl);
    if (res!=CURLE_OK) {
        fprintf(stderr,"curl error: %s\n",errbuf[0]?errbuf:curl_easy_strerror(res));
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

char *getcontests(void) {
    CURL *curl=curl_easy_init();
    if (!curl) return NULL;

    struct string s={.ptr = malloc(1),.len=0};
    char errbuf[CURL_ERROR_SIZE]={0};
    const char *url="https://codeforces.com/api/contest.list";

    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_callback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&s);
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,errbuf);
    curl_easy_setopt(curl,CURLOPT_CAINFO,"certs/cacert.pem");

    CURLcode res=curl_easy_perform(curl);
    if (res!=CURLE_OK){
        fprintf(stderr,"curl error: %s\n", errbuf[0]?errbuf:curl_easy_strerror(res));
        if (s.len>0) {
            fprintf(stderr,"  >>> raw response:\n%s\n",s.ptr);
        }
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);
    return s.ptr;
}

char *paticf(const char *handle){
    CURL *curl=curl_easy_init();
    if (!curl) return NULL;

    struct string s={.ptr=malloc(1),.len=0};
    char errbuf[CURL_ERROR_SIZE]={0};
    char url[256];

    snprintf(url,sizeof(url),"https://codeforces.com/api/user.rating?handle=%s",handle);

    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_callback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&s);
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,errbuf);
    curl_easy_setopt(curl,CURLOPT_CAINFO,"certs/cacert.pem");

    CURLcode res=curl_easy_perform(curl);
    if (res!=CURLE_OK) {
        fprintf(stderr,"curl error: %s\n",errbuf[0]?errbuf:curl_easy_strerror(res));
        if(s.len>0){
            fprintf(stderr, "  >>> raw response:\n%s\n", s.ptr);
        }
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);
    return s.ptr;
}

char *getconteststandings(const char *handle, int contestId) {
    CURL *curl=curl_easy_init();
    if (!curl) return NULL;

    struct string s={ .ptr = malloc(1), .len = 0 };
    char errbuf[CURL_ERROR_SIZE] = {0};
    char url[512];
    snprintf(url, sizeof(url),"https://codeforces.com/api/contest.standings?contestId=%d&handles=%s&showUnofficial=true",contestId, handle);

    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_callback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&s);
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,errbuf);
    curl_easy_setopt(curl,CURLOPT_CAINFO,"certs/cacert.pem");

    CURLcode res=curl_easy_perform(curl);
    if (res!=CURLE_OK) {
        fprintf(stderr,"curl error: %s\n",errbuf[0]?errbuf:curl_easy_strerror(res));
        if (s.len>0){
            fprintf(stderr,"  >>> raw response:\n%s\n",s.ptr);
        }
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);
    return s.ptr;
}

char* httpget(const char* url) {
    CURL *curl=curl_easy_init();
    if (!curl) return NULL;

    struct string s={.ptr=malloc(1),.len=0};
    char errbuf[CURL_ERROR_SIZE]={0};

    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_callback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&s);
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,errbuf);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_CAINFO, "certs/cacert.pem");

    CURLcode res=curl_easy_perform(curl);
    if (res!=CURLE_OK) {
        fprintf(stderr,"curl error: %s\n",errbuf[0]?errbuf:curl_easy_strerror(res));
        free(s.ptr);
        curl_easy_cleanup(curl);
        return NULL;
    }
    curl_easy_cleanup(curl);
    return s.ptr;
}
