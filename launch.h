#ifndef RUSH_LAUNCH_H
#define RUSH_LAUNCH_H

/* Tracks processes started by the "launch" command so they can be
 * listed ("list pros") and killed ("kill pros <n>") later, without
 * blocking rush's own console the way run/task (system()) do. */

#define RUSH_MAX_PROCESSES 32

/* Starts `path` as a new, detached process registered under `name`.
 * Prints "launched <name> as pros no. <n>" and returns 0 on success,
 * or prints an error and returns -1 if it could not be started or the
 * process table is full. */
int launch_spawn(const char *name, const char *path);

/* Prints every still-tracked process as
 * "<name> pros no. <n> (pid <pid>) - running/exited", pruning entries
 * that have exited since the last check. Prints a message instead if
 * nothing is tracked. */
void launch_list(void);

/* Kills and unregisters the process at the given slot number. Returns
 * 0 on success, -1 (with an error already printed) if no such slot is
 * currently tracked. */
int launch_kill(int slot);

#endif
