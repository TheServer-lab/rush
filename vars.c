#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "rush.h"
#include "users.h"

Variable g_vars[RUSH_MAX_VARS];
Alias g_aliases[RUSH_MAX_ALIASES];
int g_running = 1;
int g_suppress_stage_output = 0;
char g_default_tier[16] = "member";
char g_package_backend[64] = "";
char g_cwd_label[RUSH_MAX_LINE] = ".";

void rush_error(const char *fmt, ...) {
    va_list ap;
    fprintf(stdout, "error ");
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

/* ---------- tokenizer ---------- */
/* Quoted tokens keep their surrounding double quotes in the stored
 * string so downstream code (assignment, calc, show) can tell a
 * literal string apart from a bare word. */
void tokenize(const char *line, TokenList *out) {
    out->count = 0;
    size_t len = strlen(line);
    size_t i = 0;
    while (i < len && out->count < RUSH_MAX_TOKENS) {
        while (i < len && isspace((unsigned char)line[i])) i++;
        if (i >= len) break;

        if (line[i] == '"') {
            size_t start = i;
            i++;
            while (i < len && line[i] != '"') i++;
            if (i < len) i++; /* consume closing quote */
            size_t tlen = i - start;
            char *tok = malloc(tlen + 1);
            memcpy(tok, line + start, tlen);
            tok[tlen] = '\0';
            out->tokens[out->count++] = tok;
        } else {
            size_t start = i;
            while (i < len && !isspace((unsigned char)line[i])) i++;
            size_t tlen = i - start;
            char *tok = malloc(tlen + 1);
            memcpy(tok, line + start, tlen);
            tok[tlen] = '\0';
            out->tokens[out->count++] = tok;
        }
    }
}

void free_tokens(TokenList *tl) {
    for (int i = 0; i < tl->count; i++) free(tl->tokens[i]);
    tl->count = 0;
}

/* ---------- variables ---------- */
Variable *var_find(const char *name) {
    for (int i = 0; i < RUSH_MAX_VARS; i++) {
        if (g_vars[i].in_use && strcmp(g_vars[i].name, name) == 0) return &g_vars[i];
    }
    return NULL;
}

static Variable *var_alloc(const char *name) {
    Variable *v = var_find(name);
    if (v) {
        value_free(&v->value);
        return v;
    }
    for (int i = 0; i < RUSH_MAX_VARS; i++) {
        if (!g_vars[i].in_use) {
            g_vars[i].in_use = 1;
            strncpy(g_vars[i].name, name, sizeof(g_vars[i].name) - 1);
            return &g_vars[i];
        }
    }
    return NULL; /* table full */
}

Variable *var_set_int(const char *name, long v) {
    Variable *var = var_alloc(name);
    if (!var) return NULL;
    var->value.type = VAL_INT;
    var->value.ival = v;
    var->value.sval = NULL;
    return var;
}

Variable *var_set_string(const char *name, const char *v) {
    Variable *var = var_alloc(name);
    if (!var) return NULL;
    var->value.type = VAL_STRING;
    var->value.sval = strdup(v);
    return var;
}

void value_free(Value *v) {
    if (v->type == VAL_STRING && v->sval) {
        free(v->sval);
        v->sval = NULL;
    }
}

int var_delete(const char *name) {
    Variable *v = var_find(name);
    if (!v) return 0;
    value_free(&v->value);
    v->in_use = 0;
    v->name[0] = '\0';
    return 1;
}

char *value_to_display(const Value *v) {
    if (v->type == VAL_INT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", v->ival);
        return strdup(buf);
    }
    return strdup(v->sval ? v->sval : "");
}

/* ---------- interpolation ---------- */
/* Expands {name} inside a literal (already-unquoted) string.
 * Returns a new heap string, or NULL with *ok=0 on undefined variable. */
char *interpolate(const char *literal, int *ok) {
    *ok = 1;
    size_t cap = strlen(literal) + 64;
    char *result = malloc(cap);
    size_t rlen = 0;
    size_t len = strlen(literal);

    for (size_t i = 0; i < len; i++) {
        if (literal[i] == '{') {
            size_t j = i + 1;
            while (j < len && literal[j] != '}') j++;
            if (j >= len) {
                /* unmatched brace: treat literally */
                result[rlen++] = literal[i];
                continue;
            }
            size_t namelen = j - (i + 1);
            char name[64];
            if (namelen >= sizeof(name)) namelen = sizeof(name) - 1;
            memcpy(name, literal + i + 1, namelen);
            name[namelen] = '\0';

            Variable *v = var_find(name);
            if (!v) {
                rush_error("var %s not found", name);
                *ok = 0;
                free(result);
                return NULL;
            }
            char *disp = value_to_display(&v->value);
            size_t dlen = strlen(disp);
            if (rlen + dlen + 1 > cap) {
                cap = (rlen + dlen + 1) * 2;
                result = realloc(result, cap);
            }
            memcpy(result + rlen, disp, dlen);
            rlen += dlen;
            free(disp);
            i = j; /* skip past '}' */
        } else {
            if (rlen + 1 + 1 > cap) {
                cap *= 2;
                result = realloc(result, cap);
            }
            result[rlen++] = literal[i];
        }
    }
    result[rlen] = '\0';
    return result;
}

/* ---------- flags ---------- */
int has_flag(TokenList *tl, const char *flagname) {
    char full[80];
    snprintf(full, sizeof(full), "-%s", flagname);
    for (int i = 0; i < tl->count; i++) {
        if (strcmp(tl->tokens[i], full) == 0) return 1;
    }
    return 0;
}

int strip_flags(TokenList *tl, char **args, int max) {
    int n = 0;
    for (int i = 0; i < tl->count && n < max; i++) {
        if (tl->tokens[i][0] == '-' && strlen(tl->tokens[i]) > 1 &&
            !isdigit((unsigned char)tl->tokens[i][1])) {
            continue; /* it's a flag, skip */
        }
        args[n++] = tl->tokens[i];
    }
    return n;
}

/* ---------- aliases ---------- */
Alias *alias_find(const char *name) {
    for (int i = 0; i < RUSH_MAX_ALIASES; i++) {
        if (g_aliases[i].in_use && strcmp(g_aliases[i].name, name) == 0) return &g_aliases[i];
    }
    return NULL;
}

static void save_all_aliases(void) {
    char path[1100];
    build_alias_path(path, sizeof(path));
    ensure_user_dirs();
    /* ensure the parent dir for the global/user alias file itself exists
     * (ensure_user_dirs covers rush_data and rush_data/user/<u>, but the
     * global alias.txt's parent - rush_data - is already covered too) */
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < RUSH_MAX_ALIASES; i++) {
        if (g_aliases[i].in_use) {
            fprintf(f, "%s\t%s\n", g_aliases[i].name, g_aliases[i].expansion);
        }
    }
    fclose(f);
}

void alias_set(const char *name, const char *expansion) {
    Alias *a = alias_find(name);
    if (!a) {
        for (int i = 0; i < RUSH_MAX_ALIASES; i++) {
            if (!g_aliases[i].in_use) { a = &g_aliases[i]; break; }
        }
    }
    if (!a) return; /* table full */
    a->in_use = 1;
    strncpy(a->name, name, sizeof(a->name) - 1);
    strncpy(a->expansion, expansion, sizeof(a->expansion) - 1);
    save_all_aliases();
}

void alias_clear_all(void) {
    for (int i = 0; i < RUSH_MAX_ALIASES; i++) g_aliases[i].in_use = 0;
}

void alias_load_current(void) {
    alias_clear_all();
    char path[1100];
    build_alias_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[RUSH_MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        alias_set(line, tab + 1); /* note: re-saves the file each line, harmless but slightly wasteful */
    }
    fclose(f);
}
