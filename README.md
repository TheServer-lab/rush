# rush — 0.1 reference build

This is a working prototype of the rush interpreter described in
`rush-spec-v0.1.md`. It was developed and tested on Linux, but is
written in portable C (no POSIX-only calls outside of directory
listing / process control, which are isolated in `commands.c` and
`interp.c`) so it should build under MinGW or MSVC with only small
changes (`dirent.h` has drop-in ports for Windows via MinGW; MSVC
needs `<direct.h>`/`FindFirstFile` instead of `opendir`/`readdir`).

## Build (Linux, for testing)
```
gcc -Wall -Wextra -o rush main.c interp.c commands.c vars.c net.c users.c sha256.c
```

## Build (Windows, MinGW)
```
gcc -Wall -Wextra -o rush.exe main.c interp.c commands.c vars.c net.c users.c sha256.c -lws2_32
```
(dirent.h ships with MinGW, so `list`/`del -every`/`find` should work
unmodified. `-lws2_32` links Windows' socket library, needed for the
`bounce` command.)

## Run
```
./rush            # interactive REPL
./rush myscript.rsh   # run a script file
```

## What's implemented
- REPL with `rush>` / `...>` (block) prompts
- Variables (`int`/`string`), bare-word lookup, `{name}` interpolation
- `calc` with `+ - * /`, loud type-mismatch errors
- Pipes via `~` (only the final stage in a pipeline prints)
- `show, calc, where, goin, list, read, about, del, mkf, mkfl, write,
  owrite, time, find, rname, wait, bounce, ali, dump, me, help, auth,
  regi, login, logout, promo, package, exit, quit`
- `help` lists every command; `me` shows who's logged in (or
  `not logged in`); `dump <var>` deletes a variable.
- `bounce <url> [count]` — an optional count after the URL repeats the
  reachability test that many times, printing one numbered result
  line per attempt.
- `auth` now falls back to a configured default tier when no tier
  word is given (`auth show "..."`), settable via
  `auth config = <tier>` (admin-only). Typing an explicit tier still
  works and is checked against your logged-in role as a hard ceiling.
- `package config = <backend>` (e.g. `winget`) configures a backend;
  `package install git` then runs `<backend> install git` for real,
  the same way `-default` hands off to the OS.
- Accounts: `regi <user> <role>`, `login <user>`, `logout`,
  `promo <user> <role>` (admin-only). Passwords are salted SHA-256,
  stored in `rush_data/users.txt` (created next to wherever rush was
  launched from — captured once at startup, so `goin` won't move it).
  Every command is logged to `rush_data/log.txt` as
  `<user or "anonymous"> <command> <timestamp>`, including failed
  login attempts.
- `auth <tier> <command>` now enforces a real ceiling: it requires
  being logged in, and the typed tier can't exceed your account's
  role (typing *below* your own role is allowed as a deliberate,
  self-imposed restriction).
- Flags: `-test` (not yet wired to any command's actual behavior),
  `-force`, `-every`, `-silent` (not yet wired), `-info`, `-default`
- Loops (`loop N` / `end`), conditionals (`if a == 1` / `end`),
  forward-only `skipto <label>` / `label <name>`
- `.rsh` script files (`start` is a no-op marker, `quit` stops the
  script, `exit` stops the whole program)

## Known gaps (documented, not hidden)
- `open`, `edit`, `dload`, `extr` are stubs; `-default` on `open`/`edit`
  will shell out via `system()` as a placeholder, but there is no
  built-in text editor or downloader/extractor yet
- `-test` and `-silent` flags are recognized (won't error) but no
  command changes behavior for them yet
- Aliases are session-only, as specified
- No password recovery: a lost password means registering a new
  account or manually clearing `rush_data/users.txt` — accepted
  trade-off, not an oversight
- `regi`/`login` only prompt for a password once (no "confirm
  password" re-entry on registration)
- Random salt generation uses `rand()`, seeded once from the system
  clock — fine for its purpose (defeating identical-password
  hash collisions in a public file), not cryptographically strong
  randomness
- `skipto` is scoped to the current block level (a script-wide jump
  across sibling blocks is not yet supported) — this was left open in
  the spec and this build takes the more conservative interpretation

Tested clean under AddressSanitizer + UBSan across variables, calc,
pipes, file operations, aliases, loops, if/skipto/label, and auth —
no memory errors or crashes on the tested paths.
