#ifndef API_H
#define API_H

// 从 Codeforces 读取给定 handle 的用户信息，返回 JSON 字符串
// 调用者需 free 返回的 char*
char *fetch_user_info(const char *handle);
char *fetch_contest_list();
char *fetch_user_cflist();
char *fetch_contest_standings(const char *handle, int contestId);
char* http_get(const char* url);
// char **fetch_contest_standings_multi(const char *handle, const int *contestIds, int count);
#endif // API_H
