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

NVIDIA_DRIVER_VERSION="${NVIDIA_DRIVER_VERSION:-595}"
NVIDIA_DEVICE_PLUGIN_VERSION="0.20.1"

function show_help() {
    cat << 'EOF'
Usage: sudo [NVIDIA_DRIVER_VERSION=<version>] setup_nvidia.sh

Sets up NVIDIA GPU support on Ubuntu for Intrinsic Open Core.

Environment variables:
  NVIDIA_DRIVER_VERSION    NVIDIA driver series version (default: 595)
  -h, --help               Show this help message
EOF
}

function check_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        if [[ -n "${BASH_SOURCE[0]:-}" && -f "${BASH_SOURCE[0]}" ]]; then
            exec sudo -E bash "${BASH_SOURCE[0]}" "$@"
        else
            echo "Error: This script requires root privileges. Please run with sudo (e.g. curl ... | sudo -E bash)" >&2
            exit 1
        fi
    fi
}

function validate_env() {
    if [[ ! "${NVIDIA_DRIVER_VERSION}" =~ ^[0-9]+$ ]]; then
        echo "Error: NVIDIA_DRIVER_VERSION must be a numeric version (e.g. 595)" >&2
        exit 1
    fi

    if ! command -v kubectl >/dev/null 2>&1; then
        echo "Error: kubectl not found. Run setup_k3s.sh then rerun this script." >&2
        exit 1
    fi

    if ! command -v helm >/dev/null 2>&1; then
        echo "Error: helm not found. Run setup_k3s.sh then rerun this script." >&2
        exit 1
    fi

    if [[ -z "${KUBECONFIG:-}" ]]; then
        if [[ -f /etc/rancher/k3s/k3s.yaml ]]; then
            export KUBECONFIG="/etc/rancher/k3s/k3s.yaml"
        elif [[ -f "${HOME}/.kube/config" ]]; then
            export KUBECONFIG="${HOME}/.kube/config"
        fi
    fi

    if ! kubectl get nodes >/dev/null 2>&1; then
        echo "Error: Failed to connect to Kubernetes cluster. Run setup_k3s.sh then rerun this script." >&2
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

function check_gpu() {
    local gpu_found=0
    if [[ -d /sys/bus/pci/devices ]]; then
        local vendor_file
        for vendor_file in /sys/bus/pci/devices/*/vendor; do
            if [[ -f "${vendor_file}" ]] && grep -qi "0x10de" "${vendor_file}"; then
                gpu_found=1
                break
            fi
        done
    fi

    if [[ "${gpu_found}" -eq 0 ]]; then
        echo "WARNING: No NVIDIA GPU detected on PCI bus." >&2
    fi
}

function configure_environment() {
    # Allow NVIDIA DKMS module to build against PREEMPT_RT kernels.
    export IGNORE_PREEMPT_RT_PRESENCE=1

    # Systemd services (e.g. unattended-upgrades) do not read /etc/environment,
    # so set DefaultEnvironment to cover background kernel updates.
    mkdir -p /etc/systemd/system.conf.d
    cat << 'EOF' > /etc/systemd/system.conf.d/10-ignore-preempt-rt.conf
[Manager]
DefaultEnvironment=IGNORE_PREEMPT_RT_PRESENCE=1
EOF
    run_silent systemctl daemon-reexec

    # Keep in /etc/environment for interactive login sessions.
    if ! grep -q "^IGNORE_PREEMPT_RT_PRESENCE=" /etc/environment 2>/dev/null; then
        echo "IGNORE_PREEMPT_RT_PRESENCE=1" >> /etc/environment
    fi
}

function configure_unattended_upgrades() {
    # An unattended upgrade of the NVIDIA packages replaces the user-space
    # libraries while the old kernel module stays loaded, breaking nvidia-smi
    # and GPU containers with "Driver/library version mismatch" until the next
    # reboot. Leave upgrading them to deliberate reruns of this script, which
    # reloads the driver (or asks for a reboot).
    mkdir -p /etc/apt/apt.conf.d
    cat << 'EOF' > /etc/apt/apt.conf.d/51unattended-upgrades-nvidia
// Managed by setup_nvidia.sh. Entries are Python regular expressions matched
// against the start of the package name.
Unattended-Upgrade::Package-Blacklist {
    "nvidia-";
    "libnvidia-";
    "linux-modules-nvidia-";
};
EOF
}

function configure_container_toolkit_repo() {
    mkdir -p /usr/share/keyrings
    run_silent sh -c "curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | gpg --dearmor --yes -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg"
    run_silent sh -c "curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' > /etc/apt/sources.list.d/nvidia-container-toolkit.list"
}

function install_dependencies() {
    echo "Installing NVIDIA driver and container toolkit..."
    configure_environment
    configure_unattended_upgrades
    configure_container_toolkit_repo

    local packages=(
        "nvidia-driver-${NVIDIA_DRIVER_VERSION}-open"
        "nvidia-dkms-${NVIDIA_DRIVER_VERSION}-open"
        "nvidia-container-toolkit"
    )

    run_silent apt-get update
    run_silent apt-get install -y "${packages[@]}"
}

function unload_nouveau() {
    if [[ ! -d /sys/module/nouveau ]]; then
        return
    fi

    # The NVIDIA driver cannot attach to a GPU that nouveau already claimed
    # (dmesg: "NVRM: GPU 0000:01:00.0 is already bound to nouveau"). The
    # driver packages blacklist nouveau, but that only takes effect on reboot.
    echo "Unloading the nouveau driver..."
    if ! modprobe -r nouveau 2>/dev/null; then
        echo "Error: The nouveau driver is using the GPU and could not be unloaded (it is probably driving the display)." >&2
        echo "       Reboot the system (nouveau is now blacklisted) and rerun this script." >&2
        exit 1
    fi
}

function load_kernel_modules() {
    unload_nouveau

    local installed_version
    if ! installed_version=$(modinfo -F version nvidia 2>/dev/null); then
        echo "Error: No NVIDIA kernel module is available for the running kernel ($(uname -r))." >&2
        echo "       The DKMS build probably failed; check /var/lib/dkms/nvidia/*/build/make.log." >&2
        exit 1
    fi

    # apt-get replaces the user-space driver libraries in place, but a
    # previously loaded kernel module stays active. Until the system is
    # rebooted, nvidia-smi (and therefore the device plugin) fails with
    # "Driver/library version mismatch".
    if [[ -f /sys/module/nvidia/version ]]; then
        local loaded_version
        loaded_version=$(cat /sys/module/nvidia/version)
        if [[ "${loaded_version}" != "${installed_version}" ]]; then
            echo "Error: NVIDIA driver ${loaded_version} is loaded, but version ${installed_version} was installed." >&2
            echo "       Reboot the system and rerun this script." >&2
            exit 1
        fi
    fi

    run_silent modprobe -a nvidia nvidia-uvm nvidia-modeset nvidia-drm
}

function write_nvdp_config() {
    local target_file="$1"
    cat << 'EOF' > "${target_file}"
affinity: null
config:
  default: "default"
  map:
    default: |-
      version: v1
      sharing:
        timeSlicing:
          resources:
            - name: nvidia.com/gpu
              replicas: 48
EOF
}

function configure_k3s() {
    echo "Configuring K3s GPU support..."

    local containerd_dropin_dir="/var/lib/rancher/k3s/agent/etc/containerd/config-v3.toml.d"
    mkdir -p "${containerd_dropin_dir}"
    cat << 'EOF' > "${containerd_dropin_dir}/99-nvidia-default.toml"
[plugins."io.containerd.cri.v1.runtime".containerd]
  default_runtime_name = "nvidia"
EOF

    run_silent systemctl restart k3s

    # Wait for the K3s API server to become ready after restart before running Helm.
    local i
    for i in {1..30}; do
        if kubectl get nodes >/dev/null 2>&1; then
            break
        fi
        sleep 1
    done

    local nvdp_values="${WORK_DIR}/nvdp_values.yaml"
    write_nvdp_config "${nvdp_values}"

    echo "Installing nvidia-device-plugin version ${NVIDIA_DEVICE_PLUGIN_VERSION}..."
    run_silent helm repo add nvdp https://nvidia.github.io/k8s-device-plugin
    run_silent helm repo update nvdp
    run_silent helm upgrade --install nvidia-device-plugin nvdp/nvidia-device-plugin \
        --namespace kube-system --version "${NVIDIA_DEVICE_PLUGIN_VERSION}" -f "${nvdp_values}"
    run_silent kubectl rollout status daemonset/nvidia-device-plugin -n kube-system --timeout=300s
}

function verify_gpu() {
    echo "Verifying GPU access..."
    local output
    if ! output=$(nvidia-smi 2>&1); then
        echo "Error: nvidia-smi failed to communicate with the NVIDIA driver:" >&2
        echo "${output}" >&2
        echo "       Check 'dmesg | grep -i NVRM' or reboot the system, then rerun this script." >&2
        exit 1
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

    check_gpu
    install_dependencies
    load_kernel_modules
    # Verify the driver works before touching K3s: if it doesn't, the device
    # plugin rollout below would only fail with an opaque timeout.
    verify_gpu
    configure_k3s

    echo "NVIDIA setup complete!"
    echo "GPU: $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader | head -n1)"
}

main "$@"
