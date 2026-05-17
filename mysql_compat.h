#ifndef MYSQL_COMPAT_H
#define MYSQL_COMPAT_H

extern "C" {

struct st_mysql;
struct st_mysql_res;
typedef st_mysql MYSQL;
typedef st_mysql_res MYSQL_RES;

MYSQL* mysql_init(MYSQL* mysql);
void mysql_server_end(void);
MYSQL* mysql_real_connect(MYSQL* mysql,
                          const char* host,
                          const char* user,
                          const char* passwd,
                          const char* db,
                          unsigned int port,
                          const char* unix_socket,
                          unsigned long clientflag);
void mysql_close(MYSQL* sock);
const char* mysql_error(MYSQL* mysql);
int mysql_query(MYSQL* mysql, const char* q);
MYSQL_RES* mysql_store_result(MYSQL* mysql);
void mysql_free_result(MYSQL_RES* result);
unsigned long long mysql_num_rows(MYSQL_RES* res);
unsigned long mysql_real_escape_string(MYSQL* mysql, char* to, const char* from, unsigned long length);

}

#endif // MYSQL_COMPAT_H
