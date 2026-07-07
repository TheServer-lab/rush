#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include "rush.h"
#include "users.h"
#include "sha256.h"

#ifdef _WIN32
  #include <conio.h>
#else
  #include <termios.h>
  #include <unistd.h>
#endif

char g_current_user[RUSH_MAX_USERNAME] = "";
char g_current_role[RUSH_MAX_ROLE] = "";
char g_start_dir[1024] = ".";

void users_set_start_dir(const char *dir) {
    strncpy(g_start_dir, dir, sizeof(g_start_dir) - 1);
    g_start_dir[sizeof(g_start_dir) - 1] = '\0';
}

int role_valid(const char *role) {
    return strcmp(role, "guest") == 0 || strcmp(role, "member") == 0 || strcmp(role, "admin") == 0;
}

int role_rank(const char *role) {
    if (strcmp(role, "guest") == 0) return 0;
    if (strcmp(role, "member") == 0) return 1;
    if (strcmp(role, "admin") == 0) return 2;
    return -1;
}

int username_valid(const char *name) {
    if (!*name) return 0;
    for (const char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p)) return 0;
    }
    return (int)strlen(name) < RUSH_MAX_USERNAME;
}

static void ensure_data_dir(void) {
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s/rush_data", g_start_dir);
#ifdef _WIN32
    mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

static void build_path(char *out, size_t sz, const char *filename) {
    snprintf(out, sz, "%s/rush_data/%s", g_start_dir, filename);
}

void build_alias_path(char *out, size_t sz) {
    if (g_current_user[0]) {
        snprintf(out, sz, "%s/rush_data/user/%s/alias.txt", g_start_dir, g_current_user);
    } else {
        snprintf(out, sz, "%s/rush_data/alias.txt", g_start_dir);
    }
}

void build_session_dir(char *out, size_t sz) {
    if (g_current_user[0]) {
        snprintf(out, sz, "%s/rush_data/user/%s/sessions", g_start_dir, g_current_user);
    } else {
        snprintf(out, sz, "%s/rush_data/sessions", g_start_dir);
    }
}

void build_session_path(char *out, size_t sz, const char *name) {
    char dir[900];
    build_session_dir(dir, sizeof(dir));
    snprintf(out, sz, "%s/%s.txt", dir, name);
}

static void mkdir_p(const char *path) {
#ifdef _WIN32
    mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

void ensure_user_dirs(void) {
    char dir[1300];
    snprintf(dir, sizeof(dir), "%s/rush_data", g_start_dir);
    mkdir_p(dir);
    if (g_current_user[0]) {
        snprintf(dir, sizeof(dir), "%s/rush_data/user", g_start_dir);
        mkdir_p(dir);
        snprintf(dir, sizeof(dir), "%s/rush_data/user/%s", g_start_dir, g_current_user);
        mkdir_p(dir);
        snprintf(dir, sizeof(dir), "%s/rush_data/user/%s/sessions", g_start_dir, g_current_user);
        mkdir_p(dir);
    } else {
        snprintf(dir, sizeof(dir), "%s/rush_data/sessions", g_start_dir);
        mkdir_p(dir);
    }
}

/* --- password hashing --- */
static void gen_salt(char *out, size_t len) {
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }
    static const char hexchars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) out[i] = hexchars[rand() % 16];
    out[len] = '\0';
}

static void hash_password(const char *salt, const char *password, char out_hex[65]) {
    char combined[512];
    snprintf(combined, sizeof(combined), "%s%s", salt, password);
    sha256_hex((const unsigned char *)combined, strlen(combined), out_hex);
}

/* --- hidden password entry --- */
void read_hidden_line(char *buf, size_t bufsz) {
#ifdef _WIN32
    size_t i = 0;
    int ch;
    while ((ch = _getch()) != '\r' && ch != '\n') {
        if (ch == '\b') {
            if (i > 0) i--;
        } else if (i < bufsz - 1) {
            buf[i++] = (char)ch;
        }
    }
    buf[i] = '\0';
    printf("\n");
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~((tcflag_t)ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    if (fgets(buf, (int)bufsz, stdin)) {
        buf[strcspn(buf, "\n")] = '\0';
    } else {
        buf[0] = '\0';
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
#endif
}

/* --- users.txt access ---
 * format per line: username:role:salt:hash
 * Simple linear scan; fine at the scale rush is meant for. */

int users_total(void) {
    char path[1100];
    build_path(path, sizeof(path), "users.txt");
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int count = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '\n' && line[0] != '\0') count++;
    }
    fclose(f);
    return count;
}

static int find_user_line(const char *username, char *role_out, char *salt_out, char *hash_out) {
    char path[1100];
    build_path(path, sizeof(path), "users.txt");
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *u = strtok(line, ":");
        char *r = strtok(NULL, ":");
        char *s = strtok(NULL, ":");
        char *h = strtok(NULL, ":");
        if (u && r && s && h && strcmp(u, username) == 0) {
            if (role_out) strcpy(role_out, r);
            if (salt_out) strcpy(salt_out, s);
            if (hash_out) strcpy(hash_out, h);
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

int user_exists(const char *username) {
    return find_user_line(username, NULL, NULL, NULL);
}

/* --- logging --- */
void log_action(const char *username, const char *action) {
    ensure_data_dir();
    char path[1100];
    build_path(path, sizeof(path), "log.txt");
    FILE *f = fopen(path, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char timestr[32];
    strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", lt);
    fprintf(f, "%s %s %s\n", username[0] ? username : "anonymous", action, timestr);
    fclose(f);
}

/* --- account actions --- */
int do_regi(const char *username, const char *role) {
    if (!username_valid(username)) {
        printf("error username must be letters and numbers only\n");
        return 1;
    }
    if (!role_valid(role)) {
        printf("error unknown role %s\n", role);
        return 1;
    }
    if (strcmp(role, "admin") == 0 && users_total() > 0) {
        printf("error only an existing admin can grant admin (use promo)\n");
        return 1;
    }
    if (user_exists(username)) {
        printf("error username %s already taken\n", username);
        return 1;
    }

    printf("password: ");
    fflush(stdout);
    char password[256];
    read_hidden_line(password, sizeof(password));

    char salt[17];
    gen_salt(salt, 16);
    char hash[65];
    hash_password(salt, password, hash);

    ensure_data_dir();
    char path[1100];
    build_path(path, sizeof(path), "users.txt");
    FILE *f = fopen(path, "a");
    if (!f) {
        printf("error could not write to users.txt\n");
        return 1;
    }
    fprintf(f, "%s:%s:%s:%s\n", username, role, salt, hash);
    fclose(f);

    printf("user %s registered as %s\n", username, role);
    return 0;
}

int do_login(const char *username) {
    char role[RUSH_MAX_ROLE], salt[17], hash[65];
    if (!find_user_line(username, role, salt, hash)) {
        log_action("anonymous", "failed login (no such user)");
        printf("error invalid username or password\n");
        return 1;
    }

    printf("password: ");
    fflush(stdout);
    char password[256];
    read_hidden_line(password, sizeof(password));

    char attempt_hash[65];
    hash_password(salt, password, attempt_hash);

    if (strcmp(attempt_hash, hash) != 0) {
        log_action("anonymous", "failed login attempt");
        printf("error invalid username or password\n");
        return 1;
    }

    strncpy(g_current_user, username, sizeof(g_current_user) - 1);
    strncpy(g_current_role, role, sizeof(g_current_role) - 1);
    log_action(g_current_user, "login success");
    alias_load_current();
    printf("logged in as %s (%s)\n", username, role);
    return 0;
}

int do_logout(void) {
    if (!g_current_user[0]) {
        printf("error not logged in\n");
        return 1;
    }
    g_current_user[0] = '\0';
    g_current_role[0] = '\0';
    alias_load_current();
    return 0;
}

int do_promo(const char *actor_role, const char *target_user, const char *new_role) {
    if (!g_current_user[0]) {
        printf("error not logged in\n");
        return 1;
    }
    if (!actor_role || strcmp(actor_role, "admin") != 0) {
        printf("error insufficient permission\n");
        return 1;
    }
    if (!role_valid(new_role)) {
        printf("error unknown role %s\n", new_role);
        return 1;
    }
    if (!user_exists(target_user)) {
        printf("error user %s not found\n", target_user);
        return 1;
    }

    /* rewrite users.txt, replacing the target's role field */
    char path[1100];
    build_path(path, sizeof(path), "users.txt");
    FILE *f = fopen(path, "r");
    if (!f) { printf("error could not read users.txt\n"); return 1; }

    char tmp_path[1100];
    build_path(tmp_path, sizeof(tmp_path), "users.txt.tmp");
    FILE *out = fopen(tmp_path, "w");
    if (!out) { fclose(f); printf("error could not write users.txt\n"); return 1; }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char linecopy[512];
        strncpy(linecopy, line, sizeof(linecopy) - 1);
        linecopy[sizeof(linecopy)-1] = '\0';
        linecopy[strcspn(linecopy, "\n")] = '\0';

        char check[512];
        strncpy(check, linecopy, sizeof(check) - 1);
        check[sizeof(check)-1] = '\0';
        char *u = strtok(check, ":");
        char *r = strtok(NULL, ":");
        char *s = strtok(NULL, ":");
        char *h = strtok(NULL, ":");
        if (u && r && s && h && strcmp(u, target_user) == 0) {
            fprintf(out, "%s:%s:%s:%s\n", u, new_role, s, h);
        } else {
            fprintf(out, "%s\n", linecopy);
        }
    }
    fclose(f);
    fclose(out);
    remove(path);
    rename(tmp_path, path);

    char logmsg[256];
    snprintf(logmsg, sizeof(logmsg), "promoted %s to %s", target_user, new_role);
    log_action(g_current_user, logmsg);
    printf("%s is now %s\n", target_user, new_role);
    return 0;
}

int do_del_user(const char *actor_role, const char *target_user) {
    if (!g_current_user[0]) { printf("error not logged in\n"); return 1; }
    if (!actor_role || strcmp(actor_role, "admin") != 0) { printf("error insufficient permission\n"); return 1; }
    if (!user_exists(target_user)) { printf("error user %s not found\n", target_user); return 1; }
    if (strcmp(target_user, g_current_user) == 0) {
        printf("error cannot delete the account you are currently logged in as\n");
        return 1;
    }

    char path[1100], tmp_path[1100];
    build_path(path, sizeof(path), "users.txt");
    build_path(tmp_path, sizeof(tmp_path), "users.txt.tmp");
    FILE *f = fopen(path, "r");
    if (!f) { printf("error could not read users.txt\n"); return 1; }
    FILE *out = fopen(tmp_path, "w");
    if (!out) { fclose(f); printf("error could not write users.txt\n"); return 1; }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char check[512];
        strncpy(check, line, sizeof(check) - 1);
        check[sizeof(check)-1] = '\0';
        check[strcspn(check, "\n")] = '\0';
        char *u = strtok(check, ":");
        if (!(u && strcmp(u, target_user) == 0)) {
            fprintf(out, "%s", line);
            if (line[strlen(line)-1] != '\n') fprintf(out, "\n");
        }
    }
    fclose(f);
    fclose(out);
    remove(path);
    rename(tmp_path, path);

    char logmsg[256];
    snprintf(logmsg, sizeof(logmsg), "deleted user %s", target_user);
    log_action(g_current_user, logmsg);
    printf("user %s deleted\n", target_user);
    return 0;
}

static int count_admins(void) {
    char path[1100];
    build_path(path, sizeof(path), "users.txt");
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *u = strtok(line, ":");
        char *r = strtok(NULL, ":");
        if (u && r && strcmp(r, "admin") == 0) count++;
    }
    fclose(f);
    return count;
}

int do_demo(const char *actor_role, const char *target_user) {
    if (!g_current_user[0]) { printf("error not logged in\n"); return 1; }
    if (!actor_role || strcmp(actor_role, "admin") != 0) { printf("error insufficient permission\n"); return 1; }
    char role[RUSH_MAX_ROLE], salt[17], hash[65];
    if (!find_user_line(target_user, role, salt, hash)) {
        printf("error user %s not found\n", target_user);
        return 1;
    }
    int rank = role_rank(role);
    if (rank <= 0) {
        printf("error %s is already at the lowest role\n", target_user);
        return 1;
    }
    if (rank == 2 && count_admins() <= 1) {
        printf("error cannot demote the only remaining admin\n");
        return 1;
    }
    const char *new_role = (rank == 2) ? "member" : "guest";
    return do_promo("admin", target_user, new_role);
}

int list_all_users(char usernames[][RUSH_MAX_USERNAME], char roles[][RUSH_MAX_ROLE], int max) {
    char path[1100];
    build_path(path, sizeof(path), "users.txt");
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *u = strtok(line, ":");
        char *r = strtok(NULL, ":");
        if (u && r) {
            strncpy(usernames[n], u, RUSH_MAX_USERNAME - 1);
            usernames[n][RUSH_MAX_USERNAME - 1] = '\0';
            strncpy(roles[n], r, RUSH_MAX_ROLE - 1);
            roles[n][RUSH_MAX_ROLE - 1] = '\0';
            n++;
        }
    }
    fclose(f);
    return n;
}
