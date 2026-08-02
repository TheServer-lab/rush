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
ExecResult cmd_mkf(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_mkfl(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_write(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_owrite(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_time(char **args, int argc, Value *piped);
ExecResult cmd_find(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_lookfor(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_rname(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_cpy(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_mov(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_appn(char **args, int argc, TokenList *tl, Value *piped);
ExecResult cmd_wait(char **args, int argc, Value *piped);
ExecResult cmd_bounce(char **args, int argc, Value *piped);
ExecResult cmd_help(char **args, int argc);
ExecResult cmd_me(void);
ExecResult cmd_dump(char **args, int argc);
ExecResult cmd_package(TokenList *tl);
ExecResult cmd_lib(TokenList *tl);
ExecResult cmd_launch(TokenList *tl);
ExecResult cmd_config(TokenList *tl);
ExecResult cmd_list_pros(void);
ExecResult cmd_kill_pros(const char *numstr);
ExecResult cmd_pause(void);
ExecResult cmd_sdown(TokenList *tl);
ExecResult cmd_list_dsk(void);
ExecResult cmd_list_lab(void);
ExecResult cmd_list_part(const char *disknum_str);
ExecResult cmd_frmt(TokenList *tl);
ExecResult cmd_partcre(TokenList *tl);
ExecResult cmd_partdel(TokenList *tl);
ExecResult cmd_partres(TokenList *tl);
ExecResult cmd_netch(char **args, int argc);
ExecResult cmd_monitor(char **args, int argc, Value *piped);
ExecResult cmd_open(char **args, int argc, TokenList *tl);
ExecResult cmd_view(char **args, int argc);
ExecResult cmd_edit(char **args, int argc);
ExecResult cmd_dload(char **args, int argc, TokenList *tl);
ExecResult cmd_extr(char **args, int argc, TokenList *tl);
ExecResult cmd_pack(char **args, int argc, TokenList *tl);
ExecResult cmd_regi(TokenList *tl);
ExecResult cmd_login(TokenList *tl);
ExecResult cmd_logout(TokenList *tl);
ExecResult cmd_promo(TokenList *tl);
ExecResult cmd_del_user(char **args, int argc);
ExecResult cmd_list_user(void);
ExecResult cmd_demo(char **args, int argc);
ExecResult cmd_saves(char **args, int argc);
ExecResult cmd_loads(char **args, int argc);
ExecResult cmd_list_sess(void);
ExecResult cmd_del_sess(char **args, int argc);
ExecResult cmd_rr(char **args, int argc);
ExecResult cmd_run(TokenList *tl);
ExecResult cmd_cali(char **args, int argc);
ExecResult cmd_wipe(void);
ExecResult cmd_ali(TokenList *tl);

#endif
