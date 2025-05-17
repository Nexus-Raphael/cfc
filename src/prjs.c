#include<stdio.h>
#include<cJSON.h>
#include<string.h>
#include<stdlib.h>
int main(){
    cJSON* root =cJSON_CreateObject();
    cJSON_AddStringToObject(root,"str1","你好，巴巴尔！");
    cJSON* key=cJSON_AddNumberToObject(root,"num",586);
    char *str=cJSON_Print(root);
    if(key!=NULL){
        printf("key:%s\nvalue:%d\n",key->string,key->valueint);
    }
    printf("______________________\n");
    printf(str);
    free(str);

    return 0;
}