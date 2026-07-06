#ifndef RUSH_COMMANDS_H
#define RUSH_COMMANDS_H

#include "rush.h"

Value resolve_operand(const char *tok, int *err);

ExecResult cmd_show(char **args, int argc, Value *piped);
ExecResult cmd_calc(char **args, int argc, Value *piped);
ExecResult cmd_where(char **args, int argc, Value *piped);
ExecResult cmd_goin(char **args, int argc, Value *piped);
ExecResult cmd_list(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_read(char **args, int argc, Value *piped);
ExecResult cmd_about(char **args, int argc, Value *piped);
ExecResult cmd_del(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_mkf(char **args, int argc, Value *piped);
ExecResult cmd_mkfl(char **args, int argc, Value *piped);
ExecResult cmd_write(char **args, int argc, Value *piped);
ExecResult cmd_owrite(char **args, int argc, Value *piped);
ExecResult cmd_time(char **args, int argc, Value *piped);
ExecResult cmd_find(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_rname(char **args, int argc, Value *piped);
ExecResult cmd_wait(char **args, int argc, Value *piped);
ExecResult cmd_bounce(char **args, int argc, Value *piped);
ExecResult cmd_help(void);
ExecResult cmd_me(void);
ExecResult cmd_dump(char **args, int argc);
ExecResult cmd_package(TokenList *tl);
ExecResult cmd_regi(TokenList *tl);
ExecResult cmd_login(TokenList *tl);
ExecResult cmd_logout(TokenList *tl);
ExecResult cmd_promo(TokenList *tl);
ExecResult cmd_ali(TokenList *tl);
ExecResult cmd_stub(const char *name);

#endif
