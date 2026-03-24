#pragma once

#include "system/system_conf.hpp"
#include <string>

namespace astral_sys {

// Write /etc/locale.conf and regenerate locales
void apply_locale(const I18nConfig& i18n);

// Write /etc/vconsole.conf (or equivalent for non-systemd)
void apply_console(const ConsoleConfig& console);

// Enable/configure X server (write /etc/X11/xorg.conf.d entries)
void apply_xserver(bool enable, const std::string& graphics_driver);

// Write kernel module lists to initrd config and /etc/modules-load.d/
void apply_kernel_modules(const KernelConfig& kernel);

// Install and enable CPU microcode (amd-ucode / intel-ucode)
void apply_microcode(const std::string& cpu);

// Enable firmware loading
void apply_firmware(bool enable_all);

// Write graphics driver config (/etc/modprobe.d/, xorg.conf.d)
void apply_graphics(const std::string& driver);

// Write /etc/fstab from declared filesystems
void apply_fstab(const std::vector<DiskEntry>& disks);

// Write network config (supports NetworkManager, dhcpcd, wpa_supplicant)
void apply_networking(const NetworkConfig& net);

// Create/modify user accounts
void apply_users(const std::vector<UserEntry>& users);

// Write shell aliases to user's shell config
void apply_aliases(const UserConfig& ucfg);

// Write environment variables to user's profile
void apply_vars(const UserConfig& ucfg);

// Write shell aliases to /etc/profile.d/ for system-wide or user shell rc
void apply_user_shell(const UserConfig& ucfg, const std::string& home);

// Start/register snapshot daemon for the given init system
void apply_snapd_boot(const std::string& init_system);

// Apply bootloader config (limine, grub)
void apply_bootloader(const KernelConfig& kernel);

} // namespace astral_sys
