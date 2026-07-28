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
#include "image.h"
#include "launch.h"
#include "disk.h"

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

/* Parses a raw url (with optional http(s):// scheme, optional :port,
 * optional /path which is ignored) into host and port buffers. Shared
 * by bounce, netch, and monitor so all three treat urls identically. */
static void parse_url_hostport(const char *raw, char *host, size_t hostsz, char *port, size_t portsz) {
    const char *default_port = "80";
    const char *p = raw;
    if (strncmp(p, "https://", 8) == 0) { p += 8; default_port = "443"; }
    else if (strncmp(p, "http://", 7) == 0) { p += 7; default_port = "80"; }

    char hostport[256];
    size_t i = 0;
    while (p[i] && p[i] != '/' && i < sizeof(hostport) - 1) { hostport[i] = p[i]; i++; }
    hostport[i] = '\0';

    strncpy(port, default_port, portsz - 1);
    port[portsz - 1] = '\0';

    char *colon = strchr(hostport, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - hostport);
        if (hlen >= hostsz) hlen = hostsz - 1;
        memcpy(host, hostport, hlen);
        host[hlen] = '\0';
        strncpy(port, colon + 1, portsz - 1);
        port[portsz - 1] = '\0';
    } else {
        strncpy(host, hostport, hostsz - 1);
        host[hostsz - 1] = '\0';
    }
}

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

    char host[256];
    char port[16];
    parse_url_hostport(raw, host, sizeof(host), port, sizeof(port));

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

/* ---------- netch ("net check": one-shot, richer than bounce - also
 * resolves and shows the IP actually being reached) ---------- */
ExecResult cmd_netch(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("netch expects a url"); r.ok = 1; return r; }
    char *raw = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);

    char host[256];
    char port[16];
    parse_url_hostport(raw, host, sizeof(host), port, sizeof(port));

    printf("netch: resolving %s...\n", host);
    fflush(stdout);
    char ip[64];
    int resolved = (net_resolve_ip(host, ip, sizeof(ip)) == 0);
    if (resolved) printf("netch: resolved to %s\n", ip);
    else printf("netch: could not resolve host\n");

    printf("netch: connecting on port %s...\n", port);
    fflush(stdout);
    double elapsed = 0.0;
    int ok = (net_bounce(host, port, &elapsed) == 0);
    printf("netch: %s in %.2f seconds\n", ok ? "reached" : "unreachable", elapsed);
    printf("netch: result %s\n", ok ? "reachable" : "unreachable");

    free(raw);
    r.value.type = VAL_STRING;
    r.value.sval = strdup(ok ? "success" : "failure");
    r.has_value = 1;
    return r;
}

/* ---------- monitor (continuous, ping-style repeated reachability
 * checks). With no count given, runs forever - stop with Ctrl+C, or
 * with 'quit' inside a loop/script. ---------- */
ExecResult cmd_monitor(char **args, int argc, Value *piped) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("monitor expects a url"); r.ok = 1; return r; }
    char *raw = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);

    int interval = 1;
    if (argc >= 3) {
        interval = atoi(args[2]);
        if (interval <= 0) interval = 1;
    }

    int count = 0; /* 0 = infinite */
    if (argc >= 4) {
        count = atoi(args[3]);
        if (count < 0) count = 0;
    } else if (piped && piped->type == VAL_INT && piped->ival > 0) {
        count = (int)piped->ival;
    }

    char host[256];
    char port[16];
    parse_url_hostport(raw, host, sizeof(host), port, sizeof(port));

    printf("monitor: watching %s (%s), every %ds%s - press Ctrl+C to stop\n",
           host, port, interval, count > 0 ? "" : ", no limit");
    fflush(stdout);

    int sent = 0, received = 0;
    int a = 0;
    while ((count == 0 || a < count) && g_running && !g_script_stop && !g_interrupt) {
        a++;
        sent++;
        double elapsed = 0.0;
        int ok = (net_bounce(host, port, &elapsed) == 0);
        if (ok) received++;
        printf("monitor: attempt %d took %.2f seconds reach %s\n", a, elapsed, ok ? "success" : "failure");
        fflush(stdout);
        if ((count == 0 || a < count) && !g_interrupt) {
            sleep((unsigned int)interval);
        }
    }

    int loss_pct = sent > 0 ? (int)(100.0 * (sent - received) / sent) : 0;
    printf("monitor: stopped - %d sent, %d received, %d%% loss\n", sent, received, loss_pct);

    free(raw);
    r.value.type = VAL_STRING;
    r.value.sval = strdup(received > 0 ? "success" : "failure");
    r.has_value = 1;
    return r;
}

static int has_unsupported_extension(const char *path) {
    static const char *blocked[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".exe", ".dll",
        ".zip", ".tar", ".gz", ".pdf", ".mp3", ".mp4", ".ttf", ".woff",
        ".ppm", ".pnm", NULL
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

/* ---------- view: render a .bmp/.ppm image in the terminal ---------- */
ExecResult cmd_view(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) { rush_error("view expects an image file"); r.ok = 1; return r; }
    char *path = is_quoted(args[1]) ? strip_quotes(args[1]) : strdup(args[1]);
    Image img;
    if (image_load(path, &img) != 0) {
        free(path);
        r.ok = 1;
        return r;
    }
    image_render_terminal(&img);
    image_free(&img);
    free(path);
    return r;
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
        "  open <file> [-default]    interactive line editor (or OS default)\n"
        "  edit <file> <line> <text> replace one line, non-interactive\n"
        "  view <image.bmp/.ppm> [-default]  display in terminal (or OS default)\n"
        "  dload <url> [output]      download a file via curl\n"
        "  extr <archive> [dest]     extract an archive via tar\n"
        "  wait <seconds>            pause\n"
        "  bounce <url> [count]      test network reachability\n"
        "  pack <out.zip> <files..>  create a zip archive\n"
        "  ali <name> = <command>    define a session alias (persists)\n"
        "  cali [user]               clear aliases: yours (no arg), or\n"
        "                            a named user's (admin only)\n"
        "  wipe                      clear the terminal screen\n"
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
        "  lib <verb> <args>         run a language package command via\n"
        "                            the configured backend (see lib config)\n"
        "  lib install req py/node  install from requirements.txt/package.json\n"
        "  netch <url>               one-shot network check (resolve + connect)\n"
        "  monitor <url> [s] [n]     repeated reachability checks, like ping;\n"
        "                            omit n to run until Ctrl+C\n"
        "  launch config <n> = <p>   register a launch target (or: config launch)\n"
        "  launch <name>             run a registered launch target\n"
        "  list pros                 list processes started by launch\n"
        "  kill pros <n>             kill a process started by launch\n"
        "  pause                     wait for Enter - useful in scripts\n"
        "  sdown [-force]            shut down the computer (confirms first,\n"
        "                            unless -force is given)\n"
        "  list dsk                  list physical disks (Windows only)\n"
        "  list lab                  list volumes: drive, label, fs, size, free\n"
        "  list part <disk#>         list partitions on a disk\n"
        "  frmt <drive> [fs] -force  format a drive (default fs: ntfs) - needs\n"
        "                            auth admin, -force, and retyping the drive\n"
        "  partcre <disk#> <size> -force   create a partition (size e.g. 50GB, or max)\n"
        "  partdel <disk#> <part#> -force  delete a partition\n"
        "  partres <disk#> <part#> <size> -force   resize a partition\n"
        "  loop [n] ... end          repeat a block; omit n to loop forever\n"
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
        config_save_current();
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

/* ---------- lib (delegates to a configured language package manager:
 * pip or npm) ---------- */
ExecResult cmd_lib(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count >= 4 && strcmp(tl->tokens[1], "config") == 0 && strcmp(tl->tokens[2], "=") == 0) {
        const char *backend = tl->tokens[3];
        if (strcmp(backend, "pip") != 0 && strcmp(backend, "npm") != 0) {
            rush_error("unknown lib backend %s, expected pip or npm", backend);
            r.ok = 1;
            return r;
        }
        strncpy(g_lib_backend, backend, sizeof(g_lib_backend) - 1);
        g_lib_backend[sizeof(g_lib_backend) - 1] = '\0';
        printf("lib backend set to %s\n", g_lib_backend);
        config_save_current();
        return r;
    }

    if (tl->count < 2) {
        rush_error("lib expects a verb, e.g. lib install flask");
        r.ok = 1;
        return r;
    }

    /* "lib install req py" / "lib install req node" installs from a
     * requirements file (requirements.txt / package.json). This always
     * uses the language's own tool regardless of the configured lib
     * backend - lib install req py runs pip even if lib is set to npm,
     * since a requirements.txt is inherently a Python-ecosystem file. */
    if (tl->count >= 4 && strcmp(tl->tokens[1], "install") == 0 && strcmp(tl->tokens[2], "req") == 0) {
        const char *target = tl->tokens[3];
        const char *cmdline;
        if (strcmp(target, "py") == 0) {
            cmdline = "pip install -r requirements.txt";
        } else if (strcmp(target, "node") == 0) {
            cmdline = "npm install";
        } else {
            rush_error("lib install req expects py or node, got %s", target);
            r.ok = 1;
            return r;
        }
        printf("running: %s\n", cmdline);
        fflush(stdout);
        int rc = system(cmdline);
        if (rc != 0) { rush_error("lib install req command exited with a nonzero status"); r.ok = 1; }
        return r;
    }

    if (!g_lib_backend[0]) {
        rush_error("lib backend not configured, use: lib config = <pip|npm>");
        r.ok = 1;
        return r;
    }
    char cmdline[RUSH_MAX_LINE];
    strncpy(cmdline, g_lib_backend, sizeof(cmdline) - 1);
    cmdline[sizeof(cmdline) - 1] = '\0';
    for (int i = 1; i < tl->count; i++) {
        strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1);
        strncat(cmdline, tl->tokens[i], sizeof(cmdline) - strlen(cmdline) - 1);
    }
    printf("running: %s\n", cmdline);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) { rush_error("lib command exited with a nonzero status"); r.ok = 1; }
    return r;
}

/* Joins tl->tokens[start..count) with single spaces into out - used to
 * reconstruct a path/value that may have been typed unquoted with
 * multiple tokens (e.g. a launch target path). */
static void join_tokens_from(TokenList *tl, int start, char *out, size_t outsz) {
    out[0] = '\0';
    for (int i = start; i < tl->count; i++) {
        if (i > start) strncat(out, " ", outsz - strlen(out) - 1);
        strncat(out, tl->tokens[i], outsz - strlen(out) - 1);
    }
}

/* ---------- launch (register/run launch targets) ---------- */
ExecResult cmd_launch(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    /* launch config <name> = <path>  (also accepted as: config launch <name> = <path>, see cmd_config) */
    if (tl->count >= 5 && strcmp(tl->tokens[1], "config") == 0 && strcmp(tl->tokens[3], "=") == 0) {
        char path[RUSH_MAX_LINE];
        join_tokens_from(tl, 4, path, sizeof(path));
        launch_target_set(tl->tokens[2], path);
        printf("launch target %s set to %s\n", tl->tokens[2], path);
        return r;
    }
    if (tl->count < 2) {
        rush_error("launch expects: launch <name>, or launch config <name> = <path>");
        r.ok = 1;
        return r;
    }
    LaunchTarget *t = launch_target_find(tl->tokens[1]);
    if (!t) {
        rush_error("no launch target named %s, use: launch config %s = <path>", tl->tokens[1], tl->tokens[1]);
        r.ok = 1;
        return r;
    }
    r.ok = (launch_spawn(t->name, t->path) == 0) ? 0 : 1;
    return r;
}

/* ---------- config (currently just: config launch <name> = <path>,
 * the mirror-order form of "launch config ..." above) ---------- */
ExecResult cmd_config(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (tl->count >= 5 && strcmp(tl->tokens[1], "launch") == 0 && strcmp(tl->tokens[3], "=") == 0) {
        char path[RUSH_MAX_LINE];
        join_tokens_from(tl, 4, path, sizeof(path));
        launch_target_set(tl->tokens[2], path);
        printf("launch target %s set to %s\n", tl->tokens[2], path);
        return r;
    }
    rush_error("usage: config launch <name> = <path>");
    r.ok = 1;
    return r;
}

/* ---------- list pros / kill pros (processes started by launch) ---------- */
ExecResult cmd_list_pros(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    launch_list();
    return r;
}

ExecResult cmd_kill_pros(const char *numstr) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    char *end;
    long slot = strtol(numstr, &end, 10);
    if (end == numstr || *end != '\0') {
        rush_error("kill pros expects a process number, e.g. kill pros 1");
        r.ok = 1;
        return r;
    }
    r.ok = (launch_kill((int)slot) == 0) ? 0 : 1;
    return r;
}

/* ---------- pause (waits for Enter - useful in scripts) ---------- */
ExecResult cmd_pause(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    printf("Press Enter to continue . . . ");
    fflush(stdout);
    char buf[256];
    if (!fgets(buf, sizeof(buf), stdin)) { /* EOF - nothing more to wait for */ }
    return r;
}

/* ---------- sdown (shut down the computer) ---------- */
ExecResult cmd_sdown(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (!has_flag(tl, "force")) {
        printf("This will shut down your computer now.\n");
        printf("Continue? (y/n): ");
        fflush(stdout);
        char buf[16];
        if (!fgets(buf, sizeof(buf), stdin)) {
            printf("shutdown cancelled\n");
            return r;
        }
        buf[strcspn(buf, "\n")] = '\0';
        if (buf[0] != 'y' && buf[0] != 'Y') {
            printf("shutdown cancelled\n");
            return r;
        }
    }
    printf("shutting down...\n");
    fflush(stdout);
#ifdef _WIN32
    int rc = system("shutdown /s /t 0");
#else
    int rc = system("shutdown -h now");
#endif
    if (rc != 0) {
        rush_error("shutdown command failed - you may need administrator/root privileges");
        r.ok = 1;
    }
    return r;
}

/* ---------- disk / volume / partition management (Windows-only) ----------
 * All destructive operations here require THREE separate confirmations:
 *   1. auth admin - proves who you are
 *   2. -force flag - a deliberate, typed-in-advance "I mean this"
 *   3. retyping the exact target (drive letter, or disk#[:part#]) - proves
 *      you're about to touch what you think you're touching, not a typo
 * All three are required together; none of them substitutes for another. */

static int ci_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static int confirm_retype(const char *expect) {
    printf("Type %s to confirm: ", expect);
    fflush(stdout);
    char buf[128];
    if (!fgets(buf, sizeof(buf), stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = '\0';
    return ci_equal(buf, expect);
}

/* Parses sizes like "50GB", "500MB", "1TB", or "max" (out_is_max=1,
 * out_bytes=0). Requires an explicit unit - "500" alone is rejected,
 * since guessing the unit on a destructive resize is exactly the kind
 * of ambiguity this whole command is trying to avoid. */
static int parse_size_bytes(const char *s, long long *out_bytes, int *out_is_max) {
    *out_is_max = 0;
    *out_bytes = 0;
    if (ci_equal(s, "max")) { *out_is_max = 1; return 0; }

    char *end;
    double num = strtod(s, &end);
    if (end == s || num < 0) return -1;

    char unit[8];
    size_t i = 0;
    while (*end && i < sizeof(unit) - 1) unit[i++] = (char)tolower((unsigned char)*end++);
    unit[i] = '\0';
    if (*end) return -1; /* trailing garbage after the unit */

    long long mult;
    if (strcmp(unit, "mb") == 0) mult = 1024LL * 1024;
    else if (strcmp(unit, "gb") == 0) mult = 1024LL * 1024 * 1024;
    else if (strcmp(unit, "tb") == 0) mult = 1024LL * 1024 * 1024 * 1024;
    else return -1;

    *out_bytes = (long long)(num * (double)mult);
    return 0;
}

/* Collects tl's tokens that aren't flags (skipping tokens[0], the
 * command word itself) into out[], returning how many were found. */
static int collect_nonflag_args(TokenList *tl, char *out[], int max_out) {
    int n = 0;
    for (int i = 1; i < tl->count && n < max_out; i++) {
        if (tl->tokens[i][0] != '-') out[n++] = tl->tokens[i];
    }
    return n;
}

ExecResult cmd_list_dsk(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    r.ok = (disk_list() == 0) ? 0 : 1;
    return r;
}

ExecResult cmd_list_lab(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    r.ok = (disk_list_volumes() == 0) ? 0 : 1;
    return r;
}

ExecResult cmd_list_part(const char *disknum_str) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    char *end;
    long disknum = strtol(disknum_str, &end, 10);
    if (end == disknum_str || *end != '\0') {
        rush_error("list part expects a disk number, e.g. list part 1");
        r.ok = 1;
        return r;
    }
    r.ok = (disk_list_partitions((int)disknum) == 0) ? 0 : 1;
    return r;
}

ExecResult cmd_frmt(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (!g_via_auth) {
        rush_error("frmt requires auth, e.g. auth admin frmt <drive> [fs] -force");
        r.ok = 1;
        return r;
    }
    if (!has_flag(tl, "force")) {
        rush_error("frmt requires -force to confirm you understand this erases the drive");
        r.ok = 1;
        return r;
    }

    char *args_nf[4];
    int n = collect_nonflag_args(tl, args_nf, 4);
    if (n < 1) {
        rush_error("frmt expects: frmt <drive> [fs] -force");
        r.ok = 1;
        return r;
    }
    const char *drive = args_nf[0];
    const char *fs = (n >= 2) ? args_nf[1] : "ntfs";

    printf("This will PERMANENTLY ERASE all data on drive %s (filesystem: %s).\n", drive, fs);
    if (!confirm_retype(drive)) { printf("format cancelled\n"); return r; }

    r.ok = (disk_format(drive, fs) == 0) ? 0 : 1;
    return r;
}

ExecResult cmd_partcre(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (!g_via_auth) {
        rush_error("partcre requires auth, e.g. auth admin partcre <disk#> <size> -force");
        r.ok = 1;
        return r;
    }
    if (!has_flag(tl, "force")) {
        rush_error("partcre requires -force to confirm this modifies disk partitions");
        r.ok = 1;
        return r;
    }

    char *args_nf[4];
    int n = collect_nonflag_args(tl, args_nf, 4);
    if (n < 2) {
        rush_error("partcre expects: partcre <disk#> <size> -force (size e.g. 50GB, or max)");
        r.ok = 1;
        return r;
    }
    char *end;
    long disknum = strtol(args_nf[0], &end, 10);
    if (end == args_nf[0] || *end != '\0') {
        rush_error("partcre expects a numeric disk#, got %s", args_nf[0]);
        r.ok = 1;
        return r;
    }
    long long bytes = 0;
    int is_max = 0;
    if (parse_size_bytes(args_nf[1], &bytes, &is_max) != 0) {
        rush_error("could not parse size %s - use e.g. 50GB, 500MB, or max", args_nf[1]);
        r.ok = 1;
        return r;
    }

    printf("This will create a new partition on disk %ld (%s).\n",
           disknum, is_max ? "using all remaining free space" : args_nf[1]);
    char expect[32];
    snprintf(expect, sizeof(expect), "%ld", disknum);
    if (!confirm_retype(expect)) { printf("partition create cancelled\n"); return r; }

    long size_mb = is_max ? 0 : (long)(bytes / (1024 * 1024));
    r.ok = (disk_partition_create((int)disknum, size_mb) == 0) ? 0 : 1;
    return r;
}

ExecResult cmd_partdel(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (!g_via_auth) {
        rush_error("partdel requires auth, e.g. auth admin partdel <disk#> <part#> -force");
        r.ok = 1;
        return r;
    }
    if (!has_flag(tl, "force")) {
        rush_error("partdel requires -force to confirm this deletes a partition");
        r.ok = 1;
        return r;
    }

    char *args_nf[4];
    int n = collect_nonflag_args(tl, args_nf, 4);
    if (n < 2) {
        rush_error("partdel expects: partdel <disk#> <part#> -force");
        r.ok = 1;
        return r;
    }
    char *end;
    long disknum = strtol(args_nf[0], &end, 10);
    if (end == args_nf[0] || *end != '\0') {
        rush_error("partdel expects a numeric disk#, got %s", args_nf[0]);
        r.ok = 1;
        return r;
    }
    long partnum = strtol(args_nf[1], &end, 10);
    if (end == args_nf[1] || *end != '\0') {
        rush_error("partdel expects a numeric part#, got %s", args_nf[1]);
        r.ok = 1;
        return r;
    }

    printf("This will PERMANENTLY DELETE partition %ld on disk %ld and all data on it.\n", partnum, disknum);
    char expect[32];
    snprintf(expect, sizeof(expect), "%ld:%ld", disknum, partnum);
    if (!confirm_retype(expect)) { printf("partition delete cancelled\n"); return r; }

    r.ok = (disk_partition_delete((int)disknum, (int)partnum) == 0) ? 0 : 1;
    return r;
}

ExecResult cmd_partres(TokenList *tl) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (!g_via_auth) {
        rush_error("partres requires auth, e.g. auth admin partres <disk#> <part#> <size> -force");
        r.ok = 1;
        return r;
    }
    if (!has_flag(tl, "force")) {
        rush_error("partres requires -force to confirm this resizes a partition");
        r.ok = 1;
        return r;
    }

    char *args_nf[4];
    int n = collect_nonflag_args(tl, args_nf, 4);
    if (n < 3) {
        rush_error("partres expects: partres <disk#> <part#> <size> -force (size e.g. 100GB)");
        r.ok = 1;
        return r;
    }
    char *end;
    long disknum = strtol(args_nf[0], &end, 10);
    if (end == args_nf[0] || *end != '\0') {
        rush_error("partres expects a numeric disk#, got %s", args_nf[0]);
        r.ok = 1;
        return r;
    }
    long partnum = strtol(args_nf[1], &end, 10);
    if (end == args_nf[1] || *end != '\0') {
        rush_error("partres expects a numeric part#, got %s", args_nf[1]);
        r.ok = 1;
        return r;
    }
    long long bytes = 0;
    int is_max = 0;
    if (parse_size_bytes(args_nf[2], &bytes, &is_max) != 0 || is_max) {
        rush_error("could not parse size %s - use e.g. 100GB, 500MB (max isn't valid for resize)", args_nf[2]);
        r.ok = 1;
        return r;
    }

    printf("This will resize partition %ld on disk %ld to %s.\n", partnum, disknum, args_nf[2]);
    printf("Shrinking will destroy any data beyond the new size.\n");
    char expect[32];
    snprintf(expect, sizeof(expect), "%ld:%ld", disknum, partnum);
    if (!confirm_retype(expect)) { printf("partition resize cancelled\n"); return r; }

    r.ok = (disk_partition_resize((int)disknum, (int)partnum, bytes) == 0) ? 0 : 1;
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
    if (!g_via_auth) { rush_error("promo requires auth, e.g. auth admin promo <user> <role>"); r.ok = 1; return r; }
    if (tl->count < 3) { rush_error("promo expects: promo <user> <role>"); r.ok = 1; return r; }
    r.ok = do_promo(g_current_user[0] ? g_current_role : NULL, tl->tokens[1], tl->tokens[2]);
    return r;
}

/* ---------- del user / list user / demo ---------- */
ExecResult cmd_del_user(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (!g_via_auth) { rush_error("del user requires auth, e.g. auth admin del user <name>"); r.ok = 1; return r; }
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
    if (!g_via_auth) { rush_error("demo requires auth, e.g. auth admin demo <user>"); r.ok = 1; return r; }
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

/* ---------- cali: clear aliases - your own (no arg), or a named
 * user's (admin only) ---------- */
ExecResult cmd_cali(char **args, int argc) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
    if (argc < 2) {
        alias_clear_all();
        alias_save_current();
        printf("cleared %s aliases\n", g_current_user[0] ? "your" : "global");
        return r;
    }
    if (!g_via_auth) { rush_error("cali <user> requires auth, e.g. auth admin cali <user>"); r.ok = 1; return r; }
    r.ok = do_cali(g_current_user[0] ? g_current_role : NULL, args[1]);
    return r;
}

/* ---------- wipe: clear the terminal screen ---------- */
ExecResult cmd_wipe(void) {
    ExecResult r = {0, {VAL_INT,0,NULL}, 0};
#ifdef _WIN32
    fflush(stdout);
    system("cls");
#else
    printf("\033[2J\033[H");
    fflush(stdout);
#endif
    return r;
}
