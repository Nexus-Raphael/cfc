#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include <cJSON.h>
#include <time.h>
#include <locale.h>
#include <stdbool.h>

char handle[30];

const char* get_rank_name(int rating) {
    if (rating < 1200) return "Newbie";
    if (rating < 1400) return "Pupil";
    if (rating < 1600) return "Specialist";
    if (rating < 1900) return "Expert";
    if (rating < 2100) return "Candidate Master";
    if (rating < 2300) return "Master";
    if (rating < 2400) return "International Master";
    if (rating < 2600) return "Grandmaster";
    if (rating < 3000) return "International Grandmaster";
    return "Legendary Grandmaster";
}

void upload_web_and_open(const char *url_to_open) {
    int ret = system("scp -r web root@121.196.195.201:/usr/share/nginx/html/");
    if (ret != 0) {
        fprintf(stderr, "上传 web 目录失败，请检查网络或免密登录配置。\n");
    } else {
        printf("✅ 上传成功！打开网页：%s\n", url_to_open);

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "start %s", url_to_open);  // Windows 专用
        system(cmd);
    }
}


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

void yearlyCFlist() {
    int year_to_filter=2025;
    setlocale(LC_TIME, "");

    // 1. 拉下全部比赛 JSON
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

    // 2. 创建 5 个组数组
    cJSON *out = cJSON_CreateObject();
    cJSON *arrDiv1 = cJSON_CreateArray();
    cJSON *arrDiv2 = cJSON_CreateArray();
    cJSON *arrDiv3 = cJSON_CreateArray();
    cJSON *arrDiv4 = cJSON_CreateArray();
    cJSON *arrEdu  = cJSON_CreateArray();

    int cnt = cJSON_GetArraySize(result);
    for (int i = 0; i < cnt; ++i) {
        cJSON *ct = cJSON_GetArrayItem(result, i);
        if (!ct) continue;

        // 仅处理已结束的比赛
        cJSON *phase = cJSON_GetObjectItem(ct, "phase");
        if (!cJSON_IsString(phase) || strcmp(phase->valuestring, "FINISHED") != 0) {
            continue;
        }

        // 年份检查：遇到第一个非本年度，提前退出
        cJSON *start = cJSON_GetObjectItem(ct, "startTimeSeconds");
        time_t st = (time_t)start->valuedouble;
        struct tm *t = localtime(&st);
        if ((t->tm_year + 1900) != year_to_filter) {
            break;
        }

        // 构造输出项，只取 id/name/start/division
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", cJSON_GetObjectItem(ct,"id")->valueint);
        cJSON_AddStringToObject(item, "name", cJSON_GetObjectItem(ct,"name")->valuestring);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        cJSON_AddStringToObject(item, "startTime", buf);

        // 判断是 Educational 还是 Div.X
        const char *nm = cJSON_GetObjectItem(ct, "name")->valuestring;
        char *p = strstr(nm, "(Div. ");
        if (p) {
            // p 指向 "(Div. X...)"，直接解析
            int divnum = 0;
            if (sscanf(p, "(Div. %d)", &divnum) == 1) {
                // 正常的 Div 1-4
                switch (divnum) {
                    case 1: cJSON_AddItemToArray(arrDiv1, item); break;
                    case 2: cJSON_AddItemToArray(arrDiv2, item); break;
                    case 3: cJSON_AddItemToArray(arrDiv3, item); break;
                    case 4: cJSON_AddItemToArray(arrDiv4, item); break;
                    default: cJSON_Delete(item); break;
                }
            } else {
                // 虽然有 "(Div. " 字样，但解析失败也丢弃
                cJSON_Delete(item);
            }
        } else if (strstr(nm, "Educational") != NULL) {
            // 教育赛
            cJSON_AddItemToArray(arrEdu, item);
        } else {
            // 其它不关心的 Round
            cJSON_Delete(item);
        }

    }
    // 3. 挂到顶层对象
    cJSON_AddItemToObject(out, "Div1", arrDiv1);
    cJSON_AddItemToObject(out, "Div2", arrDiv2);
    cJSON_AddItemToObject(out, "Div3", arrDiv3);
    cJSON_AddItemToObject(out, "Div4", arrDiv4);
    cJSON_AddItemToObject(out, "Educational", arrEdu);

    // 4. 写文件
    char *out_str = cJSON_Print(out);
    FILE *fp = fopen("web/data.json", "w");
    if (fp) {
        fprintf(fp, "%s", out_str);
        fclose(fp);
    } else {
        perror("无法打开 web/data.json");
    }
    free(out_str);
    cJSON_Delete(out);
    cJSON_Delete(root);

    printf("已生成 web/data.json，包含 %d 年比赛分组数据。\n", year_to_filter);
    upload_web_and_open("http://121.196.195.201/web/etest.html");

}

void patiCFlist() {
    char *json = fetch_user_cflist(handle);
    if (!json) {
        fprintf(stderr, "获取用户参加过的比赛列表失败。\n");
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

    // 创建输出对象和分组数组
    cJSON *out = cJSON_CreateObject();
    cJSON *arrDiv1 = cJSON_CreateArray();
    cJSON *arrDiv2 = cJSON_CreateArray();
    cJSON *arrDiv3 = cJSON_CreateArray();
    cJSON *arrDiv4 = cJSON_CreateArray();
    cJSON *arrEdu  = cJSON_CreateArray();

    int cnt = cJSON_GetArraySize(result);
    for (int i = 0; i < cnt; ++i) {
        cJSON *entry = cJSON_GetArrayItem(result, i);
        if (!entry) continue;

        int cid = cJSON_GetObjectItem(entry, "contestId")->valueint;
        const char *cname = cJSON_GetObjectItem(entry, "contestName")->valuestring;
        time_t rating_time = (time_t)cJSON_GetObjectItem(entry, "ratingUpdateTimeSeconds")->valueint;
        struct tm *t = localtime(&rating_time);

        // 构造输出项
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", cid);
        cJSON_AddStringToObject(item, "name", cname);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        cJSON_AddStringToObject(item, "ratingUpdateTime", buf);

        // 按类别分类（Educational 或 Div. X）
        const char *nm = cname;
        char *p = strstr(nm, "(Div. ");
        if (p) {
            int divnum = 0;
            if (sscanf(p, "(Div. %d)", &divnum) == 1) {
                switch (divnum) {
                    case 1: cJSON_AddItemToArray(arrDiv1, item); break;
                    case 2: cJSON_AddItemToArray(arrDiv2, item); break;
                    case 3: cJSON_AddItemToArray(arrDiv3, item); break;
                    case 4: cJSON_AddItemToArray(arrDiv4, item); break;
                    default: cJSON_Delete(item); break;
                }
            } else {
                cJSON_Delete(item);
            }
        } else if (strstr(nm, "Educational") != NULL) {
            cJSON_AddItemToArray(arrEdu, item);
        } else {
            cJSON_Delete(item); // 其他比赛类型不处理
        }
    }

    // 添加到顶层对象
    cJSON_AddItemToObject(out, "Div1", arrDiv1);
    cJSON_AddItemToObject(out, "Div2", arrDiv2);
    cJSON_AddItemToObject(out, "Div3", arrDiv3);
    cJSON_AddItemToObject(out, "Div4", arrDiv4);
    cJSON_AddItemToObject(out, "Educational", arrEdu);

    // 输出到文件
    char *out_str = cJSON_Print(out);
    FILE *fp = fopen("web/user_data.json", "w");
    if (fp) {
        fprintf(fp, "%s", out_str);
        fclose(fp);
    } else {
        perror("无法打开 web/user_data.json");
    }

    free(out_str);
    cJSON_Delete(out);
    cJSON_Delete(root);

    printf("已生成 web/user_data.json，包含该用户参与的比赛分组数据。\n");
    upload_web_and_open("http://121.196.195.201/web/ucflist.html");

}

void userevo() {
    char *json_list = fetch_user_cflist(handle);
    if (!json_list) {
        fprintf(stderr, "获取用户 %s 的比赛列表失败。\n", handle);
        return;
    }

    cJSON *root = cJSON_Parse(json_list);
    free(json_list);
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

    int count = cJSON_GetArraySize(result);
    if (count == 0) {
        fprintf(stderr, "错误：用户 %s 未参加任何比赛或用户名无效！\n", handle);
        cJSON_Delete(root);
        return;
    }

    cJSON *out_arr = cJSON_CreateArray();
    printf("找到 %d 场参赛记录\n", count);

    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_GetArrayItem(result, i);
        if (!entry) continue;

        int contestId = cJSON_GetObjectItem(entry, "contestId")->valueint;
        const char *contestName = cJSON_GetObjectItem(entry, "contestName")->valuestring;
        int oldRating = cJSON_GetObjectItem(entry, "oldRating") ? cJSON_GetObjectItem(entry, "oldRating")->valueint : 0;
        int newRating = cJSON_GetObjectItem(entry, "newRating") ? cJSON_GetObjectItem(entry, "newRating")->valueint : 0;
        int ratingUpdateTime = 0;
        cJSON *upd_time = cJSON_GetObjectItem(entry, "ratingUpdateTimeSeconds");
        if (upd_time && cJSON_IsNumber(upd_time)) {
            ratingUpdateTime = upd_time->valueint;
        }

        printf("处理第 %d/%d 场比赛：%s (ID=%d)\n", i + 1, count, contestName, contestId);

        char *json_standings = fetch_contest_standings(handle, contestId);
        if (!json_standings) {
            fprintf(stderr, "获取比赛 %d 的详细数据失败，跳过...\n", contestId);
            continue;
        }

        cJSON *standings_root = cJSON_Parse(json_standings);
        free(json_standings);
        if (!standings_root) {
            fprintf(stderr, "比赛 %d JSON 解析失败，跳过...\n", contestId);
            continue;
        }

        cJSON *status2 = cJSON_GetObjectItem(standings_root, "status");
        if (!cJSON_IsString(status2) || strcmp(status2->valuestring, "OK") != 0) {
            fprintf(stderr, "比赛 %d API 返回状态异常，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *res2 = cJSON_GetObjectItem(standings_root, "result");
        if (!res2) {
            fprintf(stderr, "比赛 %d result 缺失，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *problems = cJSON_GetObjectItem(res2, "problems");
        cJSON *rows = cJSON_GetObjectItem(res2, "rows");
        if (!problems || !rows) {
            fprintf(stderr, "比赛 %d 缺失 problems 或 rows，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *user_row = NULL;
        int rows_count = cJSON_GetArraySize(rows);
        for (int j = 0; j < rows_count; j++) {
            cJSON *row = cJSON_GetArrayItem(rows, j);
            cJSON *party = cJSON_GetObjectItem(row, "party");
            if (!party) continue;
            cJSON *members = cJSON_GetObjectItem(party, "members");
            if (!members || cJSON_GetArraySize(members) == 0) continue;

            cJSON *member0 = cJSON_GetArrayItem(members, 0);
            cJSON *m_handle = cJSON_GetObjectItem(member0, "handle");
            if (m_handle && strcmp(m_handle->valuestring, handle) == 0) {
                user_row = row;
                break;
            }
        }

        if (!user_row) {
            fprintf(stderr, "比赛 %d 未找到用户行，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        int rank = cJSON_GetObjectItem(user_row, "rank")->valueint;
        cJSON *problemResults = cJSON_GetObjectItem(user_row, "problemResults");
        int problem_count = cJSON_GetArraySize(problems);

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "contestId", contestId);
        cJSON_AddStringToObject(obj, "contestName", contestName);
        cJSON_AddNumberToObject(obj, "ratingUpdateTime", ratingUpdateTime);
        cJSON_AddNumberToObject(obj, "rank", rank);
        cJSON_AddNumberToObject(obj, "oldRating", oldRating);
        cJSON_AddNumberToObject(obj, "newRating", newRating);
        cJSON_AddStringToObject(obj, "rankBefore", get_rank_name(oldRating));
        cJSON_AddStringToObject(obj, "rankAfter", get_rank_name(newRating));

        cJSON *problems_arr = cJSON_CreateArray();
        bool isSimpleContest = strstr(contestName, "Div. 3") || strstr(contestName, "Div. 4") || strstr(contestName, "Educational");
        for (int k = 0; k < problem_count; k++) {
            cJSON *problem = cJSON_GetArrayItem(problems, k);
            if (!problem) continue;

            const char *index = cJSON_GetObjectItem(problem, "index")->valuestring;

            cJSON *pr = (problemResults && k < cJSON_GetArraySize(problemResults)) ? cJSON_GetArrayItem(problemResults, k) : NULL;

            if (isSimpleContest) {
            // 只记录 AC 的题目 index
            bool accepted = false;
            if (pr) {
                cJSON *pr_pts = cJSON_GetObjectItem(pr, "points");
                accepted = (pr_pts && cJSON_IsNumber(pr_pts) && pr_pts->valuedouble > 0.0);
            }
            if (accepted) {
                cJSON_AddItemToArray(problems_arr, cJSON_CreateString(index));
            }
            } else {
                // 保留详细得分信息
                cJSON *prob_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(prob_obj, "index", index);

                cJSON *p_pts = cJSON_GetObjectItem(problem, "points");
                int maxPoints = (p_pts && cJSON_IsNumber(p_pts)) ? (int)p_pts->valuedouble : 0;
                cJSON_AddNumberToObject(prob_obj, "maxPoints", maxPoints);

                int userPoints = 0;
                if (pr) {
                    cJSON *pr_pts = cJSON_GetObjectItem(pr, "points");
                    if (pr_pts && cJSON_IsNumber(pr_pts)) {
                        userPoints = (int)pr_pts->valuedouble;
                    }
                }
                cJSON_AddNumberToObject(prob_obj, "points", userPoints);
                cJSON_AddItemToArray(problems_arr, prob_obj);
            }
        }

        cJSON_AddItemToObject(obj, "problems", problems_arr);
        cJSON_AddItemToArray(out_arr, obj);

        cJSON_Delete(standings_root);
    }

    // 写出 JSON 文件
    char *out_str = cJSON_Print(out_arr);
    FILE *fp = fopen("web/user_evolution.json", "w");
    if (fp) {
        fprintf(fp, "%s", out_str);
        fclose(fp);
        printf("✅ 已生成用户参赛进化数据：web/user_evolution.json\n");
    } else {
        perror("❌ 写入文件失败");
    }

    free(out_str);
    cJSON_Delete(out_arr);
    cJSON_Delete(root);
}
void userevo0() {
    char *json_list = fetch_user_cflist(handle);
    if (!json_list) {
        fprintf(stderr, "获取用户 %s 的比赛列表失败。\n", handle);
        return;
    }

    cJSON *root = cJSON_Parse(json_list);
    free(json_list);
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

    int count = cJSON_GetArraySize(result);
    cJSON *out_arr = cJSON_CreateArray();
    //----------------------------
    if (count == 0) { // 新增检查
        fprintf(stderr, "错误：用户 %s 未参加任何比赛或用户名无效！\n", handle);
        cJSON_Delete(root);
        return;
    }
    printf("找到 %d 场参赛记录\n", count);
    //----------------------------
    for (int i = 0; i < count; i++) {
        printf("处理第 %d/%d 场比赛...\n", i+1, count);
        cJSON *entry = cJSON_GetArrayItem(result, i);
        if (!entry) continue;

        int contestId = cJSON_GetObjectItem(entry, "contestId")->valueint;
        int oldRating = cJSON_GetObjectItem(entry, "oldRating")->valueint;
        int newRating = cJSON_GetObjectItem(entry, "newRating")->valueint;
        const char *contestName = cJSON_GetObjectItem(entry, "contestName")->valuestring;
        int ratingUpdateTime = cJSON_GetObjectItem(entry, "ratingUpdateTimeSeconds")->valueint;

        // 调用比赛详细数据接口
        char *json_standings = fetch_contest_standings(handle, contestId);
        if (!json_standings) {
            fprintf(stderr, "获取比赛 %d 详细数据失败，跳过...\n", contestId);
            continue;
        }

        cJSON *standings_root = cJSON_Parse(json_standings);
        free(json_standings);
        if (!standings_root) {
            fprintf(stderr, "比赛 %d 详细数据 JSON 解析失败，跳过...\n", contestId);
            continue;
        }

        cJSON *status2 = cJSON_GetObjectItem(standings_root, "status");
        if (!cJSON_IsString(status2) || strcmp(status2->valuestring, "OK") != 0) {
            fprintf(stderr, "比赛 %d API返回异常，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *res2 = cJSON_GetObjectItem(standings_root, "result");
        if (!res2) {
            fprintf(stderr, "比赛 %d 结果缺失，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        cJSON *contest = cJSON_GetObjectItem(res2, "contest");
        cJSON *problems = cJSON_GetObjectItem(res2, "problems");
        cJSON *rows = cJSON_GetObjectItem(res2, "rows");
        if (!contest || !problems || !rows) {
            fprintf(stderr, "比赛 %d 关键信息缺失，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        // 找到当前用户所在行
        int rows_count = cJSON_GetArraySize(rows);
        cJSON *user_row = NULL;
        for (int j = 0; j < rows_count; j++) {
            cJSON *row = cJSON_GetArrayItem(rows, j);
            cJSON *party = cJSON_GetObjectItem(row, "party");
            if (!party) continue;

            cJSON *members = cJSON_GetObjectItem(party, "members");
            if (!members || cJSON_GetArraySize(members) == 0) continue;

            cJSON *member0 = cJSON_GetArrayItem(members, 0);
            cJSON *m_handle = cJSON_GetObjectItem(member0, "handle");
            if (m_handle && strcmp(m_handle->valuestring, handle) == 0) {
                user_row = row;
                break;
            }
        }

        if (!user_row) {
            fprintf(stderr, "比赛 %d 未找到用户排名，跳过...\n", contestId);
            cJSON_Delete(standings_root);
            continue;
        }

        int rank = cJSON_GetObjectItem(user_row, "rank")->valueint;
        cJSON *problemResults = cJSON_GetObjectItem(user_row, "problemResults");
        int problem_count = cJSON_GetArraySize(problems);

        // 构造比赛对象
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "contestId", contestId);
        cJSON_AddStringToObject(obj, "contestName", contestName);
        cJSON_AddNumberToObject(obj, "ratingUpdateTime", ratingUpdateTime);
        cJSON_AddNumberToObject(obj, "rank", rank);

        // 头衔变化
        cJSON_AddStringToObject(obj, "rankBefore", get_rank_name(oldRating));
        cJSON_AddStringToObject(obj, "rankAfter", get_rank_name(newRating));

        // rating变化
        cJSON_AddNumberToObject(obj, "oldRating", oldRating);
        cJSON_AddNumberToObject(obj, "newRating", newRating);

        // 题目得分数组
        cJSON *problems_arr = cJSON_CreateArray();
        for (int k = 0; k < problem_count; k++) {
            cJSON *problem = cJSON_GetArrayItem(problems, k);
            cJSON *pr = cJSON_GetArrayItem(problemResults, k);

            cJSON *prob_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(prob_obj, "index", cJSON_GetObjectItem(problem, "index")->valuestring);
            cJSON_AddNumberToObject(prob_obj, "maxPoints", (int)(cJSON_GetObjectItem(problem, "points")->valuedouble));
            cJSON_AddNumberToObject(prob_obj, "points", (int)(cJSON_GetObjectItem(pr, "points")->valuedouble));

            cJSON_AddItemToArray(problems_arr, prob_obj);
        }
        cJSON_AddItemToObject(obj, "problems", problems_arr);

        cJSON_AddItemToArray(out_arr, obj);

        cJSON_Delete(standings_root);
    }

    // 写出最终JSON文件
    char *out_str = cJSON_Print(out_arr);
    FILE *fp = fopen("web/user_evolution.json", "w");
    if (fp) {
        fprintf(fp, "%s", out_str);
        fclose(fp);
        printf("已生成用户参赛进化数据：web/user_evolution.json\n");
    } else {
        perror("写入文件失败");
    }

    free(out_str);
    cJSON_Delete(out_arr);
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
        patiCFlist();
        break;
    case 4:
        userevo();
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
    // system("start \"\" \"web\\etest.html\"");//加了这一行
    return 0;
}
