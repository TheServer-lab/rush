# rush — 0.2 reference build

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

## Icon (rush.ico)
No icon file was actually uploaded in our conversation, so I can't
embed one yet - but the scaffolding is ready. Once you have
`rush.ico`, drop it in this folder and build with:
```
windres rush.rc -O coff -o rush_res.o
gcc -Wall -Wextra -o rush.exe main.c interp.c commands.c vars.c net.c users.c sha256.c rush_res.o -lws2_32
```
`windres` ships with the same MinGW toolchain you already have `gcc`
from, so no new install is needed.

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
  owrite, time, find, rname, wait, bounce, pack, ali, dump, me, help,
  saves, loads, task, run, auth, regi, login, logout, promo, demo,
  package, exit, quit` — plus `del user`, `list user`, `list sess`,
  `del sess` as domain-prefixed forms of `del`/`list`.
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
- `-test` and `-silent` flags are recognized (won't error) but no
  command changes behavior for them yet
- `open`, `edit`, `dload`, `extr` are now real:
  - `open <file>` enters a small interactive line editor (`list`,
    `a <text>`, `d <n>`, `r <n> <text>`, `save`, `quit`);
    `open <file> -default` still hands off to the OS's own program.
  - `edit <file> <line> <text>` replaces one line, no interactive mode.
  - `dload <url> [output]` and `extr <archive> [dest]` delegate to
    `curl`/`tar` (both ship on modern Windows and Linux) rather than
    reimplementing HTTP/TLS or archive formats from scratch.
- Command chaining with `;`: `x = 1 ; show x ; calc 2 * 2` runs each
  statement in sequence on one line.
- `pack <output.zip> <files/folders...>` creates a real zip archive.
  On Windows it uses PowerShell's `Compress-Archive` (built into every
  modern Windows) rather than relying on `tar.exe`'s zip support,
  which varies by build. The Linux build here uses `zip` instead,
  purely for local testing.
- `open` now refuses known binary/media extensions (`.png`, `.exe`,
  `.zip`, etc.) with `error unsupported file type` and suggests
  `open <file> -default` instead of trying to load them as text.
- Aliases now actually persist (previously session-only): `ali` saves
  immediately to `rush_data/user/<user>/alias.txt` if logged in, or
  `rush_data/alias.txt` if not. Logging in/out swaps which alias set
  is active - they're two separate namespaces, not merged.
- Sessions: `saves <name>` / `loads <name>` save and restore the
  current variable table (loading replaces whatever variables are
  currently set). Stored in `rush_data/user/<user>/sessions/<name>.txt`
  if logged in, or `rush_data/sessions/<name>.txt` if not. `list sess`
  and `del sess <name>` manage them.
- `task <script.rsh>` runs a script file from inside a running REPL
  session (previously scripts could only be run by passing the file
  as rush.exe's command-line argument).
- `run <program> [args]` launches an external program directly and
  hands it to the OS - a dedicated, explicit escape hatch (distinct
  from `-default`, which only applies to specific commands).
- User management QoL: `list user` (list all accounts), `demo <user>`
  (step a user down one role; admin-only), `del user <user>`
  (delete an account; admin-only, and you can't delete the account
  you're currently logged in as).
  - Note: `del <username>` was requested as a bare form, but that
    collides with `del <file>` if a file happens to share a
    username - implemented as `del user <username>` instead, matching
    the existing `del sess <name>` pattern. Same reasoning applied to
    `list user` / `list sess` vs. plain `list <path>`.
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
