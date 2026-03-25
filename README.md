# astral-env

Version: 2.0.0.0  
Maintained by: Izumi Sonoka  
*Made in Malaysia, btw*

---

## Table of Contents

1. [What Changed in 2.0](#what-changed-in-20)
2. [Introduction](#introduction)
3. [Installation](#installation)
4. [Configuration Format](#configuration-format)
5. [env.stars Reference](#envstars-reference)
6. [hw.stars Reference](#hwstars-reference)
7. [User Config Reference](#user-config-reference)
8. [Custom Repositories](#custom-repositories)
9. [Environment Management](#environment-management)
10. [System Management](#system-management)
11. [File Snapshots](#file-snapshots)
12. [Store & GC](#store--gc)
13. [Daemon & Auto-snapshots](#daemon--auto-snapshots)
14. [CLI Reference](#cli-reference)
15. [Init System Support](#init-system-support)
16. [Troubleshooting](#troubleshooting)
17. [TODOs](#todos)
18. [FAQ](#faq)

---

## What Changed in 2.0

**astral-env is now the primary tool.** It no longer just wraps astral it manages its own repositories, fetches its own index, installs binary packages from `/bin/`, installs fonts from `/font/`, and only delegates to astral for source-build recipes that need its full build pipeline.

**Custom repositories work.** You declare repos in `env.stars` using `github::Owner/Repo` shorthand. astral-env fetches the index, resolves packages, and installs from the right path (`/recipes`, `/bin`, or `/font`) based on what's available and your `binaryPkg` setting.

**All TODOs from 1.x are now implemented:**
- Locale, vconsole, and console font
- X server driver config
- Kernel module injection (initrd and runtime)
- CPU microcode (AMD/Intel)
- All-firmware loading
- Graphics driver setup and modprobe blacklisting
- `/etc/fstab` generation
- Networking (NetworkManager / dhcpcd / static IP)
- User account creation with group assignment
- Shell aliases written to rc files
- Environment variables written to shell profile and `/etc/environment`
- Snapshot daemon boot integration (systemd, OpenRC, runit, s6, dinit, SysVinit)
- Bootloader kernel params and timeout (limine and grub)
- dinit and SysVinit service management
- `system sync-index` CLI command

**astral is still used for:** source-build recipes that go through the full `astral -S` pipeline.

---

## Introduction

astral-env is the declarative environment and system configuration layer for Astaraxia Linux. Declare your entire system in `.stars` files packages, services, dotfiles, hostname, timezone, kernel modules, hardware, filesystems, users and `system apply` makes it real.

---

## Installation

### Prerequisites

- Astaraxia Linux with Astral 5.0.0.0+
- C++23 compiler (`gcc >= 13` or `clang >= 17`)
- `make`, `pkg-config`
- `libcurl >= 8.0`
- `openssl >= 3.0`
- `zstd` - `astral -S zstd`

### Building

```bash
git clone https://github.com/Astaraxia-Linux/Astral-env
cd Astral-env
make -j$(nproc)
sudo make install
```

Installs:
- `/usr/bin/astral-env`
- `/usr/bin/astral-env-snapd`

### First-time Setup

```bash
sudo astral-env system init
sudo astral-env system init-user yourname
```

Enable in `/etc/astral/astral.stars`:

```
$AST.core: {
    astral-env        = "enabled"
    astral-env-system = "enabled"
};
```

---

## Configuration Format

All config lives in `.stars` files under `/etc/astral/env/`.

### Syntax Rules

**Root block:**

```
$ENV: {
    ...
};
```

**Dot-shorthand** dots in key names expand to nested sets:

```
$ENV.System: {
    Packages.System.Packages: [
        neovim
        git
    ];
};
```

is equivalent to:

```
$ENV: {
    System: {
        Packages: {
            System: {
                Packages: [
                    neovim
                    git
                ];
            };
        };
    };
};
```

**Cases:**
- Block names: `PascalCase` - `System`, `Packages`, `Services`, `Boot`
- Key names: `camelCase` - `hostName`, `timeZone`, `binaryPkg`

**Package suffixes:**
- `-git` - git version
- `-Vx.x.x` - exact version
- `-bin` - binary package (requires `binaryPkg = "false"`)
- `-source` - source build (requires `binaryPkg = "true"`)

**Lists:**

```
kernelParams = [ "quiet", "rw" ]
```

**Comments:**

```
# Single line

#/
    Multi-line comment.
    Completely ignored.
/#
```

**Includes** pull in another `.stars` file with recursive merge:

```
$ENV: {
    Includes: {
        ./hw.stars
    };
};
```

Both files' values are merged recursively. If both define the same scalar, the including file wins. If both define the same block, the contents are combined.

---

## env.stars Reference

`/etc/astral/env/env.stars` - global system configuration.

```
$ENV: {
    Description = "My system"

    Includes: {
        ./hw.stars
    };

    System: {
        hostName   = "my-machine"
        timeZone   = "Asia/Kuala_Lumpur"
        layout     = "us"
        xkbVariant = "altgr-intl"

        i18n: {
            defaultLocale = "en_US.UTF-8"
            extraLocaleSettings = {
                LC_ADDRESS    = "en_MY.UTF-8";
                LC_MONETARY   = "en_MY.UTF-8";
                LC_TIME       = "en_MY.UTF-8";
            };
        };

        Console: {
            font   = "Lat2-Terminus16"
            keyMap = "us"
        };

        Server: {
            enable.xserver = "true"
        };

        Packages: {
            binaryPkg = "false"

            Repository: {
                AOHARU: {
                    url = "github::Izumi-Sonoka/AOHARU"
                };
                MyRepo: {
                    url = "github::MyUser/MyRepo"
                };
            };

            System.Fonts: {
                nerdFonts: {
                    Iosevka-mono
                    JetBrainsMono
                };
            };

            System.Packages: {
                neovim
                wget
                curl
                alacritty
                git
            };
        };

        Services: {
            sshd            = "enabled"
            NetworkManager  = "enabled"
        };
    };

    User: {
        Users: {
            iskandar: {
                normalUser     = "true"
                homeDir        = "/home/iskandar"
                moreGroup: [
                    "wheel",
                    "networkmanager",
                    "video",
                    "audio"
                ];
                userConfigPath = "/etc/astral/env/users/iskandar.stars"
            };
        };
    };

    Snap: {
        on_interval      = "true"
        default_interval = "1" "H"

        path: {
            "/home/iskandar/.config/hyprland"
            "/home/iskandar/.zshrc": {
                interval          = "autosave"
                autosave_debounce = "5" "S"
            };
        };
    };
};
```

### System fields

| Key | Type | Description |
|-----|------|-------------|
| `hostName` | string | System hostname written to `/etc/hostname` |
| `timeZone` | string | Timezone symlinks `/etc/localtime` |
| `layout` | string | Keyboard layout |
| `xkbVariant` | string | XKB variant |
| `i18n.defaultLocale` | string | Written to `/etc/locale.conf`, locale-gen run |
| `i18n.extraLocaleSettings` | map | LC_* overrides |
| `Console.font` | string | Written to `/etc/vconsole.conf` |
| `Console.keyMap` | string | Written to `/etc/vconsole.conf` |
| `Server.enable.xserver` | bool | Writes `/etc/X11/xorg.conf.d/20-gpu.conf` |
| `Packages.binaryPkg` | bool | Prefer `/bin/` tarballs over source builds |
| `Packages.Repository.<name>.url` | string | Repo URL (see [Custom Repositories](#custom-repositories)) |
| `Packages.System.Fonts.nerdFonts` | list | NerdFont families to install from `/font/` |
| `Packages.System.Packages` | list | System packages to install |
| `Services.<name>` | string | `"enabled"`, `"disabled"`, or `"masked"` |

---

## hw.stars Reference

Hardware configuration typically pulled in from `env.stars` via `Includes`.

```
$ENV: {
    Boot: {
        Initrd: {
            Kernel.Modules = [
                "xhci_pci", "ahci", "nvme",
                "usb_storage", "usbhid", "sd_mod"
            ];
        };

        Kernel: {
            Modules = [ "kvm-amd" ];
        };

        Loader: {
            type         = "limine"
            timeout      = "30"
            kernelParams = [ "quiet", "rw" ]
        };
    };

    Hardware: {
        cpu               = "amd"
        enableAllFirmware = "true"
        graphics          = "amdgpu"
    };

    FileSystems: {
        Disk: {
            "/": {
                diskPath = "/dev/sda2"
                fsType   = "ext4"
            };
            "/boot": {
                diskPath = "/dev/sda1"
                fsType   = "vfat"
            };
            "/tmp": {
                diskPath = "none"
                fsType   = "tmpfs"
            };
        };
    };

    Networking: {
        useDHCP = "true"
        Interface: {
            wlp0s12f0: {
                useDHCP = "true"
            };
            eth0: {
                useDHCP  = "false"
                address  = "192.168.1.100/24"
                gateway  = "192.168.1.1"
                dns      = "1.1.1.1"
            };
        };
    };
};
```

### Hardware fields

| Key | Applied to |
|-----|------------|
| `Boot.Initrd.Kernel.Modules` | `/etc/mkinitcpio.conf` MODULES, or dracut |
| `Boot.Kernel.Modules` | `/etc/modules-load.d/astral-env.conf` |
| `Boot.Loader.type` | Bootloader config (`limine.cfg` or `/etc/default/grub`) |
| `Boot.Loader.timeout` | Bootloader timeout |
| `Boot.Loader.kernelParams` | Appended to bootloader cmdline |
| `Hardware.cpu` | `"amd"` → installs `amd-ucode`, `"intel"` → `intel-ucode` |
| `Hardware.enableAllFirmware` | Installs `linux-firmware` |
| `Hardware.graphics` | Installs driver package, writes xorg.conf.d, modprobe blacklist |
| `FileSystems.Disk` | Written to `/etc/fstab` |
| `Networking.useDHCP` | Enables NetworkManager or dhcpcd globally |
| `Networking.Interface.<n>` | Per-interface config (DHCP or static: `address`, `gateway`, `dns`) |

---

## User Config Reference

`/etc/astral/env/<username>.stars` or wherever `userConfigPath` points.

```
$ENV: {
    Description = "iskandar's config"

    User: {
        name  = "iskandar"
        shell = "/bin/zsh"
    };

    Aliases: {
        nasi = "echo nasi lemak is tasty"
        ff   = "fastfetch"
        ls   = "ls --color=auto"
    };

    Packages: {
        firefox
        thunderbird
        mpv
        qtile
    };

    Config: {
        Symlinks: {
            ".config/qtile" = "/astral-env/users/iskandar/qtile"
        };

        Dotfiles: {
            "/home/iskandar/.zshrc": {
                path = "/astral-env/users/iskandar/dotfiles/zshrc"
            };
            "/home/iskandar/.gitconfig": {
                path = "/astral-env/users/iskandar/dotfiles/gitconfig"
            };
        };
    };

    Vars: {
        EDITOR  = "nvim"
        BROWSER = "firefox"
        TERM    = "alacritty"
    };
};
```

### What gets applied

| Section | Applied to |
|---------|------------|
| `User.name`, `User.shell` | `useradd` / `usermod` |
| `Aliases` | Appended to `~/.zshrc`, `~/.bashrc`, or `~/.config/fish/config.fish` |
| `Packages` | Installed to `/astral-env/users/<name>/`, binaries symlinked to PATH |
| `Config.Dotfiles` | Symlinked to declared destination paths |
| `Config.Symlinks` | Same as Dotfiles destination → source |
| `Vars` | Appended to shell rc and `/etc/environment` |

---

## Custom Repositories

Declare repos in `$ENV.System.Packages.Repository`. astral-env fetches their index and resolves packages from the right subdirectory.

### URL shorthand

| Shorthand | Expands to |
|-----------|------------|
| `github::Owner/Repo` | `https://raw.githubusercontent.com/Owner/Repo/refs/heads/main` |
| `codeberg::Owner/Repo` | `https://codeberg.org/Owner/Repo/raw/branch/main` |
| `https://...` | Used as-is |

### Required repo layout

```
<repo-root>/
    astral.index                                         package index (same format as AOHARU)
    recipes/<cat>/<s>/<pkg>/<pkg>-<ver>-<arch>.stars     source recipes
    bin/<arch>/<cat>/<pkg>-<ver>.stars                   pointer recipe
    bin/<arch>/<cat>/<pkg>-<ver>-<arch>.tar.bz2          binary package
    font/<family>/<family>-f<type>[-nerd].stars          pointer recipe
    font/<family>/<family>-f<type>[-nerd].tar.bz2        font tarball
```

The index format is the same as astral's `astral.index`. As of v2.1, the hierarchical format is preferred:

```
/recipes: {
    app-misc: {
        neofetch | any | 7.1.0
        fastfetch | any | 2.41.0
    };
    sys-devel: {
        gcc | x86_64 | 15.2.0
    };
};
/bin: {
    app-misc: {
        fastfetch | x86_64 | 2.41.0
    };
};
```

The old flat format (categories at top level, no `/recipes:` wrapper) is still supported for compatibility.

### Install resolution

When you run `system apply`, for each package:
1. Query cached index files for all declared repos
2. If `binaryPkg = "true"` look for the package in `/bin/`, download and extract to store
3. Otherwise delegate to `astral -S <pkg>` which uses `/recipes/`
4. Fonts always come from `/font/` regardless of `binaryPkg`

Sync indexes manually:

```bash
sudo astral-env system sync-index
```

Generate an index for your own repo:

```bash
astral-sync gen-index --repo MyRepo --dir ~/my-repo --out ~/my-repo/astral.index
```

---

## Environment Management

Per-project dev environments.

```bash
astral-env init               # scaffold astral-env.stars
astral-env lock               # resolve versions, generate lockfile
astral-env lock --update pkg  # update one package
astral-env lock --update      # update everything
astral-env build              # install from lockfile
astral-env build --force      # reinstall even if present
astral-env shell              # enter environment shell
astral-env run python main.py # run command in environment
astral-env status             # what's installed vs missing
```

The lockfile (`astral-env.lock`) pins exact versions and store paths. Commit it.

---

## System Management

```bash
sudo astral-env system diff             # preview all pending changes
sudo astral-env system apply            # apply everything
sudo astral-env system apply --yes      # skip confirmation
sudo astral-env system apply --dry-run  # show only, change nothing
sudo astral-env system apply --user iskandar
sudo astral-env system apply --global-only
sudo astral-env system rollback
sudo astral-env system rollback --list
sudo astral-env system rollback --to 2026-03-23_14:32
sudo astral-env system sync-index       # refresh repo indexes
astral-env system check                 # validate .stars files
```

### What `system apply` does

In order:
1. Hostname and timezone
2. Packages (binary from `/bin/` or source via `astral -S`)
3. Fonts from `/font/`
4. Services (enable/disable)
5. Dotfile and symlink creation
6. Locale generation and `/etc/locale.conf`
7. `/etc/vconsole.conf` (console font and keymap)
8. X server driver config
9. Kernel module lists (`/etc/mkinitcpio.conf`, `/etc/modules-load.d/`)
10. CPU microcode install
11. All-firmware install
12. Graphics driver install + modprobe blacklist
13. `/etc/fstab` generation
14. Networking config (NetworkManager, dhcpcd, or static IP)
15. User account creation and group assignment
16. Shell aliases and environment variables
17. Snapshot daemon boot registration
18. Bootloader kernel params and timeout

A rollback snapshot is saved before every apply. Rollback restores: hostname, timezone, service states, and symlinks. It does not restore packages (use `astral -R` manually) or file contents (use `snap restore`).

---

## File Snapshots

```bash
astral-env snap /home/iskandar/.zshrc
astral-env snap /home/iskandar/.config/hyprland
astral-env snap list
astral-env snap list /home/iskandar/.zshrc
astral-env snap restore snap-2026-03-23_14:32:00
astral-env snap restore snap-2026-03-23_14:32:00 --dest /tmp/zshrc.bak
astral-env snap prune --keep-last 5
astral-env snap prune --older-than 14d
```

Snapshots are content-addressed and deduplicated under `/astral-env/store/snap/`. Identical content is stored once regardless of how many times you snapshot it.

---

## Store & GC

```bash
astral-env store list
astral-env store size
astral-env gc --dry-run
astral-env gc
astral-env gc --max-age 7
```

The GC scans `/home` and `/root` for lockfiles. Any store entry referenced by a lockfile is kept. `store/snap/` is never touched by the package GC use `snap prune` for that.

---

## Daemon & Auto-snapshots

Configure auto-snapshots in `env.stars` under `$ENV.Snap`:

```
Snap: {
    on_interval      = "true"
    default_interval = "1" "H"

    path: {
        "/home/iskandar/.config"
        "/home/iskandar/.zshrc": {
            interval          = "autosave"
            autosave_debounce = "5" "S"
        };
    };
};
```

`autosave` uses inotify. The daemon waits for `autosave_debounce` seconds of quiet after the last change before snapshotting (default: 5s).

```bash
astral-env snapd start
astral-env snapd stop
astral-env snapd restart
astral-env snapd status
```

`snapd start` registers the daemon with the detected init system automatically no manual unit file writing needed.

---

## CLI Reference

```
astral-env <command> [options]

Environment Commands:
  init                    Scaffold astral-env.stars
  lock [--update [pkg]]   Generate/update lockfile
  build [--force]         Build environment from lockfile
  shell [--dir <d>]       Enter environment shell
  run <cmd...>            Run command in environment
  status                  Show installed vs missing

System Commands:
  system init             Create /etc/astral/env/env.stars
  system init-user <u>    Create per-user config
  system diff             Preview changes
  system apply            Apply changes (--dry-run, --yes, --user, --global-only)
  system rollback         Roll back (--list, --to <id>)
  system sync-index       Refresh repo indexes from all declared repos
  system check            Validate .stars files

Snapshot Commands:
  snap <path>             Snapshot a file or directory
  snap list [path]        List snapshots
  snap restore <id>       Restore snapshot (--dest <path>)
  snap prune              Prune (--keep-last N, --older-than Nd)

Store Commands:
  store list              List store entries
  store size              Total store size
  gc [--dry-run]          Collect unused entries (--max-age <days>)

Daemon Commands:
  snapd start             Start + register with init system
  snapd stop              Stop daemon
  snapd restart           Restart daemon
  snapd status            Daemon status

Global Options:
  -v, --verbose           Verbose output
  -q, --quiet             Quiet output
  -V, --version           Show version
  -h, --help              Show help
```

---

## Init System Support

| Init | Detected by | Service management | snapd registration |
|------|-------------|-------------------|-------------------|
| systemd | `/run/systemd/private` | `systemctl` | Unit file + `systemctl enable` |
| OpenRC | `/run/openrc/softlevel` | `rc-update` | `/etc/init.d/` + `rc-update add` |
| runit | `/run/runit` | `/var/service/` symlinks | `/etc/sv/` + symlink |
| s6 | `/run/s6` | bundle symlinks | `/etc/s6/sv/` service dir |
| dinit | `/run/dinit` | `dinitctl` | `/etc/dinit.d/` + boot.d symlink |
| SysVinit | `/etc/inittab` | `service(8)` + `chkconfig`/`update-rc.d` | `/etc/rc.local` entry |

---

## Troubleshooting

**`astral-env is not enabled`** add `astral-env = "enabled"` to `$AST.core` in `/etc/astral/astral.stars`.

**`astral-env-system is not enabled`** add `astral-env-system = "enabled"` to the same block.

**`zstd is required for snapshots`** run `astral -S zstd`.

**`No lockfile found`** run `astral-env lock` first.

**Package not found in any repo** run `astral-env system sync-index` to refresh indexes, then try again.

**Service management says `not supported`** your init system wasn't detected. Check `/run/` for the marker file. File an issue with `uname -a` and init name.

**Font not installing** the font tarball needs to exist at `<repo-base>/font/<family>/<family>[-nerd].tar.zst`. Check your repo layout.

**`system apply` stops at fstab** `/etc/fstab` generation is only done if `FileSystems.Disk` is declared. If something is wrong, check the generated file before rebooting.

---

## TODOs

- **`--prune` on `system apply`** flag is accepted but removal of packages not in the config is not implemented.
- **`EasterEgg.ErrorsCustomizer`** defined in spec, not implemented. Probably never will be.
- **Binary install PATH wiring for user packages** user packages installed to `/astral-env/users/<n>/` need their `/bin/` dirs added to that user's PATH. Currently the binaries are there but PATH isn't automatically updated.
- **`Packages.Repository` inheritance in user configs** user packages correctly query all repos declared in `env.stars`, but the `binaryPkg` value from `env.stars` is not inherited into user package resolution yet.
- **astral-env-snapd: s6 bundle wiring** the s6 service dir is created but the user must manually add it to their bundle. Automating this requires knowing the bundle path.

### Known issues

- `system rollback` restores hostname, timezone, services, and symlinks. It cannot restore packages or regular file contentsuse `snap restore` for files.
- The GC searches `/home` and `/root` for lockfiles. Projects stored elsewhere need a central registry at `/astral-env/registry` (not yet implemented) or their store entries may be collected.
- `astral-env shell` with zsh sets `ZDOTDIR` which can conflict with existing zsh configs that set their own `ZDOTDIR`.
- Font install tries multiple filename conventions (`<family>-nerd.tar.zst`, `<family>.tar.zst`) but if a repo uses a different naming scheme, the install silently fails.
- `process.cpp` drains stdout before stderrcould deadlock on very chatty subprocesses. Needs `select`/`poll` or threading to fix properly.

---

## FAQ

**Does astral-env replace astral?**
Mostly. astral-env handles index fetching, binary installs, font installs, and all system config. Astral is still called for source-build recipes via `astral -S`. You still need Astral installed.

**Why does astral-env still call astral for source builds?**
Astral's build pipeline (`build_from_recipe_enhanced`, sandboxing, checksum verification, transaction system) is mature and handles edge cases that would take a long time to reimplement. It makes more sense to delegate than to rewrite.

**Can I use my own GitHub repo as a package repo?**
Yes. Create a repo with the layout described in [Custom Repositories](#custom-repositories), generate an `astral.index` with `astral-sync gen-index`, and declare it in `env.stars`. Works with public GitHub and Codeberg repos out of the box.

**Can I have multiple repos?**
Yes. astral-env queries all declared repos in order. The first repo that has the package wins.

**How does `Includes` merging work?**
Recursively. Blocks are combined, scalars from the including file win on conflict. So if `env.stars` and `hw.stars` both define `Packages`, the package lists are merged. If they both define `hostName`, `env.stars`'s value is used.

**What's the difference between `Config.Dotfiles` and `Config.Symlinks`?**
Nothing meaningful both create symlinks from a destination to a source. `Dotfiles` expects the source to be inside your dotfiles store (`/astral-env/users/<n>/dotfiles/`). `Symlinks` is more freeform. Use whichever makes the intent clearer.

**Are my dotfiles safe?**
`system apply` backs up whatever exists at a dotfile destination before symlinking, and saves a rollback snapshot before touching anything. Keep backups anyway.

**Who maintains this?**
Izumi Sonoka. Two projects, one person. Send help.

---

## Credits

- **Created by**: Izumi Sonoka
- **Inspired by**: NixOS, GNU Stow, Ansible, and the desire to never type `systemctl enable` again

---

## License

GPL-3.0 - same as Astral.

---

**Last updated**: 2026-03-24 (GMT+8)  
**Version**: 2.0.0.0
