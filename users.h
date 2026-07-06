#ifndef RUSH_USERS_H
#define RUSH_USERS_H

#define RUSH_MAX_USERNAME 64
#define RUSH_MAX_ROLE 16

/* Current session's logged-in user, empty string if nobody is logged in. */
extern char g_current_user[RUSH_MAX_USERNAME];
extern char g_current_role[RUSH_MAX_ROLE];

/* Captured once at program start, before any 'goin' can move the
 * working directory - rush_data always lives relative to here. */
extern char g_start_dir[1024];

void users_set_start_dir(const char *dir);

/* 0 = a role name is one of guest/member/admin */
int role_valid(const char *role);
/* returns 0/1/2 for guest/member/admin, -1 if invalid */
int role_rank(const char *role);

/* username: letters/digits only */
int username_valid(const char *name);

/* Returns 1 if the user exists in rush_data/users.txt */
int user_exists(const char *username);

/* Total number of registered users (0 if the file doesn't exist yet). */
int users_total(void);

/* Reads a line of input with the terminal echo turned off. */
void read_hidden_line(char *buf, size_t bufsz);

/* --- account actions, each returns 0 on success --- */
int do_regi(const char *username, const char *role);
int do_login(const char *username);
int do_logout(void);
int do_promo(const char *actor_role, const char *target_user, const char *new_role);

/* --- logging --- */
void log_action(const char *username, const char *action);

#endif
