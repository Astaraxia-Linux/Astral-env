# astral-env Documentation

> *"Because manually configuring your system like a caveman is so last century"*

Version: 1.0.0.0  
Last Updated: 06 March 2026 (GMT+8)  
Maintained by: Same One Maniac™ (still just one)

---

## Table of Contents

1. [Introduction](#introduction)
2. [Installation](#installation)
3. [Configuration Format](#configuration-format)
4. [Environment Management](#environment-management)
5. [System Management](#system-management)
6. [File Snapshots](#file-snapshots)
7. [Store & GC](#store--gc)
8. [Daemon & Auto-snapshots](#daemon--auto-snapshots)
9. [CLI Reference](#cli-reference)
10. [Troubleshooting](#troubleshooting)
11. [FAQ](#faq)

---

## Introduction

### What is astral-env?

astral-env is the declarative environment and system configuration layer for Astaraxia Linux. It sits on top of Astral and lets you describe your entire system — packages, services, dotfiles, hostname, timezone, file snapshots — in a single `.stars` file. Then it makes reality match the file.

Think of it as:
- **Nix** without the functional language that makes your brain hurt
- **Ansible** without the YAML sprawl and Python dependency
- **Gentoo `savedconfig`** but for your whole system
- **A really fancy declarative wrapper** over Astral (that's literally what it is)

### Why astral-env?

- **Declarative**: Describe what you want, not how to get there
- **Reproducible**: Same `.stars` file = same system, every time
- **Rollbackable**: Applied something that broke everything? `system rollback`
- **Snapshot-aware**: Content-addressed file snapshots with deduplication
- **GC-aware**: Unused store entries get cleaned up automatically
- **Per-user config**: Global system config + per-user dotfiles/packages in one go

### Why Not astral-env?

- You enjoy typing `systemctl enable` 47 times after a fresh install
- You like your system configuration scattered across 12 different files
- You have a great memory and never forget what you changed
- You're a normal person

---

## Installation

### Prerequisites

- Astral 5.0.0.0+ (obviously)
- A C++20 compiler (`gcc` or `clang`)
- `cmake` >= 3.20
- `openssl` >= 3.0 (for SHA-256 store hashing)
- `zstd` (for file snapshots — `astral -S zstd`)
- The will to live

### Building from Source

```bash
# Clone
git clone https://github.com/Astaraxia-Linux/Astral-env
cd Astral-env

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install
sudo cmake --install build
```

This installs:
- `/usr/bin/astral-env` — the main binary
- `/usr/bin/astral-env-snapd` — the snapshot daemon

### First-time Setup

```bash
# Enable astral-env in /etc/astral/astral.stars
sudo astral-env system init

# Create your user config (replace izumi with your username)
sudo astral-env system init-user izumi
```

You'll need to tell Astral that astral-env exists. Add to `/etc/astral/astral.stars`:

```
$AST.core: {
    astral-env        = "enabled"
    astral-env-system = "enabled"
};
```

---

## Configuration Format

astral-env uses `.stars` files — the same syntax as Astral recipes. If you've written a recipe, you already know this.

### astral-env.stars (Project Environments)

For per-project development environments (like a `shell.nix` but readable):

```
$ENV.Version = "3"

$ENV.Metadata: {
    Name        = "my-project"
    Description = "My cool project environment"
};

$ENV.Packages: {
    python >= 3.11
    nodejs >= 20.0
    git
};

$ENV.Vars: {
    DEBUG    = "true"
    NODE_ENV = "development"
};

$ENV.Shell: {
    echo "Welcome to my-project environment"
    export PATH="$PWD/bin:$PATH"
};
```

### env.stars (System Configuration)

Lives at `/etc/astral/env/env.stars`. Manages the whole system:

```
$ENV.Version = "3"

$ENV.System: {
    hostname = "izumi"
    timezone = "Asia/Kuala_Lumpur"
};

$ENV.Packages: {
    neovim
    htop
    git
    zsh
};

$ENV.Services: {
    sshd    = "enabled"
    cronie  = "enabled"
    NetworkManager = "enabled"
};
```

### User Config (izumi.stars)

Lives at `/etc/astral/env/izumi.stars`. Per-user packages, dotfiles, and environment:

```
$ENV.Version = "3"

$ENV.User: {
    name  = "izumi"
    shell = "/bin/zsh"
};

$ENV.Packages: {
    firefox
    thunderbird
    mpv
};

$ENV.Dotfiles: {
    "/home/izumi/.config/nvim"    = "nvim"
    "/home/izumi/.zshrc"          = "zshrc"
    "/home/izumi/.gitconfig"      = "gitconfig"
};

$ENV.Vars: {
    EDITOR  = "nvim"
    BROWSER = "firefox"
};
```

Dotfiles in `$ENV.Dotfiles` are symlinked from `/etc/astral/env/dotfiles/izumi/`. Manage your configs in one place, have them appear wherever you need them.

### $ENV.Snap (Automatic Snapshots)

Want astral-env to automatically snapshot important paths? Add this to your config:

```
$ENV.Snap: {
    on_interval      = "true"
    default_interval = "1" "H"    # S=seconds M=minutes H=hours D=days

    path: {
        "/home/izumi/.config/hyprland"    # uses default_interval (1 hour)
        "/home/izumi/.zshrc": {
            interval = "autosave"         # snapshot on every file change
        };
        "/etc/astral": {
            interval = "6" "H"
        };
    };
};
```

`autosave` uses inotify (Linux) to detect changes and snapshot immediately. Everything else is timer-based. The snapshot daemon handles this — see [Daemon & Auto-snapshots](#daemon--auto-snapshots).

---

## Environment Management

Per-project environments. Like `nix-shell` but without needing a PhD.

### Creating an Environment

```bash
# Scaffold a new astral-env.stars
astral-env init

# Generate lockfile (resolves versions)
astral-env lock

# Build the environment (installs packages to store)
astral-env build
```

### Using the Environment

```bash
# Drop into an interactive shell with the environment active
astral-env shell

# Run a single command in the environment
astral-env run python main.py

# Check what's installed and what's missing
astral-env status
```

### Updating

```bash
# Update a specific package
astral-env lock --update python

# Update everything
astral-env lock --update
```

The lockfile (`astral-env.lock`) pins exact versions. Commit it to your repo. Your teammates will thank you (or they would if you had teammates).

### Store Layout

Packages live in the content-addressed store:

```
/astral-env/store/
  sha256-<64hex>-python-3.12.4/
    bin/
    lib/
    ...
  sha256-<64hex>-nodejs-20.11.0/
    ...
```

Multiple projects can share the same store — if two projects need the same version of Python, it's stored once. The GC knows which entries are still referenced.

---

## System Management

This is the "make my whole system declarative" part.

### Workflow

```bash
# See what would change (safe, read-only)
sudo astral-env system diff

# Apply changes
sudo astral-env system apply

# If it goes wrong
sudo astral-env system rollback

# Check config files for errors
astral-env system check
```

### Diffing

`system diff` compares your `.stars` files against actual system state:

```
astral-env system diff
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  [+] htop          (will install)
  [+] cpufetch      (will install)
  [~] sshd          (will enable)
  [+] /home/izumi/.zshrc  -> dotfiles/izumi/zshrc
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

Legend: `[+]` install/create, `[-]` remove/unlink, `[~]` change, `[!]` conflict.

### Applying

```bash
# Interactive (asks for confirmation)
sudo astral-env system apply

# Skip confirmation
sudo astral-env system apply --yes

# Dry run (shows what would happen, changes nothing)
sudo astral-env system apply --dry-run

# Apply only global config, skip user configs
sudo astral-env system apply --global-only

# Apply only a specific user's config
sudo astral-env system apply --user izumi
```

astral-env automatically saves a rollback snapshot before applying anything. You're welcome.

Package installs use `astral --parallel-build` when there are multiple packages — so your 47 packages install in parallel instead of one at a time. Coffee break may no longer be required.

### Rolling Back

```bash
# Roll back to the most recent snapshot
sudo astral-env system rollback

# List available snapshots
sudo astral-env system rollback --list

# Roll back to a specific snapshot
sudo astral-env system rollback --to 2026-03-06_14:32
```

Rollback restores:
- Service enable/disable states
- Symlink targets (dotfiles)
- Hostname and timezone

It does **not** restore packages (that's destructive and we're not monsters) or regular files (use `snap restore` for that).

### Initialising

```bash
# Create /etc/astral/env/env.stars
sudo astral-env system init

# Create /etc/astral/env/izumi.stars + dotfiles dir
sudo astral-env system init-user izumi

# Validate all .stars files in /etc/astral/env/
astral-env system check
```

---

## File Snapshots

Content-addressed, zstd-compressed, deduplicated file snapshots. Sounds fancy, works simply.

### Manual Snapshots

```bash
# Snapshot a file
astral-env snap /home/izumi/.zshrc

# Snapshot a whole directory
astral-env snap /home/izumi/.config/hyprland

# List all snapshots
astral-env snap list

# List snapshots for a specific path
astral-env snap list /home/izumi/.zshrc

# Restore a snapshot
astral-env snap restore snap-2026-03-06_14:32:00

# Restore to a different location
astral-env snap restore snap-2026-03-06_14:32:00 --dest /tmp/zshrc.bak
```

### How It Works

Snapshots are stored content-addressed under `/astral-env/store/snap/`:

```
/astral-env/store/snap/
  sha256-<64hex>/
    data.zst      # zstd-compressed tar of the snapshotted path
    meta.json     # original path, permissions, timestamp

/astral-env/snapshots/files/
  snap-2026-03-06_14:32:00.json   # index entry: links ID → blob + reason
```

**Deduplication**: If you snapshot the same file twice and it hasn't changed, the second snapshot reuses the existing blob. Storage costs nothing.

**Reasons**: Snapshots are tagged with how they were created — `manual`, `pre-apply`, `scheduled`, or `autosave`. Useful for knowing why a snapshot exists when you're digging through the list at 2am.

### Pruning

```bash
# Keep only the last 5 snapshots per path
astral-env snap prune --keep-last 5

# Remove snapshots older than 14 days
astral-env snap prune --older-than 14d

# Default prune (keep last 5, remove >30 days old)
astral-env snap prune
```

The GC knows about snapshots — it will never collect a blob that's referenced by a snapshot index entry. Prune first if you want to free space.

---

## Store & GC

### The Store

Everything lives under `/astral-env/store/`:

```
/astral-env/store/
  sha256-<64hex>-python-3.12.4/    # package entries
  sha256-<64hex>-nodejs-20.11.0/
  snap/                             # snapshot blobs (never touched by package GC)
    sha256-<64hex>/
      data.zst
      meta.json
```

### Garbage Collection

```bash
# See what would be collected (safe)
astral-env gc --dry-run

# Collect entries unused for 30+ days (default)
astral-env gc

# Be more aggressive
astral-env gc --max-age 7

# Store usage
astral-env store size
astral-env store list
```

The GC:
- Only collects entries with a `.complete` marker (partial installs are safe)
- Skips anything referenced by any lockfile found under `/home`, `/root`
- Skips the `snap/` subdirectory entirely (snapshot GC is separate, via `snap prune`)
- Skips entries newer than `--max-age` days

---

## Daemon & Auto-snapshots

`astral-env-snapd` handles scheduled and autosave snapshots in the background.

### Starting the Daemon

```bash
# Start and enable at boot (auto-detects your init system)
astral-env snapd start

# Other controls
astral-env snapd stop
astral-env snapd restart
astral-env snapd status
```

Supported init systems: systemd, OpenRC, runit, s6, dinit, launchd, SysVinit.

The daemon reads `$ENV.Snap` blocks from your tracked paths config and:
- Wakes up every 60 seconds to check if any path is overdue for a scheduled snapshot
- Uses inotify/kqueue/FSEvents for `autosave` paths, snapshotting within 5 seconds of a change settling

### Autosave Debounce

Multiple rapid writes (e.g. an editor saving) won't create a snapshot per write. The daemon waits for 5 seconds of quiet after the last change before snapshotting. Configurable:

```
$ENV.Snap: {
    path: {
        "/home/izumi/.zshrc": {
            interval          = "autosave"
            autosave_debounce = "5" "S"
        };
    };
};
```

---

## CLI Reference

```
astral-env <command> [options]

Environment Commands:
  init                  Scaffold astral-env.stars in current directory
  lock [--update [pkg]] Generate/update lockfile from .stars
  build [--force]       Build environment from lockfile
  shell [--dir <d>]     Enter interactive environment shell
  run <cmd...>          Run command inside the environment
  status                Show what's installed vs missing

System Commands:
  system init           Create /etc/astral/env/env.stars
  system init-user <u>  Create per-user config + dotfiles directory
  system diff           Show pending changes
  system apply          Apply changes (--dry-run, --yes, --user, --global-only)
  system rollback       Roll back (--list, --to <id>)
  system check          Validate all .stars files

Snapshot Commands:
  snap <path>           Snapshot a file or directory
  snap list [path]      List snapshots (optionally filtered by path)
  snap restore <id>     Restore a snapshot (--dest <path> for alternate location)
  snap prune            Prune old snapshots (--keep-last N, --older-than Nd)

Store Commands:
  store list            List all store entries
  store size            Show total store disk usage
  gc [--dry-run]        Garbage collect unused entries (--max-age <days>)

Daemon Commands:
  snapd start           Start snapshot daemon (enables at boot)
  snapd stop            Stop snapshot daemon
  snapd restart         Restart snapshot daemon
  snapd status          Show daemon status

Global Options:
  -v, --verbose         Verbose output
  -q, --quiet           Quiet output
  -V, --version         Show version
  -h, --help            Show this help
```

---

## Troubleshooting

### "astral-env is not enabled"

```
ERROR: astral-env is not enabled.
       Set 'astral-env = "enabled"' in $AST.core in /etc/astral/astral.stars
```

**Fix**: Edit `/etc/astral/astral.stars` and add:
```
$AST.core: {
    astral-env = "enabled"
};
```

### "astral-env-system is not enabled"

Same deal but for system commands.

**Fix**:
```
$AST.core: {
    astral-env        = "enabled"
    astral-env-system = "enabled"
};
```

### "zstd is required for snapshots"

**Fix**:
```bash
sudo astral -S zstd
```

### "No lockfile found"

You forgot to run `lock` before `build`.

**Fix**:
```bash
astral-env lock
astral-env build
```

### "Snapshot blob not found"

The blob was GC'd before the index entry was pruned. This shouldn't happen normally (the GC checks the snap index), but if it does:

**Fix**:
```bash
# Remove the dangling index entry
astral-env snap prune --keep-last 0  # nuclear option
# or manually remove the specific .json from /astral-env/snapshots/files/
```

### "Failed to install \<package\>"

astral-env uses `astral --parallel-build` for multiple packages. Check astral's own logs:

```bash
ls /var/log/astral_sync_*.log
tail -f /var/log/astral_sync_*.log
```

### Rollback only partially worked

Rollback restores services, symlinks, hostname, and timezone. It cannot restore:
- **Packages**: Uninstalling things that were just installed is destructive. Use `astral -R` manually.
- **Regular files** that were overwritten (not symlinks): Use `snap restore` if you had a snapshot.

**Lesson learned**: Run `astral-env snap /important/file` before doing anything scary.

---

## FAQ

### Why C++ instead of POSIX sh like Astral?

Astral gets away with sh because package operations are naturally sequential and shelling out to `tar`, `make`, etc. is fine. astral-env needs concurrent file hashing, inotify event loops, content-addressed storage, and a proper GC — doing that in sh would be a crime against humanity. C++20 with `std::filesystem` hits the sweet spot.

### Does astral-env replace Astral?

No. astral-env is a layer on top of Astral. It uses `astral -S` to install packages — it doesn't reimplement the package manager.

### Can I use astral-env without system management?

Yes. The project environment features (`init`, `lock`, `build`, `shell`, `run`) work completely independently without `astral-env-system = "enabled"`.

### Are my dotfiles safe?

When applying dotfile symlinks, astral-env:
1. Checks if something already exists at the destination
2. Asks for confirmation before overwriting a regular file (unless `--yes`)
3. Saves a rollback snapshot of the current state before applying anything

So... yes, probably. Keep backups anyway. We're not responsible for your `.zshrc`.

### What happens if two users have conflicting config?

The global `env.stars` applies first. Per-user configs apply on top. If there's a conflict (e.g. two users want different versions of the same package), it shows up as a `[!]` conflict in `system diff`. You'll need to resolve it manually in the config files.

### Can I track my configs in git?

Absolutely yes. That's the whole point. Put your `.stars` files and `/etc/astral/env/dotfiles/` in a repo. Fresh install → clone → `system apply` → done.

### How is this different from dotfiles managers?

dotfiles managers (chezmoi, stow, etc.) only handle dotfiles. astral-env handles dotfiles *and* packages *and* services *and* system settings *and* snapshots, all in one declarative file. It's more opinionated but covers more ground.

### Snapshot deduplication — how much space does it actually save?

Depends entirely on how often your files change. If you're snapshotting your neovim config hourly and only editing it once a day, 23 of those 24 snapshots are free (zero bytes). If you're snapshotting a directory that changes every hour, you're paying full price each time. `astral-env store size` will tell you the truth.

### Who maintains this?

Same One Maniac™. Two projects, one maniac. The math is concerning.

---

## Credits

- **Created by**: One Maniac™ (the same one)
- **Inspired by**: NixOS, GNU Stow, Ansible, and the desire to never type `systemctl enable` again
- **Special thanks**: The C++ committee, for `std::filesystem` finally working properly

---

## License

GPL-3.0, same as Astral. Because consistency.

---

## Final Notes

> *"astral-env: because your system configuration shouldn't live in your head"*  
> — Also nobody, ever

astral-env is young and opinionated. The store format is stable. The `.stars` syntax is stable. Everything else might change. Pin your versions.

If you have suggestions, bugs, or existential crises about declarative configuration, open an issue. The One Maniac™ will get to it eventually.

---

**Last updated**: 06 March 2026 (GMT+8)  
**Documentation version**: 1.0  
**Sanity level**: Surprisingly intact
