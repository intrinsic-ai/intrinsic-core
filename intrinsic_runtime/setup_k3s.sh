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

# Script to set up a k3s cluster on Ubuntu for the IOC project.
set -euo pipefail

# Global variables
WORK_DIR=""

function write_istio_config() {
    local target_file="$1"
    cat << 'EOF' > "${target_file}"
apiVersion: install.istio.io/v1alpha1
kind: IstioOperator
spec:
  # The "default" profile includes the ingress gateway.
  profile: default

  values:
    global:
      logging:
        level: all:warn
      istioNamespace: app-ingress

    gateways:
      istio-ingressgateway:
        autoscaleEnabled: false
    pilot:
      autoscaleEnabled: false
      env:
        # Disable leader election to reduce disk I/O on small clusters without
        # multiple Istio replicas.
        ENABLE_LEADER_ELECTION: "false"

  meshConfig:
    # from default
    defaultConfig:
      proxyMetadata: {}
    enablePrometheusMerge: true

    # Set this to "/dev/stdout" to enable access logging.
    accessLogFile: ""
    # meshConfig.rootNamespace should match values.global.istioNamespace.
    rootNamespace: app-ingress
    accessLogFormat: |
      {"authority": "%REQ(:AUTHORITY)%", "start_time": "%START_TIME%", "bytes_received": "%BYTES_RECEIVED%", "bytes_sent": "%BYTES_SENT%", "downstream_local_address": "%DOWNSTREAM_LOCAL_ADDRESS%", "downstream_remote_address": "%DOWNSTREAM_REMOTE_ADDRESS%", "duration": "%DURATION%", "istio_policy_status": "%DYNAMIC_METADATA(istio.mixer:status)%", "method": "%REQ(:METHOD)%", "path": "%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)%", "protocol": "%PROTOCOL%", "request_id": "%REQ(X-REQUEST-ID)%", "requested_server_name": "%REQUESTED_SERVER_NAME%", "response_code": "%RESPONSE_CODE%", "response_flags": "%RESPONSE_FLAGS%", "route_name": "%ROUTE_NAME%", "start_time": "%START_TIME%", "upstream_cluster": "%UPSTREAM_CLUSTER%", "upstream_host": "%UPSTREAM_HOST%", "upstream_local_address": "%UPSTREAM_LOCAL_ADDRESS%", "upstream_service_time": "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)%", "upstream_transport_failure_reason": "%UPSTREAM_TRANSPORT_FAILURE_REASON%", "user_agent": "%REQ(USER-AGENT)%", "x_forwarded_for": "%REQ(X-FORWARDED-FOR)%", "x_icon_instance_name": "%REQ(x-icon-instance-name)%", "x_resource_instance_name": "%REQ(x-resource-instance-name)%"}

  components:
    base:
      enabled: true
    cni:
      enabled: false
    pilot:
      enabled: true

    ingressGateways:
      - name: istio-ingressgateway
        enabled: true
        k8s:
          service:
            type: LoadBalancer
            ports:
            # Expose standard HTTP port 80 for in-cluster services (istio-ingressgateway.app-ingress.svc.cluster.local:80).
            - port: 80
              targetPort: 8080
              name: http2
              protocol: TCP
            # Expose port 17080 for external access outside the cluster (localhost:17080 via ServiceLB).
            # Uses targetPort 17080 so the container has distinct containerPorts (8080 and 17080).
            - port: 17080
              targetPort: 17080
              name: http-external
              protocol: TCP
            # Additional port to allow connecting to the Zenoh router from the LAN.
            - port: 7447
              targetPort: 7447
              name: tcp
              protocol: TCP
EOF
}

function write_gateway_config() {
    local target_file="$1"
    cat << 'EOF' > "${target_file}"
apiVersion: networking.istio.io/v1
kind: Gateway
metadata:
  name: gateway
  namespace: app-ingress
spec:
  selector:
    app: istio-ingressgateway
  servers:
  - port:
      name: zenoh
      number: 7447
      protocol: TCP
    hosts:
    - "*"
  # In-cluster HTTP traffic on port 80
  - port:
      name: http
      number: 80
      protocol: HTTP
    hosts:
    - "*"
  # External HTTP traffic on port 17080
  - port:
      name: http-external
      number: 17080
      protocol: HTTP
    hosts:
    - "*"
EOF
}

function configure_kubeconfig() {
    local k3s_yaml="/etc/rancher/k3s/k3s.yaml"
    local kube_dir="${HOME}/.kube"
    local kube_config="${kube_dir}/config"

    sudo sed -i \
        -e 's/name: default/name: k3s/g' \
        -e 's/cluster: default/cluster: k3s/g' \
        -e 's/user: default/user: k3s/g' \
        -e 's/current-context: default/current-context: k3s/g' \
        "${k3s_yaml}"

    if getent group adm >/dev/null; then
        sudo chgrp adm "${k3s_yaml}"
        sudo chmod 640 "${k3s_yaml}"
    else
        sudo chown "${USER}" "${k3s_yaml}"
        chmod 600 "${k3s_yaml}"
    fi

    mkdir -p "${kube_dir}"

    if [ ! -f "${kube_config}" ]; then
        cp "${k3s_yaml}" "${kube_config}"
        chmod 600 "${kube_config}"
    else
        local backup_config="${kube_config}.bak.$(date +%s)"
        cp "${kube_config}" "${backup_config}"

        KUBECONFIG="${k3s_yaml}:${kube_config}" kubectl config view --flatten > "${kube_dir}/config.tmp"
        mv "${kube_dir}/config.tmp" "${kube_config}"
        chmod 600 "${kube_config}"
        kubectl config use-context k3s >/dev/null 2>&1 || true
    fi
}

function configure_containerd() {
    local containerd_dropin_dir="/var/lib/rancher/k3s/agent/etc/containerd/config-v3.toml.d"
    local containerd_config="${containerd_dropin_dir}/00-containerd-sock.toml"

    if ! getent group containerd >/dev/null; then
        sudo groupadd containerd
    fi

    local target_user="${SUDO_USER:-${USER}}"
    if [[ -n "${target_user}" && "${target_user}" != "root" ]]; then
        sudo usermod -aG containerd "${target_user}"
    fi

    local containerd_gid
    containerd_gid=$(getent group containerd | cut -d: -f3)

    sudo mkdir -p "${containerd_dropin_dir}"
    sudo tee "${containerd_config}" > /dev/null << EOF
[grpc]
  address = "/run/k3s/containerd/containerd.sock"
  uid = 0
  gid = ${containerd_gid}
EOF

    # If we're re-running, change the group as k3s won't do it until reboot.
    if [[ -S /run/k3s/containerd/containerd.sock ]]; then
        sudo chgrp containerd /run/k3s/containerd/containerd.sock
    fi
}

function configure_sysctl() {
    local sysctl_config="/etc/sysctl.d/90-inotify.conf"

    sudo mkdir -p /etc/sysctl.d
    sudo tee "${sysctl_config}" > /dev/null << 'EOF'
fs.inotify.max_user_watches = 524288
fs.inotify.max_user_instances = 1024
EOF

    run_silent sudo sysctl -p "${sysctl_config}"
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

function check_nvidia_support() {
    # setup_nvidia.sh stores its K3s configuration (a containerd drop-in and
    # the device plugin) in cluster state that k3s-uninstall.sh removes, so
    # recreating the cluster silently loses GPU support. The container toolkit
    # survives, so use it to detect that setup_nvidia.sh had been run.
    if ! command -v nvidia-container-runtime >/dev/null 2>&1; then
        return
    fi

    if ! helm status nvidia-device-plugin -n kube-system >/dev/null 2>&1; then
        echo ""
        echo "WARNING: The NVIDIA container toolkit is installed but K3s GPU support is not configured."
        echo "         Rerun setup_nvidia.sh to restore GPU support in the cluster."
    fi
}

function main() {
    local K3S_VERSION="v1.36.2+k3s1"
    local HELM_VERSION="v4.2.3"
    local K9S_VERSION="v0.51.0"
    local ISTIO_VERSION="1.29.6"
    local CHART_ASSIGNMENT_CONTROLLER_VERSION="0.1.0-cf378be"
    local PROMETHEUS_OPERATOR_CRDS_VERSION="30.0.1"

    WORK_DIR=$(mktemp -d)
    trap 'rm -rf "${WORK_DIR}"' EXIT

    local ISTIO_CONFIG_FILE="${WORK_DIR}/istio_config.yaml"
    local GATEWAY_CONFIG_FILE="${WORK_DIR}/gateway_config.yaml"

    configure_sysctl
    configure_containerd

    local k3s_args=("--disable=traefik" "--write-kubeconfig-mode=0640")
    if getent group adm >/dev/null; then
        k3s_args+=("--write-kubeconfig-group=adm")
    fi

    echo "Installing K3s version ${K3S_VERSION}..."
    run_silent sh -c "curl -sfL https://get.k3s.io | INSTALL_K3S_VERSION='${K3S_VERSION}' sh -s - ${k3s_args[*]}"

    configure_kubeconfig

    echo "Installing Helm version ${HELM_VERSION}..."
    run_silent sh -c "curl -fsSL https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-4 | bash -s -- -v '${HELM_VERSION}'"

    echo "Installing k9s version ${K9S_VERSION}..."
    run_silent sh -c "curl -fsSL 'https://github.com/derailed/k9s/releases/download/${K9S_VERSION}/k9s_Linux_amd64.tar.gz' | sudo tar -C /usr/local/bin -zx k9s"

    echo "Installing Istio CLI version ${ISTIO_VERSION}..."
    run_silent sh -c "curl -fsL 'https://storage.googleapis.com/istio-release/releases/${ISTIO_VERSION}/istioctl-${ISTIO_VERSION}-linux-amd64.tar.gz' | sudo tar -C /usr/local/bin -zx istioctl"

    write_istio_config "${ISTIO_CONFIG_FILE}"
    run_silent /usr/local/bin/istioctl install -f "${ISTIO_CONFIG_FILE}" --skip-confirmation

    write_gateway_config "${GATEWAY_CONFIG_FILE}"
    run_silent kubectl apply -f "${GATEWAY_CONFIG_FILE}"

    echo "Installing prometheus-operator-crds version ${PROMETHEUS_OPERATOR_CRDS_VERSION}..."
    run_silent helm upgrade --install prometheus-operator-crds \
      oci://ghcr.io/prometheus-community/charts/prometheus-operator-crds \
      --version "${PROMETHEUS_OPERATOR_CRDS_VERSION}"

    echo "Installing chart-assignment-controller version ${CHART_ASSIGNMENT_CONTROLLER_VERSION}..."
    run_silent helm upgrade --install chart-assignment-controller \
      oci://us-docker.pkg.dev/cloud-robotics-releases/charts/chart-assignment-controller \
      --version "${CHART_ASSIGNMENT_CONTROLLER_VERSION}" --set webhook.enabled=false

    echo "Setup complete!"

    check_nvidia_support
}

main "$@"
