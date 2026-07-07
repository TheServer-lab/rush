#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "rush.h"
#include "net.h"
#include "users.h"

#define MAX_SCRIPT_LINES 4096

static char *trim_ws(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static const char *first_word_view(const char *s, char *buf, size_t bufsz) {
    size_t i = 0;
    while (s[i] && !isspace((unsigned char)s[i]) && i < bufsz - 1) { buf[i] = s[i]; i++; }
    buf[i] = '\0';
    return buf;
}

static int is_block_opener(const char *word) {
    return strcmp(word, "loop") == 0 || strcmp(word, "if") == 0;
}

/* Run an .rsh file: read every line, then hand the whole thing to
 * run_program, which understands start/quit/loop/if/skipto/label. */
static void run_script_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "rush: could not open script %s\n", path);
        return;
    }
    static char *lines[MAX_SCRIPT_LINES];
    static char buf[MAX_SCRIPT_LINES][RUSH_MAX_LINE];
    int n = 0;
    while (n < MAX_SCRIPT_LINES && fgets(buf[n], RUSH_MAX_LINE, f)) {
        buf[n][strcspn(buf[n], "\n")] = '\0';
        lines[n] = buf[n];
        n++;
    }
    fclose(f);
    run_program(lines, n);
}

static void repl(void) {
    char raw[RUSH_MAX_LINE];
    static char block_lines[MAX_SCRIPT_LINES][RUSH_MAX_LINE];
    static char *block_ptrs[MAX_SCRIPT_LINES];

    printf("rush 0.2\n");
    while (g_running) {
        printf("rush> ");
        fflush(stdout);
        if (!fgets(raw, sizeof(raw), stdin)) break;
        raw[strcspn(raw, "\n")] = '\0';
        char *line = trim_ws(raw);
        if (line[0] == '\0') continue;

        char word[32];
        first_word_view(line, word, sizeof(word));

        if (is_block_opener(word)) {
            int depth = 1;
            int n = 0;
            strncpy(block_lines[n], line, RUSH_MAX_LINE - 1);
            block_ptrs[n] = block_lines[n];
            n++;
            while (depth > 0) {
                printf("...> ");
                fflush(stdout);
                if (!fgets(raw, sizeof(raw), stdin)) break;
                raw[strcspn(raw, "\n")] = '\0';
                char *l2 = trim_ws(raw);
                char w2[32];
                first_word_view(l2, w2, sizeof(w2));
                if (is_block_opener(w2)) depth++;
                else if (strcmp(w2, "end") == 0) depth--;
                strncpy(block_lines[n], l2, RUSH_MAX_LINE - 1);
                block_ptrs[n] = block_lines[n];
                n++;
                if (n >= MAX_SCRIPT_LINES) break;
            }
            run_program(block_ptrs, n);
        } else if (strcmp(word, "exit") == 0) {
            g_running = 0;
        } else if (strcmp(word, "quit") == 0) {
            /* quit at the top-level REPL just ends this "script" (the
             * whole interactive session counts as one), same as exit */
            g_running = 0;
        } else {
            ExecResult er = exec_line(line);
            if (er.has_value) value_free(&er.value);
        }
    }
}

int main(int argc, char **argv) {
    net_init();
    char start_dir[1024];
    if (getcwd(start_dir, sizeof(start_dir))) {
        users_set_start_dir(start_dir);
    }
    alias_load_current();
    if (argc >= 2) {
        run_script_file(argv[1]);
        net_cleanup();
        return 0;
    }
    repl();
    net_cleanup();
    return 0;
}
