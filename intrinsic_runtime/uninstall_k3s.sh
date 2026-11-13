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

# Script to remove the k3s cluster set up by setup_k3s.sh.
set -euo pipefail

# Global variables
WORK_DIR=""

IOC_ASSUME_YES="${IOC_ASSUME_YES:-}"

function show_help() {
    cat << 'EOF'
Usage: [IOC_ASSUME_YES=1] uninstall_k3s.sh

Removes the k3s cluster and tools installed by setup_k3s.sh.

WARNING: This deletes the whole cluster, including all deployed solutions,
installed assets and container images.

Environment variables:
  IOC_ASSUME_YES      Set to 1 to skip the confirmation prompt
  -h, --help          Show this help message
EOF
}

function confirm() {
    if [[ -n "${IOC_ASSUME_YES}" ]]; then
        return
    fi

    echo "This will remove the k3s cluster, including all deployed solutions,"
    echo "installed assets and container images."

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

function remove_kubeconfig_entries() {
    local kube_config="${HOME}/.kube/config"
    if [[ ! -f "${kube_config}" ]]; then
        return
    fi

    # kubectl is a symlink to k3s, so this must happen before k3s-uninstall.sh.
    if ! command -v kubectl >/dev/null 2>&1; then
        echo "kubectl not found; leaving ${kube_config} untouched."
        return
    fi

    local current_context
    current_context=$(kubectl --kubeconfig="${kube_config}" config current-context 2>/dev/null || true)

    # setup_k3s.sh renames the entries to "k3s", but older installs still use
    # k3s's original name "default". Only touch entries that point at the
    # local k3s API server.
    local name server
    for name in k3s default; do
        server=$(kubectl --kubeconfig="${kube_config}" config view -o jsonpath="{.clusters[?(@.name==\"${name}\")].cluster.server}" 2>/dev/null || true)
        if [[ "${server}" != "https://127.0.0.1:6443" ]]; then
            continue
        fi

        echo "Removing '${name}' entries from ${kube_config}..."
        kubectl --kubeconfig="${kube_config}" config delete-context "${name}" >/dev/null 2>&1 || true
        kubectl --kubeconfig="${kube_config}" config delete-cluster "${name}" >/dev/null 2>&1 || true
        kubectl --kubeconfig="${kube_config}" config delete-user "${name}" >/dev/null 2>&1 || true
        if [[ "${current_context}" == "${name}" ]]; then
            kubectl --kubeconfig="${kube_config}" config unset current-context >/dev/null 2>&1 || true
        fi
    done

    # setup_k3s.sh created this file if it did not exist. Remove it again if
    # nothing else is left in it (backups of a pre-existing file are kept).
    # If kubectl fails, leave the file alone rather than treating the empty
    # output as "no contexts left".
    local remaining_contexts
    if remaining_contexts=$(kubectl --kubeconfig="${kube_config}" config get-contexts -o name 2>/dev/null) \
        && [[ -z "${remaining_contexts}" ]]; then
        echo "No other contexts left; removing ${kube_config}..."
        rm -f "${kube_config}"
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

function uninstall_k3s() {
    if [[ -x /usr/local/bin/k3s-uninstall.sh ]]; then
        echo "Uninstalling k3s..."
        run_silent sudo /usr/local/bin/k3s-uninstall.sh
    else
        echo "k3s-uninstall.sh not found; assuming k3s is already removed."
    fi

    # k3s-uninstall.sh removes /var/lib/rancher/k3s and /etc/rancher/k3s, but
    # clean up the directories in case a previous install only got half way.
    sudo rm -rf /var/lib/rancher/k3s /etc/rancher/k3s
}

function remove_sysctl() {
    if [[ -f /etc/sysctl.d/90-inotify.conf ]]; then
        echo "Removing inotify sysctl configuration..."
        sudo rm -f /etc/sysctl.d/90-inotify.conf
        echo "Note: The increased inotify limits stay in effect until reboot."
    fi
}

function remove_tools() {
    echo "Removing helm, k9s and istioctl..."
    sudo rm -f /usr/local/bin/helm /usr/local/bin/k9s /usr/local/bin/istioctl
}

function remove_containerd_group() {
    if ! getent group containerd >/dev/null; then
        return
    fi

    local target_user="${SUDO_USER:-${USER}}"
    if [[ -n "${target_user}" && "${target_user}" != "root" ]] && id -nG "${target_user}" | tr ' ' '\n' | grep -qx containerd; then
        echo "Removing ${target_user} from the containerd group..."
        sudo gpasswd -d "${target_user}" containerd >/dev/null
    fi

    # setup_realtime.sh grants this group access to the EtherCAT devices, so
    # keep it while that udev rule is still in place.
    if [[ -f /etc/udev/rules.d/99-EtherCAT.rules ]]; then
        echo "Keeping the containerd group; /etc/udev/rules.d/99-EtherCAT.rules (from setup_realtime.sh) still uses it."
        return
    fi

    # Only delete the group if nobody else uses it. setup_k3s.sh only created
    # it if it did not already exist.
    local members
    members=$(getent group containerd | cut -d: -f4)
    if [[ -z "${members}" ]]; then
        echo "Removing the containerd group..."
        # This fails if the group is still some user's primary group.
        sudo groupdel containerd || echo "WARNING: Failed to remove the containerd group."
    else
        echo "Keeping the containerd group; it still has members: ${members}"
    fi
}

function check_nvidia_support() {
    # setup_nvidia.sh installs the container toolkit and driver outside the
    # cluster, so they survive the removal of k3s.
    if command -v nvidia-container-runtime >/dev/null 2>&1; then
        echo ""
        echo "NOTE: The NVIDIA driver and container toolkit are still installed."
        echo "      Run uninstall_nvidia.sh to remove them as well."
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

    confirm

    WORK_DIR=$(mktemp -d)
    trap 'rm -rf "${WORK_DIR}"' EXIT

    remove_kubeconfig_entries
    uninstall_k3s
    remove_sysctl
    remove_tools
    remove_containerd_group

    echo "k3s removal complete!"
    echo "Note: Group membership changes take effect at your next login."

    check_nvidia_support
}

main "$@"
