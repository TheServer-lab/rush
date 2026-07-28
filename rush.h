#ifndef RUSH_H
#define RUSH_H

#include <stddef.h>
#include <signal.h>

#define RUSH_MAX_LINE   1024
#define RUSH_MAX_TOKENS 64
#define RUSH_MAX_VARS   256
#define RUSH_MAX_ALIASES 64

typedef enum {
    VAL_INT,
    VAL_STRING
} ValueType;

typedef struct {
    ValueType type;
    long ival;
    char *sval; /* heap-allocated, owned */
} Value;

typedef struct {
    char name[64];
    Value value;
    int in_use;
} Variable;

typedef struct {
    char name[64];
    char expansion[RUSH_MAX_LINE];
    int in_use;
} Alias;

typedef struct {
    char name[64];
    char path[RUSH_MAX_LINE];
    int in_use;
} LaunchTarget;

#define RUSH_MAX_LAUNCH_TARGETS 64

typedef struct {
    char *tokens[RUSH_MAX_TOKENS];
    int count;
} TokenList;

/* Result of running a single command/stage. Used so pipes (~) and
 * assignment (a = <command>) can capture whatever a command "returns"
 * as its printed output. */
typedef struct {
    int ok;             /* 0 = success, nonzero = error already reported */
    Value value;        /* captured output value, if any */
    int has_value;
} ExecResult;

/* --- global state --- */
extern Variable g_vars[RUSH_MAX_VARS];
extern Alias g_aliases[RUSH_MAX_ALIASES];
extern LaunchTarget g_launch_targets[RUSH_MAX_LAUNCH_TARGETS];
extern int g_running;      /* 0 once exit/quit requested */
extern char g_cwd_label[RUSH_MAX_LINE]; /* not the real OS cwd necessarily */

/* --- error reporting: format is always "error <category> <description>" */
void rush_error(const char *fmt, ...);

/* --- tokenizing --- */
void tokenize(const char *line, TokenList *out);
void free_tokens(TokenList *tl);

/* --- variables --- */
Variable *var_find(const char *name);
Variable *var_set_int(const char *name, long v);
Variable *var_set_string(const char *name, const char *v);
int var_delete(const char *name); /* returns 1 if it existed and was removed */
void value_free(Value *v);
char *value_to_display(const Value *v); /* caller frees */

/* --- global configuration, settable via <domain> config = <value> --- */
extern char g_default_tier[16];   /* used when 'auth' is given no explicit tier */
extern char g_package_backend[64]; /* e.g. "winget", set via package config = <name> */
extern char g_lib_backend[64];     /* "pip" or "npm", set via lib config = <name> */

/* Set by the Ctrl+C handler (installed in main.c). Long-running loops
 * (infinite "loop" blocks, "monitor") check this each iteration/tick
 * so Ctrl+C stops just that command gracefully instead of killing the
 * whole process. The REPL resets it to 0 after each top-level command
 * so the session itself keeps going. */
extern volatile sig_atomic_t g_interrupt;

/* Set to 1 by dispatch() only for the exact command running directly
 * under an "auth ..." prefix (see interp.c). Admin-only commands that
 * must be explicitly elevated (cali, promo, demo, del user) check this
 * and refuse to run at all if it's not set, even for an admin who is
 * already logged in - "auth" isn't optional for these. */
extern int g_via_auth;

/* --- string interpolation: expands {name} inside a literal string --- */
char *interpolate(const char *literal, int *ok);

/* --- flags --- */
int has_flag(TokenList *tl, const char *flagname);
/* returns count of tokens that are argument tokens (flags stripped),
 * fills args[] with pointers (borrowed) up to max */
int strip_flags(TokenList *tl, char **args, int max);

/* --- aliases --- */
Alias *alias_find(const char *name);
void alias_set(const char *name, const char *expansion);
void alias_clear_all(void);
void alias_save_current(void);
void alias_load_current(void); /* loads from disk based on login state, replacing memory */

LaunchTarget *launch_target_find(const char *name);
void launch_target_set(const char *name, const char *path);
void launch_target_clear_all(void);
void launch_target_save_current(void);
void launch_target_load_current(void); /* loads from disk based on login state, replacing memory */

/* --- command execution --- */
ExecResult exec_line(const char *line);
void run_program(char **lines, int n);

/* When nonzero, commands that produce a value (calc, show, etc.)
 * should suppress their own printed output because they are feeding
 * a later pipe stage rather than being the final, visible result. */
extern int g_suppress_stage_output;

#endif
