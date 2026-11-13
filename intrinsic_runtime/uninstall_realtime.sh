#!/bin/bash

# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Script to undo the real-time tuning applied by setup_realtime.sh.
set -euo pipefail

# Global variables
WORK_DIR=""

# Written in phase 1 so that GRUB prefers the generic kernel on the next boot,
# removed again in phase 2 once the real-time kernel packages are gone. Named
# so that it sorts after the 99-realtime.cfg shipped by ubuntu-realtime.
GRUB_FLAVOUR_DROPIN="/etc/default/grub.d/zz-intrinsic-uninstall-realtime.cfg"

IOC_ASSUME_YES="${IOC_ASSUME_YES:-}"

function show_help() {
    cat << 'EOF'
Usage: sudo [IOC_ASSUME_YES=1] uninstall_realtime.sh

Removes the real-time tuning and kernel installed by setup_realtime.sh.

If the system is currently running the real-time kernel, this script removes
everything except the running kernel's packages and switches the default boot
entry to the generic kernel. Reboot and run the script again to remove the
remaining real-time kernel packages.

This also runs "apt-get autoremove", which removes any other packages that are
no longer needed, including ones unrelated to the real-time setup.

Environment variables:
  IOC_ASSUME_YES      Set to 1 to skip the confirmation prompt
  -h, --help          Show this help message
EOF
}

function check_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        if [[ -n "${BASH_SOURCE[0]:-}" && -f "${BASH_SOURCE[0]}" ]]; then
            # sudo-rs (default since Ubuntu 25.10) ignores -E, so list the variables.
            exec sudo --preserve-env=IOC_ASSUME_YES bash "${BASH_SOURCE[0]}" "$@"
        else
            echo "Error: This script requires root privileges. Please run with sudo (e.g. curl ... | sudo -E bash)" >&2
            exit 1
        fi
    fi
}

function confirm() {
    if [[ -n "${IOC_ASSUME_YES}" ]]; then
        return
    fi

    echo "This will remove the real-time kernel and the real-time tuning (TuneD,"
    echo "isolated CPU cores, NIC tuning) from this system."

    # Read from the terminal so the prompt also works with "curl ... | bash".
    # Opening /dev/tty fails without a controlling terminal (e.g. non-interactive
    # ssh), regardless of what "test -r /dev/tty" says.
    local answer
    if ! read -r -p "Continue? [y/N] " answer 2>/dev/null < /dev/tty; then
        echo "" >&2
        echo "Error: Could not read confirmation from the terminal. Rerun with IOC_ASSUME_YES=1 to skip the prompt." >&2
        exit 1
    fi
    if [[ ! "${answer}" =~ ^[Yy]([Ee][Ss])?$ ]]; then
        echo "Aborted."
        exit 1
    fi
}

function run_silent() {
    if [[ "${EUID}" -ne 0 ]]; then
        sudo -v
    fi

    local log_file
    log_file=$(mktemp "${WORK_DIR}/cmd_XXXXXX.log")

    trap 'echo ""; echo "Command interrupted: $*"; echo "Logs:"; cat "${log_file}"; exit 130' INT TERM

    if ! "$@" < /dev/null > "${log_file}" 2>&1; then
        trap - INT TERM
        echo "Command failed: $*"
        echo "Logs:"
        cat "${log_file}"
        exit 1
    fi
    trap - INT TERM
}

function is_installed() {
    dpkg-query -W -f='${db:Status-Abbrev}' "$1" 2>/dev/null | grep -q '^.i'
}

function running_rt_kernel() {
    [[ "$(uname -r)" == *realtime* || "$(uname -v)" == *PREEMPT_RT* ]]
}

function unlabel_k3s_node() {
    if ! command -v kubectl >/dev/null 2>&1; then
        return
    fi

    local node_name
    if ! node_name=$(kubectl get nodes -o jsonpath='{.items[0].metadata.name}' 2>/dev/null) || [[ -z "${node_name}" ]]; then
        echo "Kubernetes cluster not reachable; skipping node label removal."
        return
    fi

    echo "Removing intrinsic.ai/node-role label from node '${node_name}'..."
    run_silent kubectl label nodes "${node_name}" intrinsic.ai/node-role-
}

function remove_tuna_service() {
    # Installed by older versions of setup_realtime.sh.
    if [[ -f /etc/systemd/system/tuna-realtime.service ]]; then
        echo "Removing tuna-realtime service..."
        systemctl disable --now tuna-realtime.service >/dev/null 2>&1 || true
        rm -f /etc/systemd/system/tuna-realtime.service
        run_silent systemctl daemon-reload
    fi
}

function deactivate_tuned() {
    if ! command -v tuned-adm >/dev/null 2>&1; then
        return
    fi

    # Turning the profile off while TuneD is still installed makes it roll back
    # its runtime changes (CPU governor, IRQ affinity, sysctls, NIC tuning) and,
    # on setups created before 99-intrinsic-realtime.cfg, clear its bootcmdline.
    echo "Deactivating TuneD profile..."
    systemctl start tuned >/dev/null 2>&1 || true
    tuned-adm off >/dev/null 2>&1 || echo "WARNING: 'tuned-adm off' failed; cleaning up GRUB manually."
    systemctl disable --now tuned >/dev/null 2>&1 || true
}

function remove_tuned_profiles() {
    echo "Removing TuneD profiles..."
    # /etc/tuned/<name> is normally a symlink, but older setups may have left a
    # directory there. rm -rf handles both without following symlinks.
    rm -rf /etc/tuned/profiles/intrinsic-realtime /etc/tuned/profiles/intrinsic-realtime-ethercat
    rm -rf /etc/tuned/intrinsic-realtime /etc/tuned/intrinsic-realtime-ethercat

    if [[ -f /etc/udev/rules.d/99-EtherCAT.rules ]]; then
        echo "Removing EtherCAT udev rule..."
        rm -f /etc/udev/rules.d/99-EtherCAT.rules
        if command -v udevadm >/dev/null 2>&1; then
            run_silent udevadm control --reload-rules
        fi
    fi

    rm -f /etc/ioc/ethercat_nic
    rmdir --ignore-fail-on-non-empty /etc/ioc 2>/dev/null || true
}

function remove_kernel_cmdline() {
    if [[ -f /etc/default/grub.d/99-intrinsic-realtime.cfg ]]; then
        echo "Removing real-time kernel command line configuration..."
        rm -f /etc/default/grub.d/99-intrinsic-realtime.cfg
    fi
}

function remove_systemd_affinity() {
    if [[ -f /etc/systemd/system.conf.d/10-cpu-affinity.conf ]]; then
        echo "Removing systemd CPUAffinity configuration..."
        rm -f /etc/systemd/system.conf.d/10-cpu-affinity.conf
        run_silent systemctl daemon-reload
    fi
}

function purge_tuned() {
    local packages=()
    local package
    # tuna was installed by older versions of setup_realtime.sh.
    for package in tuned tuna; do
        if is_installed "${package}"; then
            packages+=("${package}")
        fi
    done

    if [[ ${#packages[@]} -gt 0 ]]; then
        echo "Removing ${packages[*]}..."
        run_silent apt-get purge -y "${packages[@]}"
    fi

    # Generated files that purging the package leaves behind.
    rm -f /etc/tuned/bootcmdline /etc/tuned/active_profile /etc/tuned/profile_mode
    rmdir --ignore-fail-on-non-empty /etc/tuned/profiles /etc/tuned 2>/dev/null || true

    # TuneD's bootloader plugin appends these to /etc/default/grub and does not
    # remove them again.
    if grep -q 'tuned_params\|tuned_initrd' /etc/default/grub 2>/dev/null; then
        sed -i -e '/\$tuned_params/d' -e '/\$tuned_initrd/d' /etc/default/grub
    fi
}

function restore_power_profiles_daemon() {
    if [[ "$(systemctl is-enabled power-profiles-daemon.service 2>/dev/null || true)" == "masked" ]]; then
        echo "Restoring power-profiles-daemon service..."
        run_silent systemctl unmask power-profiles-daemon.service
        if systemctl list-unit-files power-profiles-daemon.service --no-legend 2>/dev/null | grep -q .; then
            systemctl start power-profiles-daemon.service 2>/dev/null || true
        fi
    fi
}

function installed_rt_kernel_packages() {
    dpkg-query -W -f='${db:Status-Abbrev} ${Package}\n' \
        'ubuntu-realtime' 'linux-realtime*' 'linux-*-realtime*' 2>/dev/null \
        | awk '$1 ~ /^.i/ {print $2}' || true
}

function ensure_generic_kernel() {
    if dpkg-query -W -f='${db:Status-Abbrev} ${Package}\n' 'linux-image-*-generic*' 2>/dev/null | grep -q '^.i'; then
        return
    fi

    echo "No generic kernel installed; installing linux-generic..."
    run_silent apt-get install -y linux-generic
}

function remove_rt_kernel() {
    local packages
    packages=$(installed_rt_kernel_packages)
    if [[ -z "${packages}" ]]; then
        rm -f "${GRUB_FLAVOUR_DROPIN}"
        return
    fi

    ensure_generic_kernel

    local running_version
    running_version=$(uname -r)

    local purge_now=()
    local keep_for_now=()
    local package
    for package in ${packages}; do
        if running_rt_kernel && [[ "${package}" == *"${running_version}"* ]]; then
            keep_for_now+=("${package}")
        else
            purge_now+=("${package}")
        fi
    done

    if [[ ${#purge_now[@]} -gt 0 ]]; then
        echo "Removing real-time kernel packages: ${purge_now[*]}"
        run_silent apt-get purge -y "${purge_now[@]}"
    fi

    if [[ ${#keep_for_now[@]} -gt 0 ]]; then
        echo "Configuring GRUB to boot the generic kernel by default..."
        mkdir -p /etc/default/grub.d
        cat << 'EOF' > "${GRUB_FLAVOUR_DROPIN}"
# Written by uninstall_realtime.sh; removed once the real-time kernel is purged.
GRUB_FLAVOUR_ORDER="generic"
EOF
    else
        rm -f "${GRUB_FLAVOUR_DROPIN}"
    fi
}

function finish() {
    echo "Removing unused packages..."
    run_silent apt-get autoremove --purge -y

    echo "Updating GRUB configuration..."
    run_silent update-grub

    echo "Real-time removal complete!"

    if is_installed gnome-shell && ! is_installed power-profiles-daemon; then
        echo ""
        echo "NOTE: power-profiles-daemon is not installed. If it was removed when tuned"
        echo "      was installed, restore it with: sudo apt install power-profiles-daemon"
    fi

    if running_rt_kernel; then
        echo ""
        echo "NOTE: The system is still running the real-time kernel ($(uname -r))."
        echo "      Its packages cannot be removed while it is running. Reboot into"
        echo "      the generic kernel, then run this script again:"
        echo "      sudo reboot"
    elif grep -q "isolcpus=" /proc/cmdline; then
        echo ""
        echo "NOTE: Please reboot the system to clear the real-time kernel boot parameters:"
        echo "      sudo reboot"
    fi
}

function main() {
    if [[ $# -gt 0 ]]; then
        if [[ "$1" == "-h" || "$1" == "--help" ]]; then
            show_help
            exit 0
        fi
        echo "Error: Unexpected arguments: $*" >&2
        show_help >&2
        exit 1
    fi

    check_root "$@"
    confirm

    WORK_DIR=$(mktemp -d)
    trap 'rm -rf "${WORK_DIR}"' EXIT

    export DEBIAN_FRONTEND=noninteractive

    unlabel_k3s_node
    remove_tuna_service
    deactivate_tuned
    remove_tuned_profiles
    remove_kernel_cmdline
    remove_systemd_affinity
    purge_tuned
    restore_power_profiles_daemon
    remove_rt_kernel
    finish
}

main "$@"
