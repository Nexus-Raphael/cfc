#ifndef API_H
#define API_H

// 从 Codeforces 读取给定 handle 的用户信息，返回 JSON 字符串
// 调用者需 free 返回的 char*
char *fetch_user_info(const char *handle);
char *fetch_contest_list();
#endif // API_H
