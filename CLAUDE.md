# CLAUDE.md — Project Rules for AI Assistants

## Documentation Maintenance (MANDATORY)

After **any** change to the project — code, architecture, bug fixes, new features, new syscalls, new native functions, new UI elements — you MUST update the relevant documentation:

### 1. `project_context.md` — Master project document
Update this file whenever:
- A new syscall is added or modified
- A new user-space application is added or significantly changed
- A kernel subsystem is changed (drivers, memory, fs, networking, etc.)
- A bug is fixed that has architectural significance
- A new SuluScript application is written or completed

### 2. `suluscript_documentation.md` — SuluScript language reference
**This is the authoritative SuluScript doc** (prefer over `SULUSCRIPT.md` which is older).
Update this file whenever:
- A new native function is added to `sulu_interpreter.c`
- A new built-in variable is exposed to scripts
- A new UI element (e.g. `textbox`, `canvas`) is added to the interpreter
- A parser feature is added (new operator, new syntax)
- A language bug is fixed that changes behavior

Both files should be updated **in the same session** as the code change, not deferred.

---

## Project Overview

- **Repo**: `/Users/takudzwamakoni/dev/xv6-riscv`
- **Active branch**: `research-iart`
- **Main branch** (for PRs): `riscv`
- Full context: see `project_context.md`
