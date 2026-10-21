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

set -euo pipefail

# Global variables
WORK_DIR=""

IOC_RT_CORES="${IOC_RT_CORES:-1}"
IOC_RT_NIC="${IOC_RT_NIC:-}"
IOC_ETHERCAT_NIC="${IOC_ETHERCAT_NIC:-}"

function show_help() {
    cat << 'EOF'
Usage: sudo [IOC_RT_CORES=<list>] [IOC_RT_NIC=<iface>] [IOC_ETHERCAT_NIC=<iface>] setup_realtime.sh

Sets up real-time tuning on Ubuntu for Intrinsic Open Core.

Environment variables:
  IOC_RT_CORES        Comma-separated list of isolated real-time CPU cores (default: 1)
  IOC_RT_NIC          Optional network interface for real-time traffic (pins IRQs to RT cores)
  IOC_ETHERCAT_NIC    Optional network interface for EtherCAT tuning (e.g. enp1s0)
  -h, --help          Show this help message
EOF
}

function check_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        if [[ -n "${BASH_SOURCE[0]:-}" && -f "${BASH_SOURCE[0]}" ]]; then
            # sudo-rs (default since Ubuntu 25.10) ignores -E, so list the variables.
            exec sudo --preserve-env=IOC_RT_CORES,IOC_RT_NIC,IOC_ETHERCAT_NIC bash "${BASH_SOURCE[0]}" "$@"
        else
            echo "Error: This script requires root privileges. Please run with sudo (e.g. curl ... | sudo -E bash)" >&2
            exit 1
        fi
    fi
}

function validate_nic() {
    local nic="$1"
    local var_name="$2"

    if [[ -n "${nic}" && ! -d "/sys/class/net/${nic}" ]]; then
        echo "Error: Network interface '${nic}' (${var_name}) does not exist." >&2
        exit 1
    fi
}

function validate_env() {
    # Restrict IOC_RT_CORES to a comma-separated list of numbers (no ranges or leading zeros)
    # to make it easier to invert when determining housekeeping cores.
    if [[ ! "${IOC_RT_CORES}" =~ ^(0|[1-9][0-9]*)(,(0|[1-9][0-9]*))*$ ]]; then
        echo "Error: IOC_RT_CORES must be a comma-separated list of CPU numbers without leading zeros (e.g. 1 or 1,2)" >&2
        exit 1
    fi

    local cpu
    for cpu in ${IOC_RT_CORES//,/ }; do
        if (( cpu == 0 )); then
            echo "Error: CPU 0 cannot be isolated. It is required for housekeeping." >&2
            exit 1
        fi
    done

    validate_nic "${IOC_RT_NIC}" "IOC_RT_NIC"

    if [[ -n "${IOC_ETHERCAT_NIC}" ]]; then
        local saved_ethercat_nic=""
        if [[ -f /etc/ioc/ethercat_nic ]]; then
            saved_ethercat_nic=$(tr -d '[:space:]' < /etc/ioc/ethercat_nic 2>/dev/null || true)
        fi

        if [[ "${IOC_ETHERCAT_NIC}" != "${saved_ethercat_nic}" ]]; then
            validate_nic "${IOC_ETHERCAT_NIC}" "IOC_ETHERCAT_NIC"
        fi
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

function install_dependencies() {
    echo "Installing real-time kernel and dependencies..."
    local packages=("ubuntu-realtime" "tuned")
    if [[ -n "${IOC_ETHERCAT_NIC}" ]]; then
        packages+=("ethtool")
    fi

    # uninstall_realtime.sh makes GRUB prefer the generic kernel until the
    # real-time kernel packages are purged. Remove that before apt-get install
    # (and configure_kernel_cmdline) runs update-grub, otherwise the real-time
    # kernel would never be booted.
    rm -f /etc/default/grub.d/zz-intrinsic-uninstall-realtime.cfg

    run_silent apt-get install -y "${packages[@]}"
}

function check_smt() {
    local smt_active="0"
    if [[ -f /sys/devices/system/cpu/smt/active ]]; then
        smt_active=$(cat /sys/devices/system/cpu/smt/active)
    fi

    if [[ "${smt_active}" == "1" ]]; then
        echo "Error: Hyper-Threading / SMT is currently enabled in BIOS." >&2
        echo "       You must disable Hyper-Threading in BIOS before setting up real-time:" >&2
        echo "       systemctl reboot --firmware-setup" >&2
        exit 1
    fi
}

function get_housekeeping_cpus() {
    local total_cpus
    total_cpus=$(nproc --all)

    local cpu
    for cpu in ${IOC_RT_CORES//,/ }; do
        if (( cpu >= total_cpus )); then
            echo "Error: Specified RT core ${cpu} exceeds available CPU cores (${total_cpus})." >&2
            exit 1
        fi
    done

    local hk_cpus
    hk_cpus=$(grep -vxFf <(tr ',' '\n' <<< "${IOC_RT_CORES}") <(seq 0 $((total_cpus - 1))) | paste -sd ',' || true)

    if [[ -z "${hk_cpus}" ]]; then
        echo "Error: All CPU cores (${total_cpus}) were allocated to real-time. At least one housekeeping core is required." >&2
        exit 1
    fi

    echo "${hk_cpus}"
}

function configure_systemd_affinity() {
    local hk_cpus
    hk_cpus=$(get_housekeeping_cpus)
    echo "Configuring systemd CPUAffinity to exclude RT cores..."

    mkdir -p /etc/systemd/system.conf.d
    cat << EOF > /etc/systemd/system.conf.d/10-cpu-affinity.conf
[Manager]
CPUAffinity=${hk_cpus}
EOF

    run_silent systemctl daemon-reload
}

function configure_kernel_cmdline() {
    local hk_cpus
    hk_cpus=$(get_housekeeping_cpus)
    local cfg_file="/etc/default/grub.d/99-intrinsic-realtime.cfg"
    echo "Tuning kernel for real-time execution (isolated cores: ${IOC_RT_CORES})..."

    # The kernel command line is deliberately not left to the TuneD bootloader
    # plugin. TuneD only keeps these parameters in /etc/tuned/bootcmdline while it
    # is running, and blanks that file when it stops ("rolling back all changes").
    # The next run of update-grub, e.g. from an automatic kernel upgrade, then
    # silently drops them and the following boot comes up without isolated cores.
    # A static drop-in is independent of any running daemon and survives kernel
    # updates.
    local params=(
        # Equivalent to the TuneD 'network-latency' profile.
        "skew_tick=1"
        "tsc=reliable"
        "rcupdate.rcu_normal_after_boot=1"
        # Equivalent to the TuneD 'realtime' profile (with isolate_managed_irq=Y).
        "isolcpus=managed_irq,domain,${IOC_RT_CORES}"
        "intel_pstate=disable"
        "nosoftlockup"
        # Read by the Intrinsic runtime to determine the real-time cores.
        "rcu_nocbs=${IOC_RT_CORES}"
        "irqaffinity=${hk_cpus}"
    )

    mkdir -p /etc/default/grub.d
    cat << EOF > "${cfg_file}"
# Managed by setup_realtime.sh. Do not edit.
GRUB_CMDLINE_LINUX_DEFAULT="\${GRUB_CMDLINE_LINUX_DEFAULT:+\$GRUB_CMDLINE_LINUX_DEFAULT }${params[*]}"
EOF

    run_silent update-grub
}

function configure_base_profile() {
    local profile_dir="/etc/tuned/profiles/intrinsic-realtime"
    echo "Tuning userspace for real-time execution (isolated cores: ${IOC_RT_CORES})..."

    mkdir -p "${profile_dir}"
    cat << EOF > "${profile_dir}/tuned.conf"
[main]
include=realtime

[variables]
isolate_managed_irq=Y
isolated_cores=${IOC_RT_CORES}
rt_nic=${IOC_RT_NIC}

[bootloader]
# The kernel command line is owned by /etc/default/grub.d/99-intrinsic-realtime.cfg
# (see configure_kernel_cmdline), so keep TuneD away from the bootloader. The
# cmdline fragments inherited from the 'realtime' and 'network-latency' profiles
# are blanked as well, otherwise TuneD would still write them to
# /etc/tuned/bootcmdline and they would end up duplicated in grub.cfg.
skip_grub_config=1
cmdline_realtime=
cmdline_network_latency=
EOF

    if [[ -n "${IOC_RT_NIC}" ]]; then
        echo "Configuring network interrupt affinity (realtime NIC: ${IOC_RT_NIC})..."
        cat << 'EOF' >> "${profile_dir}/tuned.conf"

[rt_nic_irq_affinity]
# The scheduler plugin moves all IRQs off the isolated cores whenever the profile
# is applied, so this needs to run after it (default priority 0).
type=script
priority=10
script=rt_nic_irq_affinity.sh
EOF
    fi

    # Installed even without IOC_RT_NIC, so that TuneD can still run it with
    # "stop" when switching away from a previous profile that used it.
    cat << 'EOF' > "${profile_dir}/rt_nic_irq_affinity.sh"
#!/bin/bash
# Managed by setup_realtime.sh. Do not edit.
#
# Pins the interrupts of the real-time NIC (TuneD variable rt_nic) to the
# isolated cores. Since Linux 6.19, IRQ threads are marked PF_NO_SETAFFINITY, so
# moving the threads directly (e.g. with tuna or taskset) fails. Instead, set
# the affinity of the interrupts, and the kernel moves their threads with them.
ACTION="$1"
NIC="${TUNED_rt_nic:-}"
RT_CORES="${TUNED_isolated_cores:-}"

if [[ -z "${NIC}" ]]; then
    exit 0
fi

# Only interrupts that the driver has requested, which happens when the
# interface is brought up, have an entry in /proc/irq.
IRQS=()
for msi_irq in "/sys/class/net/${NIC}/device/msi_irqs/"*; do
    irq="${msi_irq##*/}"
    if [[ -f "/proc/irq/${irq}/smp_affinity_list" ]]; then
        IRQS+=("${irq}")
    fi
done

case "${ACTION}" in
    start)
        if [[ ${#IRQS[@]} -eq 0 || -z "${RT_CORES}" ]]; then
            echo "No active interrupts found for ${NIC} (is it up?) or isolated_cores unset." >&2
            exit 1
        fi
        status=0
        for irq in "${IRQS[@]}"; do
            echo "${RT_CORES}" > "/proc/irq/${irq}/smp_affinity_list" || status=1
        done
        exit "${status}"
        ;;
    stop)
        # Return the interrupts to the default affinity (the housekeeping cores).
        default_affinity=$(< /proc/irq/default_smp_affinity)
        for irq in "${IRQS[@]}"; do
            echo "${default_affinity}" > "/proc/irq/${irq}/smp_affinity" 2> /dev/null || true
        done
        ;;
esac
EOF
    chmod +x "${profile_dir}/rt_nic_irq_affinity.sh"

    # Some tooling, documentation, and upstream TuneD defaults look in /etc/tuned/<name>
    # while Debian/Ubuntu packages configure profile_dirs to search /etc/tuned/profiles/<name>.
    # Symlink to /etc/tuned/<name> so both paths resolve.
    ln -sfn "${profile_dir}" "/etc/tuned/intrinsic-realtime"

    # power-profiles-daemon declares 'Conflicts=tuned.service', and GNOME/GDM
    # activates it over D-Bus a few seconds into every boot. systemd then stops
    # TuneD, which rolls back the entire profile (CPU governor, IRQ affinity,
    # sysctls, NIC tuning), leaving the machine untuned. Mask it before TuneD is
    # started so the profile stays active.
    if systemctl list-unit-files power-profiles-daemon.service --no-legend 2> /dev/null | grep -q .; then
        # Mask before stopping, so that a pending D-Bus request cannot restart it.
        run_silent systemctl mask power-profiles-daemon.service
        systemctl stop power-profiles-daemon.service 2> /dev/null || true
    fi
}

function configure_ethercat_profile() {
    local profile_dir="/etc/tuned/profiles/intrinsic-realtime-ethercat"
    echo "Creating EtherCAT TuneD profile 'intrinsic-realtime-ethercat' (NIC: ${IOC_ETHERCAT_NIC})..."

    mkdir -p /etc/ioc
    echo "${IOC_ETHERCAT_NIC}" > /etc/ioc/ethercat_nic

    mkdir -p "${profile_dir}"
    cat << EOF > "${profile_dir}/tuned.conf"
[main]
include=intrinsic-realtime

[variables]
ethercat_nic=${IOC_ETHERCAT_NIC}

[sysctl]
net.core.netdev_max_backlog = 10000

[script]
script=nic_tuning.sh
EOF

    cat << 'EOF' > "${profile_dir}/nic_tuning.sh"
#!/bin/bash
ACTION="$1"
NIC="${TUNED_ethercat_nic:-${ethercat_nic:-$(cat /etc/ioc/ethercat_nic 2>/dev/null || true)}}"

if [ "${ACTION}" = "start" ] && [ -n "${NIC}" ]; then
    ethtool --set-eee "${NIC}" eee off 2>/dev/null || true
    ethtool -K "${NIC}" tso off gso off gro off lro off rx off tx off 2>/dev/null || true
fi
EOF
    chmod +x "${profile_dir}/nic_tuning.sh"

    # Some tooling, documentation, and upstream TuneD defaults look in /etc/tuned/<name>
    # while Debian/Ubuntu packages configure profile_dirs to search /etc/tuned/profiles/<name>.
    # Symlink to /etc/tuned/<name> so both paths resolve.
    ln -sfn "${profile_dir}" "/etc/tuned/intrinsic-realtime-ethercat"

    cat << 'EOF' > /etc/udev/rules.d/99-EtherCAT.rules
KERNEL=="EtherCAT[0-9]*", MODE="0664", GROUP="containerd"
EOF

    if command -v udevadm >/dev/null 2>&1; then
        run_silent udevadm control --reload-rules
        run_silent udevadm trigger
    fi
}

function activate_profile() {
    local target_profile="intrinsic-realtime"
    if [[ -n "${IOC_ETHERCAT_NIC}" ]]; then
        target_profile="intrinsic-realtime-ethercat"
    fi

    run_silent systemctl enable --now tuned

    echo "Activating TuneD profile: ${target_profile}..."
    run_silent tuned-adm profile "${target_profile}"
}

function remove_tuna_service() {
    # Older versions of this script installed a service that used tuna to move the
    # NIC's IRQ threads, which has no effect since Linux 6.19. The interrupts are
    # now pinned by rt_nic_irq_affinity.sh in the TuneD profile instead.
    if [[ -f /etc/systemd/system/tuna-realtime.service ]]; then
        echo "Removing obsolete tuna-realtime service..."
        systemctl disable --now tuna-realtime.service > /dev/null 2>&1 || true
        rm -f /etc/systemd/system/tuna-realtime.service
        run_silent systemctl daemon-reload
    fi
}

function label_k3s_node() {
    if ! command -v kubectl >/dev/null 2>&1; then
        echo "Error: kubectl not found. Run setup_k3s.sh then rerun this script." >&2
        exit 1
    fi

    local node_name
    if ! node_name=$(kubectl get nodes -o jsonpath='{.items[0].metadata.name}' 2>/dev/null) || [[ -z "${node_name}" ]]; then
        echo "Error: Failed to connect to Kubernetes cluster or retrieve node name. Run setup_k3s.sh then rerun this script." >&2
        exit 1
    fi

    echo "Labeling node '${node_name}' with intrinsic.ai/node-role=rtpc..."
    run_silent kubectl label nodes "${node_name}" intrinsic.ai/node-role=rtpc --overwrite
}

function check_system_state() {
    if ! uname -v | grep -qi "PREEMPT_RT"; then
        echo ""
        echo "WARNING: System is not currently booted into a PREEMPT_RT kernel."
        echo "         Current kernel: $(uname -r)"
        echo "         Please reboot to boot into the real-time kernel:"
        echo "         sudo reboot"
    elif ! grep -q "isolcpus=" /proc/cmdline; then
        echo ""
        echo "NOTICE: TuneD real-time boot parameters have been configured in GRUB."
        echo "        Please reboot the system for kernel boot parameters to take effect:"
        echo "        sudo reboot"
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
    validate_env

    WORK_DIR=$(mktemp -d)
    trap 'rm -rf "${WORK_DIR}"' EXIT

    install_dependencies
    # It's important to run check_smt after install_dependencies (to avoid a
    # second reboot to install the RT kernel) but before we call
    # get_housekeeping_cpus (as the numbering of the CPUs is affected by
    # disabling SMT).
    check_smt
    configure_systemd_affinity
    configure_kernel_cmdline
    configure_base_profile

    if [[ -n "${IOC_ETHERCAT_NIC}" ]]; then
        configure_ethercat_profile
    fi

    remove_tuna_service
    activate_profile
    label_k3s_node

    echo "Real-time setup complete!"
    echo "Active profile: $(tuned-adm active)"

    check_system_state
}

main "$@"
