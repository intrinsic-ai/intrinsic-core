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

# Script to remove the NVIDIA GPU support installed by setup_nvidia.sh.
set -euo pipefail

# Global variables
WORK_DIR=""
CLUSTER_AVAILABLE=0
# Name of the installed Ubuntu prebuilt NVIDIA kernel modules metapackage, if
# any (see detect_prebuilt).
PREBUILT_PACKAGE=""

IOC_ASSUME_YES="${IOC_ASSUME_YES:-}"

function show_help() {
    cat << 'EOF'
Usage: sudo [IOC_ASSUME_YES=1] uninstall_nvidia.sh

Removes the NVIDIA GPU support installed by setup_nvidia.sh: the Kubernetes
device plugin, the container toolkit and the NVIDIA driver.

If Ubuntu's prebuilt NVIDIA kernel modules are installed (e.g. by
"ubuntu-drivers install"), the driver is kept for the generic kernel and only
the DKMS modules added by setup_nvidia.sh are removed. Otherwise, all NVIDIA
driver packages are removed, whichever driver series or tool installed them,
and the system falls back to the open-source nouveau driver.

This also runs "apt-get autoremove", which removes any other packages that are
no longer needed, including ones unrelated to the NVIDIA setup.

Reboot afterwards to finish the removal.

Environment variables:
  IOC_ASSUME_YES           Set to 1 to skip the confirmation prompt
  -h, --help               Show this help message
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

    if [[ -n "${PREBUILT_PACKAGE}" ]]; then
        echo "This will remove the NVIDIA container toolkit, the Kubernetes device plugin"
        echo "and the NVIDIA DKMS modules. The prebuilt NVIDIA driver is kept."
    else
        echo "This will remove the NVIDIA driver, the container toolkit and the Kubernetes"
        echo "device plugin."
    fi

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

function installed_packages() {
    dpkg-query -W -f='${db:Status-Abbrev} ${Package}\n' "$@" 2>/dev/null \
        | awk '$1 ~ /^.i/ {print $2}' || true
}

function detect_cluster() {
    if [[ -z "${KUBECONFIG:-}" ]]; then
        if [[ -f /etc/rancher/k3s/k3s.yaml ]]; then
            export KUBECONFIG="/etc/rancher/k3s/k3s.yaml"
        elif [[ -f "${HOME}/.kube/config" ]]; then
            export KUBECONFIG="${HOME}/.kube/config"
        fi
    fi

    if command -v kubectl >/dev/null 2>&1 && kubectl get nodes >/dev/null 2>&1; then
        CLUSTER_AVAILABLE=1
    else
        echo "Kubernetes cluster not reachable; skipping cluster cleanup."
    fi
}

function detect_prebuilt() {
    # setup_nvidia.sh only installs the DKMS driver. Ubuntu's prebuilt module
    # metapackages (linux-modules-nvidia-<series>-<flavour>, installed e.g. by
    # "ubuntu-drivers install") provide nvidia-prebuilt-kernel, as well as the
    # nvidia-dkms-<series> dependency of nvidia-driver-<series>. If one is
    # installed, the driver was set up independently of setup_nvidia.sh, so keep
    # it. Only one driver series can be installed at a time, so it always
    # matches the installed driver.
    PREBUILT_PACKAGE=$(dpkg-query -W -f='${db:Status-Abbrev}|${Package}|${Provides}\n' 'linux-modules-nvidia-*' 2>/dev/null \
        | awk -F'|' '$1 ~ /^.i/ && $3 ~ /nvidia-prebuilt-kernel/ {print $2; exit}' || true)

    if [[ -n "${PREBUILT_PACKAGE}" ]]; then
        echo "Found prebuilt NVIDIA kernel modules (${PREBUILT_PACKAGE}); keeping the NVIDIA driver."
    fi
}

function warn_gpu_users() {
    if ! command -v nvidia-smi >/dev/null 2>&1; then
        return
    fi

    # The prebuilt modules are kept, so if the running kernel uses them,
    # processes on the host keep their GPU access.
    if [[ -n "${PREBUILT_PACKAGE}" ]] && ! modinfo -n nvidia 2>/dev/null | grep -q '/updates/dkms/'; then
        return
    fi

    local gpu_processes
    gpu_processes=$(nvidia-smi --query-compute-apps=pid,process_name --format=csv,noheader 2>/dev/null || true)
    if [[ -n "${gpu_processes}" ]]; then
        echo "WARNING: The following processes are using the GPU and will lose access after reboot:"
        echo "${gpu_processes}" | sed 's/^/         /'
    fi
}

function remove_device_plugin() {
    if [[ "${CLUSTER_AVAILABLE}" -ne 1 ]] || ! command -v helm >/dev/null 2>&1; then
        return
    fi

    if helm status nvidia-device-plugin --namespace kube-system >/dev/null 2>&1; then
        echo "Removing NVIDIA device plugin..."
        run_silent helm uninstall nvidia-device-plugin --namespace kube-system
    fi
    helm repo remove nvdp >/dev/null 2>&1 || true
}

function restore_k3s_runtime() {
    local containerd_config="/var/lib/rancher/k3s/agent/etc/containerd/config-v3.toml.d/99-nvidia-default.toml"
    if [[ ! -f "${containerd_config}" ]]; then
        return
    fi

    # This must happen before the container toolkit is removed: otherwise
    # containerd keeps "nvidia" as its default runtime and no new container can
    # be started on the node.
    echo "Restoring default K3s container runtime..."
    rm -f "${containerd_config}"

    if systemctl is-active --quiet k3s; then
        run_silent systemctl restart k3s

        local i
        for i in {1..30}; do
            if kubectl get nodes >/dev/null 2>&1; then
                break
            fi
            sleep 1
        done
    fi
}

function installed_nvidia_packages() {
    if [[ -n "${PREBUILT_PACKAGE}" ]]; then
        # Only remove what setup_nvidia.sh added on top of the prebuilt driver.
        # The prebuilt metapackage keeps satisfying the dependency of
        # nvidia-driver-<series> on nvidia-dkms-<series>, so the driver stays.
        installed_packages 'nvidia-dkms-*' 'nvidia-container-toolkit*' 'libnvidia-container*'
        return
    fi

    # Purge the driver family explicitly rather than relying on autoremove.
    # On Ubuntu Desktop, packages like libavcodec62 declare "Suggests: libcuda1",
    # which is provided by libnvidia-compute-*. Because apt considers suggestions
    # important by default, autoremove would keep libnvidia-compute-*, which in
    # turn keeps nvidia-kernel-common-* (and therefore the nouveau blacklist).
    # Parts of the driver (e.g. nvidia-utils-*) may also be marked as manually
    # installed, and prebuilt modules (linux-modules-nvidia-*) from prior installs
    # would also pin nvidia-kernel-common-*.
    # nvidia-prime and nvidia-settings are intentionally not matched.
    installed_packages \
        'nvidia-driver-*' 'nvidia-dkms-*' 'nvidia-kernel-source-*' 'nvidia-kernel-common-*' \
        'nvidia-utils-*' 'nvidia-compute-utils-*' 'nvidia-firmware-*' 'xserver-xorg-video-nvidia-*' \
        'libnvidia-compute-*' 'libnvidia-gl-*' 'libnvidia-decode-*' 'libnvidia-encode-*' \
        'libnvidia-fbc1-*' 'libnvidia-extra-*' 'libnvidia-cfg1-*' 'libnvidia-common-*' \
        'nvidia-container-toolkit*' 'libnvidia-container*' \
        'linux-modules-nvidia-*' 'linux-objects-nvidia-*' 'linux-signatures-nvidia-*'
}

function purge_packages() {
    local packages
    packages=$(installed_nvidia_packages)
    if [[ -n "${packages}" ]]; then
        if [[ -n "${PREBUILT_PACKAGE}" ]]; then
            echo "Removing NVIDIA DKMS modules and container toolkit..."
        else
            echo "Removing NVIDIA driver and container toolkit..."
        fi
        # shellcheck disable=SC2086
        run_silent apt-get purge -y ${packages}
    fi

    # Removes remaining auto-installed dependencies (e.g. dkms, nvidia-prime,
    # nvidia-settings, screen-resolution-extra).
    echo "Removing unused packages..."
    run_silent apt-get autoremove --purge -y

    # The package scripts only rebuild the initramfs of the running kernel. The
    # other installed kernels would keep loading the embedded NVIDIA modules.
    if [[ -n "${packages}" ]] && command -v update-initramfs >/dev/null 2>&1; then
        echo "Updating initramfs for all kernels..."
        run_silent update-initramfs -u -k all
    fi
}

function remove_container_toolkit_repo() {
    if [[ -f /etc/apt/sources.list.d/nvidia-container-toolkit.list || -f /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg ]]; then
        echo "Removing NVIDIA container toolkit apt repository..."
        rm -f /etc/apt/sources.list.d/nvidia-container-toolkit.list
        rm -f /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg
        apt-get update >/dev/null 2>&1 || echo "WARNING: 'apt-get update' failed; run it manually."
    fi

    rm -f /etc/apt/apt.conf.d/51unattended-upgrades-nvidia
}

function restore_environment() {
    if [[ -f /etc/systemd/system.conf.d/10-ignore-preempt-rt.conf ]]; then
        rm -f /etc/systemd/system.conf.d/10-ignore-preempt-rt.conf
        run_silent systemctl daemon-reexec
    fi

    if grep -q "^IGNORE_PREEMPT_RT_PRESENCE=" /etc/environment 2>/dev/null; then
        sed -i '/^IGNORE_PREEMPT_RT_PRESENCE=/d' /etc/environment
    fi
}

function finish() {
    echo "NVIDIA removal complete!"

    if [[ -n "${PREBUILT_PACKAGE}" ]]; then
        echo ""
        echo "NOTE: The NVIDIA driver with prebuilt kernel modules (${PREBUILT_PACKAGE})"
        echo "      was kept. It is used when booting the generic kernel."
    else
        echo ""
        echo "To reinstall the NVIDIA driver:"
        echo "  - For standard Ubuntu (generic kernel):"
        echo "      Finish uninstalling the realtime kernel first (if applicable), reboot into"
        echo "      the generic kernel, and run:"
        echo "        sudo ubuntu-drivers install"
        echo "        sudo reboot"
        echo "  - For the realtime kernel (Intrinsic Core):"
        echo "      sudo ./setup_nvidia.sh"
        echo "      sudo reboot"
    fi

    echo ""
    echo "NOTE: Please reboot to finish the removal:"
    echo "      sudo reboot"
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

    detect_prebuilt
    warn_gpu_users
    confirm

    WORK_DIR=$(mktemp -d)
    trap 'rm -rf "${WORK_DIR}"' EXIT

    export DEBIAN_FRONTEND=noninteractive

    detect_cluster
    remove_device_plugin
    restore_k3s_runtime
    purge_packages
    remove_container_toolkit_repo
    restore_environment
    finish
}

main "$@"
