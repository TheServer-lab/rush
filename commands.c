#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "rush.h"
#include "commands.h"
#include "net.h"
#include "users.h"

extern int g_script_stop; /* set by 'quit' to stop the current script/block, see interp.c */

static int is_quoted(const char *s) { return s[0] == '"'; }

static char *strip_quotes(const char *s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        char *out = malloc(len - 1);
        memcpy(out, s + 1, len - 2);
        out[len - 2] = '\0';
        return out;
    }
    return strdup(s);
}

/* Resolve a single operand token to a Value: quoted string (with
 * interpolation), int literal, or bare-word variable lookup. Sets
 * *err to 1 if resolution failed (error already printed). */
Value resolve_operand(const char *tok, int *err) {
    Value v; v.type = VAL_INT; v.ival = 0; v.sval = NULL;
    *err = 0;

    if (is_quoted(tok)) {
        char *inner = strip_quotes(tok);
        int ok;
        char *expanded = interpolate(inner, &ok);
        free(inner);
        if (!ok) { *err = 1; return v; }
        v.type = VAL_STRING;
        v.sval = expanded;
        return v;
    }

    /* integer literal? */
    int i = 0;
    int neg = 0;
    if (tok[0] == '-' && isdigit((unsigned char)tok[1])) { neg = 1; i = 1; }
    int all_digit = (tok[i] != '\0');
    for (; tok[i]; i++) if (!isdigit((unsigned char)tok[i])) { all_digit = 0; break; }
    if (all_digit) {
        v.type = VAL_INT;
        v.ival = atol(tok);
        (void)neg;
        return v;
    }

    /* bare word: variable lookup */
    Variable *var = var_find(tok);
    if (!var) {
        rush_error("var %s not found", tok);
        *err = 1;
        return v;
    }
    if (var->value.type == VAL_STRING) {
        v.type = VAL_STRING;
        v.sval = strdup(var->value.sval);
    } else {
        v.type = VAL_INT;
        v.ival = var->value.ival;
    }
    return v;
}

/* ---------- show ---------- */
ExecResult cmd_show(char **args, int argc, Value *piped) {
    ExecResult r = {0, {VAL_INT, 0, NULL}, 0};
    if (argc >= 2) {
        int err;
        Value v = resolve_operand(args[1], &err);
        if (err) { r.ok = 1; return r; }
        if (!g_suppress_stage_output) {
            char *disp = value_to_display(&v);
            printf("%s\n", disp);
            free(disp);
        }
        r.value = v;
        r.has_value = 1;
    } else if (piped) {
        if (!g_suppress_stage_output) {
            char *disp = value_to_display(piped);
            printf("%s\n", disp);
            free(disp);
        }
    } else if (!g_suppress_stage_output) {
        printf("\n");
    }
    return r;
}

/* ---------- calc ---------- */
ExecResult cmd_calc(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT, 0, NULL}, 0};
    if (argc != 4) {
        rush_error("calc expects <value> <operator> <value>");
        r.ok = 1;
        return r;
    }
    int err1, err2;
    Value a = resolve_operand(args[1], &err1);
    const char *op = args[2];
    Value b = resolve_operand(args[3], &err2);
    if (err1 || err2) { r.ok = 1; return r; }

    if (a.type != VAL_INT || b.type != VAL_INT) {
        rush_error("string and int collision");
        value_free(&a); value_free(&b);
        r.ok = 1;
        return r;
    }

    long result;
    if (strcmp(op, "+") == 0) result = a.ival + b.ival;
    else if (strcmp(op, "-") == 0) result = a.ival - b.ival;
    else if (strcmp(op, "*") == 0) result = a.ival * b.ival;
    else if (strcmp(op, "/") == 0) {
        if (b.ival == 0) { rush_error("divide by zero"); r.ok = 1; return r; }
        result = a.ival / b.ival;
    } else {
        rush_error("unknown operator %s", op);
        r.ok = 1;
        return r;
    }
    if (!g_suppress_stage_output) printf("%ld\n", result);
    r.value.type = VAL_INT;
    r.value.ival = result;
    r.has_value = 1;
    return r;
}

/* ---------- where / goin ---------- */
ExecResult cmd_where(char **args, int argc, Value *piped) {
    (void)args; (void)argc; (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    char buf[RUSH_MAX_LINE];
    if (getcwd(buf, sizeof(buf))) printf("%s\n", buf);
    return r;
}

ExecResult cmd_goin(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("goin expects a path"); r.ok = 1; return r; }

    char *path;
    if (!is_quoted(args[1]) && strcmp(args[1], "home") == 0) {
#ifdef _WIN32
        const char *home = getenv("USERPROFILE");
#else
        const char *home = getenv("HOME");
#endif
        if (!home) { rush_error("could not determine home directory"); r.ok = 1; return r; }
        path = strdup(home);
    } else {
        path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    }

    if (chdir(path) != 0) {
        rush_error("path %s not found", path);
        r.ok = 1;
    }
    free(path);
    return r;
}

/* ---------- list ---------- */
static void list_dir(const char *path, int info, int every, int depth) {
    DIR *d = opendir(path);
    if (!d) { rush_error("path %s not found", path); return; }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[RUSH_MAX_LINE];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        stat(full, &st);
        for (int i = 0; i < depth; i++) printf("  ");
        if (info) {
            printf("%s\t%ld bytes\t%s\n", ent->d_name, (long)st.st_size,
                   S_ISDIR(st.st_mode) ? "dir" : "file");
        } else {
            printf("%s%s\n", ent->d_name, S_ISDIR(st.st_mode) ? "/" : "");
        }
        if (every && S_ISDIR(st.st_mode)) list_dir(full, info, every, depth + 1);
    }
    closedir(d);
}

ExecResult cmd_list(char **args, int argc, TokenList *tl, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    const char *path = argc >= 2 ? args[1] : ".";
    char *clean = (argc >= 2 && is_quoted(args[1])) ? strip_quotes(args[1]) : strdup(path);
    list_dir(clean, has_flag(tl, "info"), has_flag(tl, "every"), 0);
    free(clean);
    return r;
}

/* ---------- read ---------- */
ExecResult cmd_read(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("read expects a file"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    FILE *f = fopen(path, "r");
    if (!f) { rush_error("file %s not found", path); free(path); r.ok = 1; return r; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) fwrite(buf, 1, n, stdout);
    fclose(f);
    free(path);
    return r;
}

/* ---------- about ---------- */
ExecResult cmd_about(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("about expects a target"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    struct stat st;
    if (stat(path, &st) != 0) { rush_error("file %s not found", path); free(path); r.ok = 1; return r; }
    printf("name: %s\n", path);
    printf("type: %s\n", S_ISDIR(st.st_mode) ? "folder" : "file");
    printf("size: %ld bytes\n", (long)st.st_size);
    free(path);
    return r;
}

/* ---------- del ---------- */
static int remove_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char full[RUSH_MAX_LINE];
            snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
            remove_recursive(full);
        }
        closedir(d);
        return rmdir(path);
    }
    return remove(path);
}

ExecResult cmd_del(char **args, int argc, TokenList *tl, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("del expects a target"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    int every = has_flag(tl, "every");
    int force = has_flag(tl, "force");
    int rc;
    if (every) rc = remove_recursive(path);
    else rc = remove(path) == 0 ? 0 : (rmdir(path) == 0 ? 0 : -1);
    if (rc != 0 && !force) { rush_error("could not delete %s", path); r.ok = 1; }
    free(path);
    return r;
}

/* ---------- mkf / mkfl ---------- */
ExecResult cmd_mkf(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("mkf expects a folder name"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
#ifdef _WIN32
    if (mkdir(path) != 0) { rush_error("could not create folder %s", path); r.ok = 1; }
#else
    if (mkdir(path, 0755) != 0) { rush_error("could not create folder %s", path); r.ok = 1; }
#endif
    free(path);
    return r;
}

ExecResult cmd_mkfl(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("mkfl expects a file name"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    FILE *f = fopen(path, "w");
    if (!f) { rush_error("could not create file %s", path); r.ok = 1; }
    else fclose(f);
    free(path);
    return r;
}

/* ---------- write / owrite ---------- */
static ExecResult do_write(char **args, int argc, const char *mode) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 3) { rush_error("write expects a file and text"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    int err;
    Value v = resolve_operand(args[2], &err);
    if (err) { free(path); r.ok = 1; return r; }
    char *text = value_to_display(&v);
    FILE *f = fopen(path, mode);
    if (!f) { rush_error("could not write to %s", path); r.ok = 1; }
    else { fprintf(f, "%s\n", text); fclose(f); }
    free(path); free(text); value_free(&v);
    return r;
}

ExecResult cmd_write(char **args, int argc, Value *piped) { (void)piped; return do_write(args, argc, "a"); }
ExecResult cmd_owrite(char **args, int argc, Value *piped) { (void)piped; return do_write(args, argc, "w"); }

/* ---------- time ---------- */
ExecResult cmd_time(char **args, int argc, Value *piped) {
    (void)args; (void)argc; (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M:%S", lt);
    printf("%s\n", buf);
    return r;
}

/* ---------- find ---------- */
static void find_in(const char *path, const char *needle, int every) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[RUSH_MAX_LINE];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        if (strstr(ent->d_name, needle)) printf("%s\n", full);
        struct stat st;
        stat(full, &st);
        if (every && S_ISDIR(st.st_mode)) find_in(full, needle, every);
    }
    closedir(d);
}

ExecResult cmd_find(char **args, int argc, TokenList *tl, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("find expects a search term"); r.ok = 1; return r; }
    char *needle = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    find_in(".", needle, has_flag(tl, "every"));
    free(needle);
    return r;
}

/* ---------- rname ---------- */
ExecResult cmd_rname(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 3) { rush_error("rname expects old and new names"); r.ok = 1; return r; }
    char *from = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    char *to   = is_quoted(args[2]) ? strip_quotes(args[2]) : strdup(args[2]);
    if (rename(from, to) != 0) { rush_error("could not rename %s", from); r.ok = 1; }
    free(from); free(to);
    return r;
}

/* ---------- wait ---------- */
ExecResult cmd_wait(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("wait expects seconds"); r.ok = 1; return r; }
    sleep(atoi(args[1]));
    return r;
}

/* ---------- ali ---------- */
ExecResult cmd_ali(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count < 4 || strcmp(tl->tokens[2], "=") != 0) {
        rush_error("ali expects: ali <name> = <command>");
        r.ok = 1;
        return r;
    }
    char expansion[RUSH_MAX_LINE] = "";
    for (int i = 3; i < tl->count; i++) {
        strcat(expansion, tl->tokens[i]);
        if (i != tl->count - 1) strcat(expansion, " ");
    }
    alias_set(tl->tokens[1], expansion);
    return r;
}

/* Note: 'auth' privilege checks are now real (see interp.c dispatch()),
 * gated on the logged-in user's role rather than a declarative stub. */

/* ---------- bounce (single-shot or repeated network reachability test) ---------- */
ExecResult cmd_bounce(char **args, int argc, Value *piped) {
    (void)piped;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("bounce expects a url"); r.ok = 1; return r; }
    char *raw = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);

    int attempts = 1;
    if (argc >= 3) {
        attempts = atoi(args[2]);
        if (attempts < 1) attempts = 1;
    } else if (piped && piped->type == VAL_INT && piped->ival > 0) {
        /* no explicit count given inline - a piped number becomes the
         * attempt count, e.g. calc 3 * 3 ~ bounce https://example.com */
        attempts = (int)piped->ival;
    }

    const char *default_port = "80";
    const char *p = raw;
    if (strncmp(p, "https://", 8) == 0) { p += 8; default_port = "443"; }
    else if (strncmp(p, "http://", 7) == 0) { p += 7; default_port = "80"; }

    /* isolate host[:port] up to the first '/' (path is ignored - bounce
     * only tests reachability of the host, not a specific page) */
    char hostport[256];
    size_t i = 0;
    while (p[i] && p[i] != '/' && i < sizeof(hostport) - 1) { hostport[i] = p[i]; i++; }
    hostport[i] = '\0';

    char host[256];
    char port[16];
    strncpy(port, default_port, sizeof(port) - 1);
    port[sizeof(port) - 1] = '\0';

    char *colon = strchr(hostport, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - hostport);
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        memcpy(host, hostport, hlen);
        host[hlen] = '\0';
        strncpy(port, colon + 1, sizeof(port) - 1);
        port[sizeof(port) - 1] = '\0';
    } else {
        strncpy(host, hostport, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }

    int last_ok = 0;
    for (int a = 1; a <= attempts; a++) {
        double elapsed = 0.0;
        last_ok = (net_bounce(host, port, &elapsed) == 0);
        if (attempts == 1) {
            printf("bounce back took %.2f seconds reach %s\n", elapsed, last_ok ? "success" : "failure");
        } else {
            printf("bounce back took %.2f seconds reach %s (attempt %d/%d)\n",
                   elapsed, last_ok ? "success" : "failure", a, attempts);
        }
    }

    free(raw);
    r.value.type = VAL_STRING;
    r.value.sval = strdup(last_ok ? "success" : "failure");
    r.has_value = 1;
    return r;
}

static int has_unsupported_extension(const char *path) {
    static const char *blocked[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".exe", ".dll",
        ".zip", ".tar", ".gz", ".pdf", ".mp3", ".mp4", ".ttf", ".woff", NULL
    };
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    for (int i = 0; blocked[i]; i++) {
        size_t bl = strlen(blocked[i]);
        size_t dl = strlen(dot);
        if (dl == bl) {
            int match = 1;
            for (size_t k = 0; k < bl; k++) {
                if (tolower((unsigned char)dot[k]) != blocked[i][k]) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

/* ---------- open: rush's own simple interactive line editor ---------- */
#define MAX_EDIT_LINES 4096

ExecResult cmd_open(char **args, int argc, TokenList *tl) {
    (void)tl;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("open expects a file"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);

    if (has_unsupported_extension(path)) {
        rush_error("unsupported file type");
        printf("try: open %s -default\n", path);
        free(path);
        r.ok = 1;
        return r;
    }

    char *lines[MAX_EDIT_LINES];
    int n = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char buf[RUSH_MAX_LINE];
        while (n < MAX_EDIT_LINES && fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\n")] = '\0';
            lines[n++] = strdup(buf);
        }
        fclose(f);
        printf("opened %s (%d lines). commands: list, a <text>, d <n>, r <n> <text>, save, quit\n", path, n);
    } else {
        printf("%s does not exist yet - starting empty. commands: list, a <text>, d <n>, r <n> <text>, save, quit\n", path);
    }

    int dirty = 0;
    char line[RUSH_MAX_LINE];
    for (;;) {
        printf("edit> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "quit") == 0) {
            if (dirty) printf("(discarded unsaved changes)\n");
            break;
        } else if (strcmp(line, "save") == 0) {
            FILE *out = fopen(path, "w");
            if (!out) { printf("error could not write to %s\n", path); continue; }
            for (int i = 0; i < n; i++) fprintf(out, "%s\n", lines[i]);
            fclose(out);
            dirty = 0;
            printf("saved %s (%d lines)\n", path, n);
        } else if (strcmp(line, "list") == 0) {
            for (int i = 0; i < n; i++) printf("%3d: %s\n", i + 1, lines[i]);
        } else if (strncmp(line, "a ", 2) == 0) {
            if (n < MAX_EDIT_LINES) { lines[n++] = strdup(line + 2); dirty = 1; }
            else printf("error file has too many lines\n");
        } else if (strncmp(line, "d ", 2) == 0) {
            int idx = atoi(line + 2) - 1;
            if (idx < 0 || idx >= n) { printf("error line %d out of range\n", idx + 1); }
            else {
                free(lines[idx]);
                for (int i = idx; i < n - 1; i++) lines[i] = lines[i + 1];
                n--;
                dirty = 1;
            }
        } else if (strncmp(line, "r ", 2) == 0) {
            char *rest = line + 2;
            char *sp = strchr(rest, ' ');
            if (!sp) { printf("error: r <n> <text>\n"); continue; }
            *sp = '\0';
            int idx = atoi(rest) - 1;
            char *newtext = sp + 1;
            if (idx < 0 || idx >= n) { printf("error line %d out of range\n", idx + 1); }
            else { free(lines[idx]); lines[idx] = strdup(newtext); dirty = 1; }
        } else if (line[0] == '\0') {
            /* blank line, ignore */
        } else {
            printf("unknown editor command (try: list, a <text>, d <n>, r <n> <text>, save, quit)\n");
        }
    }

    for (int i = 0; i < n; i++) free(lines[i]);
    free(path);
    return r;
}

/* ---------- edit: single-shot line replace, no interactive mode ---------- */
ExecResult cmd_edit(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 4) { rush_error("edit expects: edit <file> <line> <text>"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    int target = atoi(args[2]);
    int err;
    Value v = resolve_operand(args[3], &err);
    if (err) { free(path); r.ok = 1; return r; }
    char *newtext = value_to_display(&v);
    value_free(&v);

    char *lines[MAX_EDIT_LINES];
    int n = 0;
    FILE *f = fopen(path, "r");
    if (!f) { rush_error("file %s not found", path); free(path); free(newtext); r.ok = 1; return r; }
    char buf[RUSH_MAX_LINE];
    while (n < MAX_EDIT_LINES && fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = '\0';
        lines[n++] = strdup(buf);
    }
    fclose(f);

    if (target < 1 || target > n) {
        rush_error("line %d out of range (file has %d lines)", target, n);
        r.ok = 1;
    } else {
        free(lines[target - 1]);
        lines[target - 1] = strdup(newtext);
        FILE *out = fopen(path, "w");
        if (!out) { rush_error("could not write to %s", path); r.ok = 1; }
        else {
            for (int i = 0; i < n; i++) fprintf(out, "%s\n", lines[i]);
            fclose(out);
        }
    }
    for (int i = 0; i < n; i++) free(lines[i]);
    free(path);
    free(newtext);
    return r;
}

/* ---------- dload / extr: delegate to curl/tar rather than
 * reimplementing HTTP+TLS or archive formats from scratch ---------- */
ExecResult cmd_dload(char **args, int argc, TokenList *tl) {
    (void)tl;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("dload expects a url"); r.ok = 1; return r; }
    char *url = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);

    char output[512];
    if (argc >= 3) {
        char *o = is_quoted(args[2]) ? strip_quotes(args[2]) : strdup(args[2]);
        strncpy(output, o, sizeof(output) - 1);
        output[sizeof(output) - 1] = '\0';
        free(o);
    } else {
        const char *slash = strrchr(url, '/');
        strncpy(output, slash && slash[1] ? slash + 1 : "downloaded_file", sizeof(output) - 1);
        output[sizeof(output) - 1] = '\0';
    }

    char cmdline[RUSH_MAX_LINE];
    snprintf(cmdline, sizeof(cmdline), "curl -L -o \"%s\" \"%s\"", output, url);
    printf("running: %s\n", cmdline);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) { rush_error("download failed"); r.ok = 1; }
    else printf("saved to %s\n", output);
    free(url);
    return r;
}

ExecResult cmd_extr(char **args, int argc, TokenList *tl) {
    (void)tl;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("extr expects an archive"); r.ok = 1; return r; }
    char *archive = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    char dest[512] = ".";
    if (argc >= 3) {
        char *d = is_quoted(args[2]) ? strip_quotes(args[2]) : strdup(args[2]);
        strncpy(dest, d, sizeof(dest) - 1);
        dest[sizeof(dest) - 1] = '\0';
        free(d);
    }

    char cmdline[RUSH_MAX_LINE];
    snprintf(cmdline, sizeof(cmdline), "tar -xf \"%s\" -C \"%s\"", archive, dest);
    printf("running: %s\n", cmdline);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) { rush_error("extraction failed"); r.ok = 1; }
    else printf("extracted %s to %s\n", archive, dest);
    free(archive);
    return r;
}

/* ---------- pack: create a real .zip archive ---------- */
ExecResult cmd_pack(char **args, int argc, TokenList *tl) {
    (void)tl;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 3) { rush_error("pack expects: pack <output.zip> <file/folder...>"); r.ok = 1; return r; }
    char *output = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);

    char cmdline[RUSH_MAX_LINE * 2];
#ifdef _WIN32
    char pathlist[RUSH_MAX_LINE] = "";
    for (int i = 2; i < argc; i++) {
        char *src = is_quoted(args[i]) ? strip_quotes(args[i]) : strdup(args[i]);
        if (i > 2) strncat(pathlist, ",", sizeof(pathlist) - strlen(pathlist) - 1);
        strncat(pathlist, "'", sizeof(pathlist) - strlen(pathlist) - 1);
        strncat(pathlist, src, sizeof(pathlist) - strlen(pathlist) - 1);
        strncat(pathlist, "'", sizeof(pathlist) - strlen(pathlist) - 1);
        free(src);
    }
    snprintf(cmdline, sizeof(cmdline),
             "powershell -Command \"Compress-Archive -Path %s -DestinationPath '%s' -Force\"",
             pathlist, output);
#else
    /* Linux/dev-testing fallback: the 'zip' utility. Not what ships
     * to users - the Windows build always uses Compress-Archive. */
    char pathlist[RUSH_MAX_LINE] = "";
    for (int i = 2; i < argc; i++) {
        char *src = is_quoted(args[i]) ? strip_quotes(args[i]) : strdup(args[i]);
        strncat(pathlist, " \"", sizeof(pathlist) - strlen(pathlist) - 1);
        strncat(pathlist, src, sizeof(pathlist) - strlen(pathlist) - 1);
        strncat(pathlist, "\"", sizeof(pathlist) - strlen(pathlist) - 1);
        free(src);
    }
    snprintf(cmdline, sizeof(cmdline), "zip -r \"%s\"%s", output, pathlist);
#endif

    printf("running: %s\n", cmdline);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) { rush_error("pack failed"); r.ok = 1; }
    else printf("packed into %s\n", output);
    free(output);
    return r;
}

/* ---------- help ---------- */
ExecResult cmd_help(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    printf(
        "rush 0.2 commands:\n"
        "  show <value>              print a value\n"
        "  calc <a> <op> <b>         + - * / arithmetic\n"
        "  where                     print current directory\n"
        "  goin <path>               change directory\n"
        "  list [path]               list directory contents\n"
        "  read <file>               print a file's contents\n"
        "  about <target>            show details about a file/folder\n"
        "  del <target>              delete a file or folder\n"
        "  mkf <name>                make a folder\n"
        "  mkfl <name>               make a file\n"
        "  write <file> <text>       append text to a file\n"
        "  owrite <file> <text>      overwrite a file's contents\n"
        "  time                      show the current time\n"
        "  find <term>               search for a file\n"
        "  rname <old> <new>         rename a file or folder\n"
        "  wait <seconds>            pause\n"
        "  bounce <url> [count]      test network reachability\n"
        "  pack <out.zip> <files..>  create a zip archive\n"
        "  ali <name> = <command>    define a session alias (persists)\n"
        "  dump <variable>           delete a variable\n"
        "  me                        show who is logged in\n"
        "  saves <name>              save current variables as a session\n"
        "  loads <name>              load a saved session\n"
        "  list sess / del sess <n>  list or delete saved sessions\n"
        "  task <script.rsh>         run a script file\n"
        "  run <program> [args]      launch an external program\n"
        "  regi <user> <role>        register an account\n"
        "  login <user>              log in\n"
        "  logout                    log out\n"
        "  promo <user> <role>       change a user's role (admin only)\n"
        "  demo <user>               step a user down one role (admin only)\n"
        "  del user <user>           delete an account (admin only)\n"
        "  list user                 list all registered accounts\n"
        "  auth [tier] <command>     run a command at a privilege tier\n"
        "  package <verb> <args>     run a package command via the\n"
        "                            configured backend (see package config)\n"
        "  loop <n> ... end          repeat a block\n"
        "  if <a> <op> <b> ... end   conditional block\n"
        "  skipto <label> / label <name>   forward-only jump\n"
        "  cmd1 ; cmd2               run multiple commands on one line\n"
        "  exit / quit               leave rush / stop a script\n"
        "  flags: -test -force -every -silent -info -default\n"
    );
    return r;
}

/* ---------- me (whoami) ---------- */
ExecResult cmd_me(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    const char *who = g_current_user[0] ? g_current_user : "anonymous";
    if (!g_suppress_stage_output) {
        if (g_current_user[0]) printf("%s (%s)\n", g_current_user, g_current_role);
        else printf("not logged in\n");
    }
    r.value.type = VAL_STRING;
    r.value.sval = strdup(who);
    r.has_value = 1;
    return r;
}

/* ---------- dump (delete a variable) ---------- */
ExecResult cmd_dump(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("dump expects a variable name"); r.ok = 1; return r; }
    if (!var_delete(args[1])) {
        rush_error("var %s not found", args[1]);
        r.ok = 1;
    }
    return r;
}

/* ---------- package (delegates to a configured OS backend) ---------- */
ExecResult cmd_package(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count >= 4 && strcmp(tl->tokens[1], "config") == 0 && strcmp(tl->tokens[2], "=") == 0) {
        strncpy(g_package_backend, tl->tokens[3], sizeof(g_package_backend) - 1);
        g_package_backend[sizeof(g_package_backend) - 1] = '\0';
        printf("package backend set to %s\n", g_package_backend);
        return r;
    }
    if (!g_package_backend[0]) {
        rush_error("package backend not configured, use: package config = <name>");
        r.ok = 1;
        return r;
    }
    if (tl->count < 2) {
        rush_error("package expects a verb, e.g. package install git");
        r.ok = 1;
        return r;
    }
    char cmdline[RUSH_MAX_LINE];
    strncpy(cmdline, g_package_backend, sizeof(cmdline) - 1);
    cmdline[sizeof(cmdline) - 1] = '\0';
    for (int i = 1; i < tl->count; i++) {
        strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1);
        strncat(cmdline, tl->tokens[i], sizeof(cmdline) - strlen(cmdline) - 1);
    }
    printf("running: %s\n", cmdline);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) { rush_error("package command exited with a nonzero status"); r.ok = 1; }
    return r;
}

/* ---------- accounts ---------- */
ExecResult cmd_regi(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count < 3) { rush_error("regi expects: regi <username> <role>"); r.ok = 1; return r; }
    r.ok = do_regi(tl->tokens[1], tl->tokens[2]);
    return r;
}

ExecResult cmd_login(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count < 2) { rush_error("login expects: login <username>"); r.ok = 1; return r; }
    r.ok = do_login(tl->tokens[1]);
    return r;
}

ExecResult cmd_logout(TokenList *tl) {
    (void)tl;
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    r.ok = do_logout();
    return r;
}

ExecResult cmd_promo(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count < 3) { rush_error("promo expects: promo <user> <role>"); r.ok = 1; return r; }
    r.ok = do_promo(g_current_user[0] ? g_current_role : NULL, tl->tokens[1], tl->tokens[2]);
    return r;
}

/* ---------- del user / list user / demo ---------- */
ExecResult cmd_del_user(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("del user expects a username"); r.ok = 1; return r; }
    r.ok = do_del_user(g_current_user[0] ? g_current_role : NULL, args[1]);
    return r;
}

ExecResult cmd_list_user(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    char usernames[RUSH_MAX_VARS][RUSH_MAX_USERNAME];
    char roles[RUSH_MAX_VARS][RUSH_MAX_ROLE];
    int n = list_all_users(usernames, roles, RUSH_MAX_VARS);
    if (n == 0) { printf("no registered users\n"); return r; }
    for (int i = 0; i < n; i++) printf("%s (%s)\n", usernames[i], roles[i]);
    return r;
}

ExecResult cmd_demo(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("demo expects a username"); r.ok = 1; return r; }
    r.ok = do_demo(g_current_user[0] ? g_current_role : NULL, args[1]);
    return r;
}

/* ---------- sessions (saves/loads/list sess/del sess) ---------- */
ExecResult cmd_saves(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("saves expects a name"); r.ok = 1; return r; }
    ensure_user_dirs();
    char path[1100];
    build_session_path(path, sizeof(path), args[1]);
    FILE *f = fopen(path, "w");
    if (!f) { rush_error("could not write session %s", args[1]); r.ok = 1; return r; }
    int count = 0;
    for (int i = 0; i < RUSH_MAX_VARS; i++) {
        if (g_vars[i].in_use) {
            if (g_vars[i].value.type == VAL_INT) {
                fprintf(f, "%s\tI\t%ld\n", g_vars[i].name, g_vars[i].value.ival);
            } else {
                fprintf(f, "%s\tS\t%s\n", g_vars[i].name, g_vars[i].value.sval ? g_vars[i].value.sval : "");
            }
            count++;
        }
    }
    fclose(f);
    printf("saved session %s (%d variables)\n", args[1], count);
    return r;
}

ExecResult cmd_loads(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("loads expects a name"); r.ok = 1; return r; }
    char path[1100];
    build_session_path(path, sizeof(path), args[1]);
    FILE *f = fopen(path, "r");
    if (!f) { rush_error("session %s not found", args[1]); r.ok = 1; return r; }

    for (int i = 0; i < RUSH_MAX_VARS; i++) {
        if (g_vars[i].in_use) { value_free(&g_vars[i].value); g_vars[i].in_use = 0; }
    }

    char line[RUSH_MAX_LINE];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *name = strtok(line, "\t");
        char *type = strtok(NULL, "\t");
        char *value = strtok(NULL, "\t");
        if (!name || !type || !value) continue;
        if (type[0] == 'I') var_set_int(name, atol(value));
        else var_set_string(name, value);
        count++;
    }
    fclose(f);
    printf("loaded session %s (%d variables)\n", args[1], count);
    return r;
}

ExecResult cmd_list_sess(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    char dir[1100];
    build_session_dir(dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (!d) { printf("no sessions saved\n"); return r; }
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 4 && strcmp(ent->d_name + len - 4, ".txt") == 0) {
            char name[256];
            strncpy(name, ent->d_name, len - 4);
            name[len - 4] = '\0';
            printf("%s\n", name);
            count++;
        }
    }
    closedir(d);
    if (count == 0) printf("no sessions saved\n");
    return r;
}

ExecResult cmd_del_sess(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("del sess expects a name"); r.ok = 1; return r; }
    char path[1100];
    build_session_path(path, sizeof(path), args[1]);
    if (remove(path) != 0) { rush_error("session %s not found", args[1]); r.ok = 1; }
    else printf("deleted session %s\n", args[1]);
    return r;
}

/* ---------- task: run an .rsh script file from within the REPL ---------- */
#define MAX_TASK_LINES 4096
ExecResult cmd_task(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("task expects a script file"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    FILE *f = fopen(path, "r");
    if (!f) { rush_error("script %s not found", path); free(path); r.ok = 1; return r; }

    static char *lines[MAX_TASK_LINES];
    static char buf[MAX_TASK_LINES][RUSH_MAX_LINE];
    int n = 0;
    while (n < MAX_TASK_LINES && fgets(buf[n], RUSH_MAX_LINE, f)) {
        buf[n][strcspn(buf[n], "\n")] = '\0';
        lines[n] = buf[n];
        n++;
    }
    fclose(f);
    free(path);
    run_program(lines, n);
    return r;
}

/* ---------- run: launch an external program, no rush-native alternative ---------- */
ExecResult cmd_run(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count < 2) { rush_error("run expects a program to launch"); r.ok = 1; return r; }
    char cmdline[RUSH_MAX_LINE] = "";
    for (int i = 1; i < tl->count; i++) {
        if (i > 1) strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1);
        strncat(cmdline, tl->tokens[i], sizeof(cmdline) - strlen(cmdline) - 1);
    }
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) { rush_error("program exited with a nonzero status"); r.ok = 1; }
    return r;
}
