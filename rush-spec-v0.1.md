# rush — Language Specification

**Version:** 0.2
**Status:** Current
**Platform:** Windows (console application)

## 1. Mission

rush is a REPL-first command shell designed to replace inherited Unix
terminology (`sudo`, `apt`, `ls`, `grep`, `|`, `&&`, cryptic single-letter
flags) with plain, guessable, consistent English words and a single
grammar rule applied everywhere.

rush is **not** a general-purpose programming language. It is a scripting
and interactive-command tool. It deliberately does not aim to support
large programs, complex data structures, or modular application
development. Features are scoped to "a few dozen lines of interactive
or scripted work," not software engineering at scale.

rush commands are self-contained input/output operations by default.
rush can reach outside itself to run external Windows programs, but
only when explicitly asked to via the `-default` flag. Nothing leaves
rush's own execution model silently.

## 2. Core Grammar

Every rush statement follows the same shape:

```
<command> <object> [flags...]
```

Where relevant, commands that manage a sub-system follow:

```
<domain> <verb> <object> [flags...]
```

Examples: `auth package install git`, `auth config auth member`.

This is the one grammar rule in rush. There are no per-command dialects.

## 3. Naming Rule

- Commands are spelled out in full by default.
- A command whose full name is **8 or more letters** is shortened to a
  short, recognizable form (e.g. `download` → `dload`, `extract` → `extr`).
- Abbreviations must not share a prefix with another command's
  abbreviation (avoid ambiguous near-collisions).

## 4. Flags

Flags are global — one meaning, valid across every command that
supports them. Flags are written after the object, and may be stacked.

| Flag       | Meaning                                          |
|------------|---------------------------------------------------|
| `-test`    | Preview only; do not perform the action            |
| `-force`   | Ignore warnings / overwrite existing target        |
| `-every`   | Include everything, recursively                   |
| `-silent`  | Suppress output                                    |
| `-info`    | Verbose / detailed output                          |
| `-default` | Hand off to the external OS default program/process|

Example: `del myfolder -force -every`

## 5. Privilege Model (`auth`)

rush supports three trust tiers:

- `guest` — least privileged
- `member` — middle ground (default assumed tier before any config)
- `admin` — full privilege

`auth` may prefix any command to run it at a given tier for that call
only:

```
auth admin package install git
auth guest install suspicious_software
auth member <command>
```

If no tier word is given, `auth` falls back to the configured default
tier instead of requiring one every time:

```
auth show "runs at the default tier"
```

Changing the default tier requires being logged in as `admin`:

```
auth config = member
```

**Real enforcement (as of 0.2):** `auth` is backed by the account
system (§20). Running any `auth`-prefixed command requires being
logged in; the typed (or default) tier is checked against the
logged-in user's assigned role as a hard ceiling — you cannot run
above your own role, but running *at or below* it is always allowed
(useful for deliberately running something cautiously). With nobody
logged in, any `auth`-prefixed command fails immediately with
`error not logged in`.

> This enforcement happens at rush's own application layer (checking
> the logged-in user's role before running the inner command) — it is
> not OS-level process isolation (e.g. Windows integrity levels or
> restricted tokens). That remains a possible future hardening, not
> what's built today.

## 6. Variables

```
a = 1
a = "hello"
```

- Two types: `int` and `string`. No implicit coercion between them.
- Bare word = variable lookup: `show a`
- Inside a quoted string, `{name}` interpolates a variable:
  `show "{a} World"`
- Referencing an undefined variable is an error in both forms:
  `error var b not found`

## 7. Arithmetic (`calc`)

```
calc 3 * 3
```

- Supports `+ - * /` only in 0.1.
- Operates on `int` values only. Mixing `int` and `string` is an error:
  `error string and int collision`
- Output is the bare resulting value (no echoed expression).
- Result may be captured into a variable:
  `a = calc 3 * 3`

## 8. Pipes

rush uses `~` in place of the Unix `|`:

```
calc 3 * 3 ~ show
```

Output of the left-hand command is passed to the right-hand command.
Chaining multiple `~` is permitted.

### 8.1 Command chaining (`;`)

Multiple independent statements can be run on one line, separated by
`;`:

```
x = 1 ; show x ; calc 2 * 2
```

Unlike `~`, `;` does not pass any value between statements - each one
runs independently, in order. The line's overall result (for
assignment or further piping) is whichever the *last* statement
produced.

## 9. Network (`bounce`)

```
bounce https://google.com
```

Output:
```
bounce back took 0.30 seconds reach success
```

`bounce` performs a single TCP connection to the target host (not a
full HTTP request, and not ICMP like traditional `ping`) and reports
how long resolution + connection took, plus whether it succeeded. It
is meant as a quick, one-shot reachability test — not continuous
monitoring like `ping` does by default.

- If the URL includes a scheme, the port defaults accordingly:
  `https://` → 443, `http://` → 80, no scheme → 80.
- An explicit port may be given: `bounce example.com:8080`.
- Any path in the URL is ignored — `bounce` only tests the host, not
  a specific page.
- Result (`"success"` or `"failure"`) can be captured like any other
  command's output: `a = bounce https://example.com`.
- An optional attempt count may follow the URL: `bounce example.com 3`
  runs three attempts in sequence, printing one result line per
  attempt (numbered); the captured value reflects the *last*
  attempt's outcome.
- If no count is given inline, a piped integer is used as the attempt
  count instead: `calc 3 * 3 ~ bounce https://example.com` runs 9
  attempts. This is `bounce`'s answer to §19's open question of
  whether piped values are actually consumed rather than ignored —
  other commands may still just discard piped input if it isn't
  relevant to what they do.

## 10. Control Flow

### Loops

```
loop 3
show "hello there!"
end
```

`loop <n>` opens a block; `end` closes it. The REPL prompt continues
accepting lines until `end` is entered.

### Conditionals

```
if a == 1
skipto next
end
```

`if <condition>` opens a block, closed by `end`.

### Labels and jumps (`skipto`)

`skipto <label>` jumps forward to a named label. This is an
intentionally restricted jump primitive, not a general-purpose
`goto`:

- **Forward-only.** `skipto` may only jump to a label that appears
  *below* it in the script, at any distance. It can never jump
  backward.
- This rule removes the classic danger of `goto` (invisible loops
  created by jumping backward past initialization). Since `skipto`
  cannot move backward, it cannot create a cycle — the only way to
  repeat code in rush is `loop`. `skipto` skips code forward; `loop`
  repeats code. The two are never the same construct.
- If no matching label exists below the current line, this is a
  parse-time error, caught before the script runs:
  `error no label found to skip to`
- An undefined label referenced anywhere is likewise an error:
  `error label '<name>' not found`

## 11. Aliases (`ali`)

```
ali gs = git status
```

- Defines a shorthand for a longer command.
- **Persistent as of 0.2** (resolves the open question from §19):
  every `ali` immediately writes the full alias table to disk -
  `rush_data/user/<user>/alias.txt` if logged in, or
  `rush_data/alias.txt` if not.
- Logged-in and global aliases are **separate namespaces, not
  merged**: logging in loads that user's own alias set (replacing
  whatever was active), and logging out reloads the global set. An
  alias defined while logged in as one user is not visible globally
  or to a different user.
- An alias pointing to an external program follows the same rule as
  any external call: it only reaches outside rush when the target
  requires it (external `.exe`s are invoked via the same mechanism as
  `-default`).

## 12. External Programs (`-default`)

By default, rush commands are native, self-contained builtins. The
`-default` flag is the signal to leave rush and hand off to a real OS
process for commands that have a genuine rush-native alternative:

```
open file.txt              # opens in rush's own line editor
open file.txt -default     # opens in the OS default program (e.g. Notepad)
```

Some commands have no realistic rush-native alternative at all
(downloading, archive extraction — implementing HTTP/TLS or a
compression format from scratch is out of scope and not something to
attempt casually) — those delegate to the OS unconditionally. See
§12.1.

### 12.1 `open`, `edit`, `dload`, `extr`, `pack`

- **`open <file>`** enters a small interactive line editor:
  `list` (show numbered lines), `a <text>` (append), `d <n>` (delete
  line n), `r <n> <text>` (replace line n), `save`, `quit`. This is a
  distinct mini-mode, not part of rush's own command grammar — it
  exists only while `open` is running.
  `open <file> -default` skips all of that and hands off to the OS's
  registered default program for the file instead.
  - Known binary/media extensions (`.png`, `.jpg`, `.exe`, `.zip`,
    `.pdf`, and similar) are refused up front with
    `error unsupported file type`, along with a suggestion to use
    `open <file> -default` instead of trying to load them as text.
- **`edit <file> <line> <text>`** replaces a single line's content in
  one shot, no interactive mode — meant for scripting, where `open`'s
  interactivity would not make sense.
- **`dload <url> [output]`** and **`extr <archive> [dest]`** delegate
  to `curl` and `tar` respectively (both ship on modern Windows and
  Linux). `output`/`dest` default to a name derived from the URL, or
  the current directory.
- **`pack <output.zip> <file/folder...>`** creates a real zip archive.
  On Windows this uses PowerShell's `Compress-Archive` cmdlet (built
  into every modern Windows) rather than `tar.exe`'s zip support,
  which isn't consistently available across Windows builds. The
  Linux reference build here uses the `zip` utility instead, purely
  for local testing — the actual Windows build always uses
  `Compress-Archive`.

## 13. Errors

Format: `error <category or types> <plain-English description>`

Examples:
```
error var b not found
error string and int collision
error label 'next' not found
```

Errors are intended to name the *kind* of problem first, then describe
it in plain language — no bare exit codes, no unexplained failures.

## 14. Scripts (`.rsh`)

```
start                       # begins script execution
goin c:/Downloads/
dload file.zip
dload file1.7z
extr "file" "file1"
quit                        # ends the whole script
# comments use '#'
```

- `start` / `quit` bookend a script file.
- `quit` ends the entire script's execution.
- `exit` (interactive-only) closes the rush terminal itself.
- `#` denotes a comment, retained for editor/tooling compatibility
  despite not matching rush's plain-word philosophy elsewhere.

## 15. Command Reference (0.1)

| Command   | Description                                  |
|-----------|-----------------------------------------------|
| `show`    | Print text (Unix: `echo`)                     |
| `list`    | List directory contents (Unix: `ls`)          |
| `where`   | Print current directory (Unix: `pwd`)         |
| `goin`    | Change directory (Unix: `cd`); `goin home`
              jumps to the OS home directory - quoting it
              (`goin "home"`) means a literal folder instead |
| `del`     | Delete a file or folder                       |
| `mkf`     | Make a folder                                 |
| `mkfl`    | Make a file                                   |
| `write`   | Append text to a file                         |
| `owrite`  | Overwrite the contents of a file               |
| `time`    | Show current time                             |
| `read`    | Print file contents (Unix: `cat`)             |
| `about`   | Show details about a file/item                |
| `find`    | Search for a file or content                   |
| `rname`   | Rename a file or folder                        |
| `open`    | Interactive line editor for a file (§12.1)     |
| `edit`    | Replace one line in a file, non-interactive    |
| `dload`   | Download a file via `curl` (§12.1)             |
| `extr`    | Extract an archive via `tar` (§12.1)           |
| `pack`    | Create a zip archive (§12.1)                    |
| `calc`    | Evaluate an arithmetic expression              |
| `wait`    | Pause execution for N seconds                  |
| `bounce`  | Test network reachability (one-shot, like ping)|
| `ali`     | Define a persistent alias (§11)                |
| `dump`    | Delete a variable                              |
| `me`      | Show who is currently logged in (whoami)       |
| `help`    | List available commands                        |
| `saves`   | Save current variables as a session (§16)      |
| `loads`   | Load a saved session (§16)                      |
| `list sess` | List saved sessions (§16)                     |
| `del sess`  | Delete a saved session (§16)                  |
| `task`    | Run a `.rsh` script file from the REPL (§17)     |
| `run`     | Launch an external program (§17)                |
| `regi`    | Register a new account                          |
| `login`   | Log in as a registered user                     |
| `logout`  | End the current login session                   |
| `promo`   | Change a user's role (admin only)               |
| `demo`    | Step a user down one role (admin only) (§18)    |
| `del user`  | Delete an account (admin only) (§18)          |
| `list user` | List all registered accounts (§18)            |
| `package` | Run a package-manager verb via the configured
              backend (see §21)                                |
| `auth`    | Run a command at (or below) your logged-in
              role, or fall back to the configured default tier|
| `loop`    | Begin a repeat block                           |
| `if`      | Begin a conditional block                       |
| `skipto`  | Jump to a label                                |
| `end`     | Close a `loop` or `if` block                    |
| `start`   | Begin `.rsh` script execution                  |
| `quit`    | End script execution                           |
| `exit`    | Close the rush terminal                         |

> Note: `mkf`/`mkfl` remain under review — their similarity is a known
> open risk flagged for reconsideration before wider use.

## 16. Sessions (`saves`/`loads`)

```
x = 11
saves mywork
dump x
loads mywork
show x        # 11 again
```

`saves <name>` writes every current variable to a session file;
`loads <name>` clears the current variable table entirely and
replaces it with the saved one (not a merge). Stored at:
- `rush_data/user/<user>/sessions/<name>.txt` if logged in
- `rush_data/sessions/<name>.txt` if not

`list sess` and `del sess <name>` manage saved sessions, scoped the
same way (per-user if logged in, global otherwise).

## 17. Running Scripts and External Programs (`task`, `run`)

- **`task <script.rsh>`** runs a script file from inside an already-
  running rush session, using the same interpreter that handles
  `loop`/`if`/`skipto`/`label`/`start`/`quit` for `.rsh` files passed
  as `rush.exe`'s command-line argument. Variables set by the script
  remain set afterward, in the same session.
- **`run <program> [args...]`** launches an external program and
  hands control to the OS — unconditionally, with no rush-native
  alternative. This is a dedicated command distinct from `-default`
  (which only exists as a flag on specific commands like `open`).

## 18. User Management (`del user`, `list user`, `demo`)

- **`list user`** lists every registered account and role.
- **`del user <username>`** deletes an account (admin only). You
  cannot delete the account you are currently logged in as.
- **`demo <username>`** steps a user down exactly one role
  (`admin` → `member`, `member` → `guest`; admin only). Unlike
  `promo`, which takes an explicit target role, `demo` always steps
  down by one rank automatically.
  - Refuses to demote the only remaining admin
    (`error cannot demote the only remaining admin`) - this was a
    real, silent lockout risk found during testing: demoting the
    system's last admin leaves nobody who can `promo` anyone back to
    admin, and `regi ... admin` only works when *no* accounts exist
    at all yet.

> Naming note: a bare `del <username>` was considered, but rejected —
> it would collide with `del <file>` whenever a file happens to share
> a username, since there would be no way to tell which one was
> meant. `del user <username>` avoids that ambiguity entirely, at the
> cost of two extra words — matching the same domain-keyword pattern
> already established by `del sess <name>`. The same reasoning
> applies to `list user` / `list sess` versus a plain `list <path>`.

## 19. Open Questions for 0.2

- Whether `mkf`/`mkfl` should be renamed to remove their one-letter
  collision risk
- Whether `skipto` may jump forward out of its current block to a
  label after a different block's `end`, or only within the same block
- Whether pipes (`~`) work generally across all commands or only a
  defined subset
- Copy/move commands (no current equivalent to `cp`/`mv`)
- Whether `package config = winget`-style swappable OS backends
  generalize to other domains (`service`, `network`, etc.) or stay
  specific to `package`

## 20. Accounts & Privilege Enforcement (implemented in 0.2)

This was designed as a 0.2 feature and has since been built and
tested; kept here as the authoritative design reference.

**Accounts**
- `regi <username> <role>` registers a new user.
  - If no account exists yet at all, the very first `regi` may claim
    `admin`.
  - After that, `regi` may only create `guest` or `member` accounts.
  - Only an existing `admin` may grant admin: `promo <user> admin`.
- Usernames: letters and numbers only (also sidesteps any collision
  with the `:` delimiter used in storage).
- No password recovery: a lost password means registering a new
  account, or an admin/operator manually clearing
  `rush_data/users.txt`. This is an accepted trade-off, not an
  oversight.

**Storage**
- `rush_data/users.txt`, created relative to wherever `rush.exe` was
  launched from (captured once at startup, so `goin` moving the
  working directory afterward doesn't relocate it).
- One line per user: `username:role:salt:hash`
  - `hash` is SHA-256.
  - `salt` is a random per-user string mixed into the hash input,
    so two identical passwords never produce identical hash output
    (the file is treated as public, so this specifically closes off
    "these two accounts share a password" being visible for free —
    it is not about `SHA-256` being weak).

**Session**
- `login <username>` prompts for a password (input not echoed to the
  terminal). On success, that user stays "logged in" for the rest of
  the session, until `logout` or the program exits.
- `logout` ends the current session's login without closing rush.

**Privilege enforcement**
- Only `auth`-gated commands require login — everything else
  (`show`, `calc`, `list`, file commands, etc.) works with nobody
  logged in, exactly as in 0.1.
- If nobody is logged in and an `auth`-gated command is attempted:
  hard error, the command does not run.
- A logged-in user's assigned role is a hard **ceiling**: attempting
  `auth admin ...` while logged in as `member` fails with
  `error insufficient permission` — the command does not run.
- Typing a tier **at or below** your own assigned role is allowed and
  runs normally — this is self-imposed caution (e.g. an admin
  deliberately running something as `guest`), not an escalation path.

**Logging**
- `rush_data/log.txt`, created alongside `users.txt`.
- Every command is logged, not only `auth`-gated ones — this gives a
  complete audit trail rather than only covering the
  security-relevant half.
- Format: `<username> <command> <timestamp>`
- If nobody is logged in, the username field is `anonymous`.
- Failed login attempts (wrong password) are also logged.

## 21. Package Delegation (`package`)

`package` has no built-in package-management logic of its own — it
delegates every call to a configured external backend (`winget`,
`choco`, or a future Linux backend like `apt`), the same way `-default`
hands off to the OS. This keeps rush's own surface area small while
still making it genuinely useful for real package management.

```
package config = winget
package install git
```

- `package config = <backend>` sets the backend command rush will
  prefix onto every future `package` call. Unlike `auth config`, this
  is not currently gated behind being logged in as admin — it is not
  treated as a security-relevant setting, only a convenience one.
- `package <verb> <args...>` is rewritten to `<backend> <verb>
  <args...>` and run exactly like `-default` does (via the OS's
  normal process-launch mechanism), with its output passed through.
- Calling `package` before a backend is configured is an error:
  `error package backend not configured, use: package config = <name>`
- Combines naturally with `auth`: `auth admin package install git`
  requires being logged in with at least `admin` role before the
  package call is allowed to run.


