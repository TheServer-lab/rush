#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "rush.h"
#include "commands.h"
#include "users.h"

int g_script_stop = 0;

static int is_known_command(const char *word) {
    static const char *names[] = {
        "auth","regi","login","logout","promo","demo","help","me","dump",
        "show","calc","where","goin","list","read","about","del","mkf",
        "mkfl","write","owrite","time","find","rname","wait","bounce",
        "pack","ali","saves","loads","task","run","package","open",
        "edit","dload","extr","exit","quit","loop","if","skipto","label",
        "end","start", NULL
    };
    for (int i = 0; names[i]; i++) if (strcmp(word, names[i]) == 0) return 1;
    if (alias_find(word)) return 1;
    return 0;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static char *first_word(const char *line, char *buf, size_t bufsz) {
    size_t i = 0;
    while (line[i] && !isspace((unsigned char)line[i]) && i < bufsz - 1) {
        buf[i] = line[i];
        i++;
    }
    buf[i] = '\0';
    return buf;
}

/* split a line into pipeline stages on top-level '~' (not inside quotes) */
static int split_pipeline(const char *line, char stages[][RUSH_MAX_LINE], int max_stages) {
    int n = 0;
    int in_quotes = 0;
    size_t start = 0;
    size_t len = strlen(line);
    for (size_t i = 0; i <= len; i++) {
        char c = line[i];
        if (c == '"') in_quotes = !in_quotes;
        if ((c == '~' && !in_quotes) || c == '\0') {
            size_t seglen = i - start;
            if (seglen >= RUSH_MAX_LINE) seglen = RUSH_MAX_LINE - 1;
            memcpy(stages[n], line + start, seglen);
            stages[n][seglen] = '\0';
            n++;
            start = i + 1;
            if (n >= max_stages) break;
        }
    }
    return n;
}

/* Dispatch a single already-tokenized stage. Handles built-in commands,
 * alias expansion, and the "auth <tier> ..." elevation prefix. */
static ExecResult dispatch(TokenList *tl, Value *piped) {
    ExecResult r = {0, {VAL_INT, 0, NULL}, 0};
    if (tl->count == 0) return r;
    const char *cmd = tl->tokens[0];
    char *args[RUSH_MAX_TOKENS];
    int argc = strip_flags(tl, args, RUSH_MAX_TOKENS);

    if (strcmp(cmd, "auth") == 0) {
        if (tl->count >= 4 && strcmp(tl->tokens[1], "config") == 0 && strcmp(tl->tokens[2], "=") == 0) {
            if (!g_current_user[0]) { rush_error("not logged in"); r.ok = 1; return r; }
            if (strcmp(g_current_role, "admin") != 0) { rush_error("insufficient permission"); r.ok = 1; return r; }
            if (!role_valid(tl->tokens[3])) { rush_error("unknown role %s", tl->tokens[3]); r.ok = 1; return r; }
            strncpy(g_default_tier, tl->tokens[3], sizeof(g_default_tier) - 1);
            g_default_tier[sizeof(g_default_tier) - 1] = '\0';
            printf("default privilege tier set to %s\n", g_default_tier);
            return r;
        }
        if (tl->count < 2) {
            rush_error("usage: auth [tier] <command>, or auth config = <tier>");
            r.ok = 1;
            return r;
        }
        const char *tier;
        int inner_start;
        if (role_valid(tl->tokens[1])) {
            tier = tl->tokens[1];
            inner_start = 2;
        } else {
            /* no explicit tier given - fall back to the configured default */
            tier = g_default_tier;
            inner_start = 1;
        }
        if (tl->count <= inner_start) {
            rush_error("auth expects a command to run");
            r.ok = 1;
            return r;
        }
        if (!g_current_user[0]) { rush_error("not logged in"); r.ok = 1; return r; }
        if (role_rank(tier) > role_rank(g_current_role)) {
            rush_error("insufficient permission");
            r.ok = 1;
            return r;
        }
        TokenList inner;
        inner.count = tl->count - inner_start;
        for (int i = 0; i < inner.count; i++) inner.tokens[i] = tl->tokens[i + inner_start];
        return dispatch(&inner, piped);
    }
    if (strcmp(cmd, "regi") == 0)   return cmd_regi(tl);
    if (strcmp(cmd, "login") == 0)  return cmd_login(tl);
    if (strcmp(cmd, "logout") == 0) return cmd_logout(tl);
    if (strcmp(cmd, "promo") == 0)  return cmd_promo(tl);
    if (strcmp(cmd, "help") == 0)   return cmd_help();
    if (strcmp(cmd, "me") == 0)     return cmd_me();
    if (strcmp(cmd, "dump") == 0)   return cmd_dump(args, argc);
    if (strcmp(cmd, "package") == 0) return cmd_package(tl);
    if (strcmp(cmd, "show") == 0)  return cmd_show(args, argc, piped);
    if (strcmp(cmd, "calc") == 0)  return cmd_calc(args, argc, piped);
    if (strcmp(cmd, "where") == 0) return cmd_where(args, argc, piped);
    if (strcmp(cmd, "goin") == 0)  return cmd_goin(args, argc, piped);
    if (strcmp(cmd, "list") == 0) {
        if (tl->count >= 2 && strcmp(tl->tokens[1], "user") == 0) return cmd_list_user();
        if (tl->count >= 2 && strcmp(tl->tokens[1], "sess") == 0) return cmd_list_sess();
        return cmd_list(args, argc, tl, piped);
    }
    if (strcmp(cmd, "read") == 0)  return cmd_read(args, argc, piped);
    if (strcmp(cmd, "about") == 0) return cmd_about(args, argc, piped);
    if (strcmp(cmd, "del") == 0) {
        if (tl->count >= 3 && strcmp(tl->tokens[1], "user") == 0) {
            char *shifted[RUSH_MAX_TOKENS];
            shifted[0] = tl->tokens[0];
            shifted[1] = tl->tokens[2];
            return cmd_del_user(shifted, 2);
        }
        if (tl->count >= 3 && strcmp(tl->tokens[1], "sess") == 0) {
            char *shifted[RUSH_MAX_TOKENS];
            shifted[0] = tl->tokens[0];
            shifted[1] = tl->tokens[2];
            return cmd_del_sess(shifted, 2);
        }
        return cmd_del(args, argc, tl, piped);
    }
    if (strcmp(cmd, "mkf") == 0)   return cmd_mkf(args, argc, piped);
    if (strcmp(cmd, "mkfl") == 0)  return cmd_mkfl(args, argc, piped);
    if (strcmp(cmd, "write") == 0) return cmd_write(args, argc, piped);
    if (strcmp(cmd, "owrite") == 0) return cmd_owrite(args, argc, piped);
    if (strcmp(cmd, "time") == 0)  return cmd_time(args, argc, piped);
    if (strcmp(cmd, "find") == 0)  return cmd_find(args, argc, tl, piped);
    if (strcmp(cmd, "rname") == 0) return cmd_rname(args, argc, piped);
    if (strcmp(cmd, "wait") == 0)  return cmd_wait(args, argc, piped);
    if (strcmp(cmd, "bounce") == 0) return cmd_bounce(args, argc, piped);
    if (strcmp(cmd, "pack") == 0)  return cmd_pack(args, argc, tl);
    if (strcmp(cmd, "demo") == 0)  return cmd_demo(args, argc);
    if (strcmp(cmd, "saves") == 0) return cmd_saves(args, argc);
    if (strcmp(cmd, "loads") == 0) return cmd_loads(args, argc);
    if (strcmp(cmd, "task") == 0)  return cmd_task(args, argc);
    if (strcmp(cmd, "run") == 0)   return cmd_run(tl);
    if (strcmp(cmd, "ali") == 0)   return cmd_ali(tl);
    if (strcmp(cmd, "open") == 0) {
        if (has_flag(tl, "default")) {
#ifdef _WIN32
            char linebuf[RUSH_MAX_LINE] = "start \"\" ";
            if (argc >= 2) strncat(linebuf, args[1], sizeof(linebuf) - strlen(linebuf) - 1);
            fflush(stdout);
            system(linebuf);
            return r;
#else
            printf("(-default would hand off to the OS default program here)\n");
            return r;
#endif
        }
        return cmd_open(args, argc, tl);
    }
    if (strcmp(cmd, "edit") == 0)  return cmd_edit(args, argc);
    if (strcmp(cmd, "dload") == 0) return cmd_dload(args, argc, tl);
    if (strcmp(cmd, "extr") == 0)  return cmd_extr(args, argc, tl);

    Alias *a = alias_find(cmd);
    if (a) {
        char rebuilt[RUSH_MAX_LINE];
        strncpy(rebuilt, a->expansion, sizeof(rebuilt) - 1);
        rebuilt[sizeof(rebuilt)-1] = '\0';
        for (int i = 1; i < tl->count; i++) {
            strncat(rebuilt, " ", sizeof(rebuilt) - strlen(rebuilt) - 1);
            strncat(rebuilt, tl->tokens[i], sizeof(rebuilt) - strlen(rebuilt) - 1);
        }
        return exec_line(rebuilt);
    }

    rush_error("command %s not found", cmd);
    r.ok = 1;
    return r;
}

/* Top-level: handles "name = <literal or command>" assignment, then
 * pipelines the remaining stages with '~'. */
/* split a line into statements on top-level ';' (not inside quotes) */
static int split_statements(const char *line, char stmts[][RUSH_MAX_LINE], int max_stmts) {
    int n = 0;
    int in_quotes = 0;
    size_t start = 0;
    size_t len = strlen(line);
    for (size_t i = 0; i <= len; i++) {
        char c = line[i];
        if (c == '"') in_quotes = !in_quotes;
        if ((c == ';' && !in_quotes) || c == '\0') {
            size_t seglen = i - start;
            if (seglen >= RUSH_MAX_LINE) seglen = RUSH_MAX_LINE - 1;
            memcpy(stmts[n], line + start, seglen);
            stmts[n][seglen] = '\0';
            n++;
            start = i + 1;
            if (n >= max_stmts) break;
        }
    }
    return n;
}

static ExecResult exec_single_statement(const char *raw) {
    ExecResult r = {0, {VAL_INT, 0, NULL}, 0};
    char line[RUSH_MAX_LINE];
    strncpy(line, raw, sizeof(line) - 1);
    line[sizeof(line)-1] = '\0';
    char *trimmed = trim(line);
    if (trimmed[0] == '\0' || trimmed[0] == '#') return r;

    log_action(g_current_user, trimmed);

    /* assignment? */
    TokenList probe;
    tokenize(trimmed, &probe);
    if (probe.count >= 3 && strcmp(probe.tokens[1], "=") == 0 &&
        probe.tokens[0][0] != '-' ) {
        char *name = probe.tokens[0];
        if (probe.count == 3 && probe.tokens[2][0] == '"') {
            char *inner = strdup(probe.tokens[2] + 1);
            size_t l = strlen(inner);
            if (l && inner[l-1] == '"') inner[l-1] = '\0';
            int ok;
            char *expanded = interpolate(inner, &ok);
            free(inner);
            if (ok) var_set_string(name, expanded);
            free(expanded);
        } else if (probe.count == 3 && isdigit((unsigned char)probe.tokens[2][0])) {
            var_set_int(name, atol(probe.tokens[2]));
        } else if (probe.count == 3 && !is_known_command(probe.tokens[2])) {
            /* single bare word on the right of '=' that isn't a known
             * command: treat as a plain string literal, e.g. a = hello */
            var_set_string(name, probe.tokens[2]);
        } else {
            /* rest is a sub-command (either multi-word, or a single
             * word that matches a known command/alias, e.g. x = me) */
            const char *p = strstr(trimmed, "= ");
            const char *rest = p ? p + 2 : trimmed;
            ExecResult sub = exec_single_statement(rest);
            if (sub.has_value) {
                if (sub.value.type == VAL_INT) var_set_int(name, sub.value.ival);
                else var_set_string(name, sub.value.sval);
                value_free(&sub.value);
            }
        }
        free_tokens(&probe);
        return r;
    }
    free_tokens(&probe);

    /* pipeline */
    char stages[RUSH_MAX_TOKENS][RUSH_MAX_LINE];
    int nstages = split_pipeline(trimmed, stages, RUSH_MAX_TOKENS);
    Value *piped = NULL;
    Value carried;
    int have_carried = 0;
    ExecResult last = r;

    for (int i = 0; i < nstages; i++) {
        char *stage = trim(stages[i]);
        if (stage[0] == '\0') continue;
        TokenList tl;
        tokenize(stage, &tl);
        g_suppress_stage_output = (i != nstages - 1) ? 1 : 0;
        ExecResult stage_result = dispatch(&tl, piped);
        g_suppress_stage_output = 0;
        free_tokens(&tl);
        /* The old carried value has now been consumed as this stage's
         * piped input (if it was used at all) - safe to free it and
         * replace it. The *final* stage's value is NOT freed here; it
         * is handed back to the caller (assignment code, or discarded
         * by a top-level caller that doesn't need it). */
        if (have_carried) { value_free(&carried); have_carried = 0; }
        last = stage_result;
        if (last.has_value) {
            carried = last.value;
            have_carried = 1;
            piped = &carried;
        } else {
            piped = NULL;
        }
    }
    return last;
}

/* Top-level entry point: splits on ';' for command chaining, then runs
 * each statement in order. Returns the last statement's result. */
ExecResult exec_line(const char *raw) {
    ExecResult r = {0, {VAL_INT, 0, NULL}, 0};
    char stmts[RUSH_MAX_TOKENS][RUSH_MAX_LINE];
    int n = split_statements(raw, stmts, RUSH_MAX_TOKENS);
    ExecResult last = r;
    for (int i = 0; i < n; i++) {
        char *stmt = trim(stmts[i]);
        if (stmt[0] == '\0') continue;
        if (last.has_value) value_free(&last.value);
        last = exec_single_statement(stmt);
    }
    return last;
}

/* ---------- block interpreter: loop / if / skipto / label / end ---------- */

static int find_matching_end(char **lines, int n, int start_idx) {
    int depth = 1;
    char word[32];
    for (int i = start_idx + 1; i < n; i++) {
        first_word(trim(lines[i]), word, sizeof(word));
        if (strcmp(word, "loop") == 0 || strcmp(word, "if") == 0) depth++;
        else if (strcmp(word, "end") == 0) { depth--; if (depth == 0) return i; }
    }
    return -1;
}

static int eval_condition(const char *cond_str) {
    TokenList tl;
    tokenize(cond_str, &tl);
    if (tl.count != 3) {
        rush_error("if expects: if <value> <op> <value>");
        free_tokens(&tl);
        return 0;
    }
    int err1, err2;
    Value a = resolve_operand(tl.tokens[0], &err1);
    const char *op = tl.tokens[1];
    Value b = resolve_operand(tl.tokens[2], &err2);
    int result = 0;
    if (!err1 && !err2 && a.type == VAL_INT && b.type == VAL_INT) {
        if (strcmp(op, "==") == 0) result = a.ival == b.ival;
        else if (strcmp(op, "!=") == 0) result = a.ival != b.ival;
        else if (strcmp(op, "<") == 0) result = a.ival < b.ival;
        else if (strcmp(op, ">") == 0) result = a.ival > b.ival;
        else if (strcmp(op, "<=") == 0) result = a.ival <= b.ival;
        else if (strcmp(op, ">=") == 0) result = a.ival >= b.ival;
        else rush_error("unknown operator %s", op);
    } else if (!err1 && !err2 && a.type == VAL_STRING && b.type == VAL_STRING) {
        if (strcmp(op, "==") == 0) result = strcmp(a.sval, b.sval) == 0;
        else if (strcmp(op, "!=") == 0) result = strcmp(a.sval, b.sval) != 0;
        else rush_error("unsupported string operator %s", op);
    } else if (!err1 && !err2) {
        rush_error("string and int collision");
    }
    value_free(&a); value_free(&b);
    free_tokens(&tl);
    return result;
}

void run_program(char **lines, int n) {
    int pc = 0;
    char word[32];
    while (pc < n && g_running && !g_script_stop) {
        char *line = trim(lines[pc]);
        first_word(line, word, sizeof(word));
        if (line[0] == '\0' || line[0] == '#') { pc++; continue; }

        if (strcmp(word, "start") == 0) { pc++; continue; }

        if (strcmp(word, "loop") == 0) {
            int count = atoi(line + 5);
            int end_idx = find_matching_end(lines, n, pc);
            if (end_idx < 0) { rush_error("missing end for loop"); return; }
            int body_n = end_idx - pc - 1;
            char **body = lines + pc + 1;
            for (int c = 0; c < count && g_running && !g_script_stop; c++) {
                run_program(body, body_n);
            }
            pc = end_idx + 1;
            continue;
        }
        if (strcmp(word, "if") == 0) {
            int cond = eval_condition(line + 3);
            int end_idx = find_matching_end(lines, n, pc);
            if (end_idx < 0) { rush_error("missing end for if"); return; }
            if (cond) run_program(lines + pc + 1, end_idx - pc - 1);
            pc = end_idx + 1;
            continue;
        }
        if (strcmp(word, "label") == 0) { pc++; continue; }
        if (strcmp(word, "skipto") == 0) {
            char target[64];
            first_word(line + 7, target, sizeof(target));
            int found = -1;
            for (int i = pc + 1; i < n; i++) {
                char w2[32], lbl[64];
                char *l2 = trim(lines[i]);
                first_word(l2, w2, sizeof(w2));
                if (strcmp(w2, "label") == 0) {
                    first_word(l2 + 6, lbl, sizeof(lbl));
                    if (strcmp(lbl, target) == 0) { found = i; break; }
                }
            }
            if (found < 0) { rush_error("no label found to skip to"); return; }
            pc = found + 1;
            continue;
        }
        if (strcmp(word, "quit") == 0) { g_script_stop = 1; return; }
        if (strcmp(word, "exit") == 0) { g_running = 0; return; }

        ExecResult er = exec_line(line);
        if (er.has_value) value_free(&er.value);
        pc++;
    }
}
