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

# Utility functions used by various app integration tests, eg
# integration_smoke_test.sh.

set -euo pipefail

# Make our include guard clean against set -o nounset.
[[ -n "${INTRINSIC_TESTING_UTIL_INTEGRATION_TEST_UTIL_SH__:-}" ]] || declare -i INTRINSIC_TESTING_UTIL_INTEGRATION_TEST_UTIL_SH__=0

if (( INTRINSIC_TESTING_UTIL_INTEGRATION_TEST_UTIL_SH__++ == 0 )); then

if ! declare -f die >/dev/null; then
  function die {
    echo "$@" >&2
    exit 1
  }
fi

# These commonly-used global variables are set here to avoid hardcoding them
# in many tests. Some are set by the test executor:
#    intrinsic/testing/dockerized_test_executor/test_executor_lib.go
#    intrinsic/testing/vmpool_test_executor/vmpool_test_wrapper.sh
# and might have to be set for manual runs, eg:
#    bazel test \
#      --test_env=INTRINSIC_ORG=intrinsic@giza-workcells \
#      --test_env=CLUSTER_NAME=vmkube \
#      --test_env=CLUSTER_ADDR=localhost:17081 \
#      [...]
CLUSTER_NAME="${CLUSTER_NAME:-}"
readonly CLUSTER_NAME
export CLUSTER_NAME
CLUSTER_ADDR="${CLUSTER_ADDR:-}"
readonly CLUSTER_ADDR
export CLUSTER_ADDR
INTRINSIC_ORG="${INTRINSIC_ORG:-}"
readonly INTRINSIC_ORG
if [[ "${INTRINSIC_ORG}" != *@* ]]; then
  echo "INTRINSIC_ORG must be in the format of <org>@<project>."
  exit 1
fi
export INTRINSIC_ORG

# Tools
readonly KUBECTL="$(rlocation "kubebuilder_test_tools/kubectl")"
readonly NC="${NC:-/usr/bin/nc}"

#######################################
# Wrapper for kubectl that uses the $CLUSTER_NAME envvar. If this function is
# run in the background, killing the resulting PID might not stop the kubectl
# process. Please use kc_port_forward to enable port forwarding.
# Globals:
#   CLUSTER_NAME
# Arguments:
#   Any given arguments will be passed to kubectl
# Outputs:
#   Output of the kubectl command.
#######################################
function kc {
  # shellcheck disable=SC2153
  "${KUBECTL}" --context="${CLUSTER_NAME}" "$@"
}

#######################################
# Calls `exec kubectl` to set up port forwarding. Sets a trap to kill the
# portforwarding on SIGINT, SIGTERM, or EXIT.
# Globals:
#   CLUSTER_NAME
# Arguments:
#   $1: kubernetes namespace
#   $2: the type/name of the resource whose port is forwarded
#   $3: port assignment
# Outputs:
#   None.
#######################################
function kc_port_forward {
  exec "${KUBECTL}" \
    --context="${CLUSTER_NAME}" \
    --namespace="$1" \
    port-forward "$2" \
    "$3" \
    2>/dev/null &
  local port_forward_pid="$!"
  trap "kill ${port_forward_pid}" SIGINT SIGTERM EXIT
}

#######################################
# Run kubectl with retries, up to 5 minutes.
# Globals:
#   CLUSTER_NAME
# Arguments:
#   Any given arguments will be passed to kubectl
# Outputs:
#   Output of the kubectl command.
#######################################
function kc_retries {
  for i in $(seq 60); do
    kc "$@" && break || { echo "kubectl failed, retry #${i}" 1>&2; sleep 5; }
  done
}

#######################################
# Wrapper for inctl.
# Globals:
#   None
# Arguments:
#   Any given arguments will be passed to inctl
# Outputs:
#   Output of the inctl command.
#######################################
function inctl {
  inctl_bin="$(rlocation "intrinsic-core/intrinsic/tools/inctl/inctl_integration_tests")"
  # Re-direct GOOGLE_LOG_DIR to capture inctl logs as Sponge artifacts.
  GOOGLE_LOG_DIR="${TEST_UNDECLARED_OUTPUTS_DIR}" "${inctl_bin}" "$@"
}

#######################################
# Get current app namespace.
# Globals:
#   None
# Arguments:
#   None
# Outputs:
#   Namespace of currently running app
#######################################
function get_app_namespace {
  echo "$(kc get chartassignments -l exclusive=true -o jsonpath='{.items[*].spec.namespaceName}')"
}

#######################################
# Legacy function to reset a test machine before running tests.
# This is not required anymore - the vmpool manager is doing all, but the workcellmode
# reset if recycling a vm.
# Globals:
#   None
# Arguments:
#   None
#######################################
function delete_all_running_apps {
  log_phase "delete_all_running_apps"

  # Clean up after //intrinsic/kubernetes/workcellmode:mode_integration_test_cloud.
  kc patch -n app-intrinsic-base workcellmode workcellmode \
    --type=merge -p '{"spec": {"target": "develop"}}' || true

}

#######################################
# Cat logs for workloads identified by a list of app-labels.
# Globals:
#   CLUSTER_NAME, NAMESPACE
# Arguments:
#   $1: list of labels.
# Outputs:
#   Log messages.
#######################################
function print_app_logs {
  local app_labels="$1"
  local previous

  for app_label in ${app_labels}; do
    printf "\n\n"
    echo "#############################"
    echo "# Logs of ${app_label}"
    echo "#############################"
    printf "\n"

    # shellcheck disable=SC2153
    kc --namespace="${NAMESPACE}" logs -l "app=${app_label}" --all-containers --tail -1 \
      || echo "Cannot get ${app_label} logs"

    # Check for pre-crash logs. In particular, we've observed skills crashing
    # and restarting during test runs.
    if previous="$(kc --namespace="${NAMESPACE}" logs -l "app=${app_label}" \
        --all-containers --tail -1 --previous 2>/dev/null)" ; then
      echo
      echo "#############################"
      echo "# ${app_label} might have crashed during operation. Previous logs:"
      echo "#############################"
      echo
      echo "$previous"
    fi
  done
}

#######################################
# Cat logs for the workload identified by an app-label and container name.
# Globals:
#   CLUSTER_NAME, NAMESPACE
# Arguments:
#   $1: label.
#   $2: container name.
# Outputs:
#   Log messages.
#######################################
function print_app_container_logs {
  local app_label="$1"
  local container_name="$2"
  local previous

  printf "\n\n"
  echo "#############################"
  echo "# Logs of ${app_label} / container ${container_name}"
  echo "#############################"
  printf "\n"

  kc --namespace="${NAMESPACE}" logs -l "app=${app_label}" \
    -c "${container_name}" --tail -1 \
    || echo "Cannot get ${app_label} logs for container ${container_name}"

  # Check for pre-crash logs. In particular, we've observed skills crashing
  # and restarting during test runs.
  if previous="$(kc --namespace="${NAMESPACE}" logs -l "app=${app_label}" \
      -c "${container_name}" --tail -1 --previous 2>/dev/null)" ; then
    echo
    echo "#############################"
    echo "# ${app_label}/${container_name} might have crashed during operation. Previous logs:"
    echo "#############################"
    echo
    echo "$previous"
  fi
}

#######################################
# Globals:
#   CLUSTER_ADDR
# Arguments:
#   None
# Outputs:
#   URL of the frontend (e.g. http://localhost:17080/frontend/)
#######################################
function frontend_url {
  echo "http://${CLUSTER_ADDR}/frontend/"
}

#######################################
# Echo with a timestamp prefix.
# E.g.: "ts_echo foo" will output "2020-06-19 09:34:56+00:00: foo".
# Globals:
#   None
# Arguments:
#   $1, $2, ...: What to echo.
# Outputs:
#   The arguments with a timestamp prefix.
#######################################
function ts_echo {
  # Keep the date expression in sync with "trace_on()" below.
  echo "$(date --rfc-3339=seconds):" "$@"
}

#######################################
# Logs a message indicating the start of a phase.
# Globals:
#   None
# Arguments:
#   $1: Phase name.
# Outputs:
#   Phase message with timestamp.
#######################################
function log_phase {
  ts_echo ">>> Starting phase: $1"
}

#######################################
# Enable trace output to contain a date-time-stamp and file:line information
# followed by the command that will be executed.
# Globals:
#   None
# Arguments:
#   None
# Outputs:
#   None
#######################################
function trace_on {
  # Format for the xtrace lines.
  # Keep the date expression in sync with "ts_echo()" above.
  export 'PS4=+$(date --rfc-3339=seconds):$(basename "${BASH_SOURCE[0]}"):${LINENO}: '
  # Set timezone to match the timestamps in the container logs (grte-base-image
  # uses the default timezone)
  export TZ="Etc/UTC"
  # Print all commands for easier debugging of test failures
  set -x
}

#######################################
# Disable trace output.
# Globals:
#   None
# Arguments:
#   None
# Outputs:
#   None
#######################################
function trace_off {
  set +x
}


#######################################
# Checks if there are crashlooping pods in the app-namespace.
# Globals:
#   CLUSTER_NAME, NAMESPACE
# Arguments:
#   $1: additional hint to display in case of error
#######################################
function assert-no-crashlooping-pods {
  if [[ -z "${NAMESPACE:-}" ]] ; then
    die "NAMESPACE is not set"
  fi
  local pods
  pods=$(kc -n "${NAMESPACE}" get pods)
  if echo "${pods}" | grep -q CrashLoopBackOff; then
    local crashing
    crashing=$(echo "${pods}" | grep CrashLoopBackOff | cut -d' ' -f1 | tr '\n' ',' |  sed -e 's/,$/\n/' -e 's/,/, /g')
    die "crashlooping pods ${crashing}"
  fi
}

#######################################
# Waits until gzserver is ready.
# Globals:
#   None
# Arguments:
#   $1: Host name.
#   $2: Port number.
#   $3: timeout in seconds.
# Outputs:
#   None
#######################################
function wait-for-gzserver {
  local start_seconds
  local elapsed_seconds
  local now_seconds
  start_seconds=$(date +%s)
  elapsed_seconds=0
  ts_echo "Waiting for gzserver to become reachable"
  until gzserver_is_reachable "$1" "$2" || ((elapsed_seconds > $3)); do
    sleep 3
    now_seconds=$(date +%s)
    elapsed_seconds=$((now_seconds - start_seconds))
  done
}

#######################################
# Checks if gzserver is reachable
# Globals:
#   None
# Arguments:
#   $1: Host name.
#   $2: Port number.
# Outputs:
#   None
#######################################
function gzserver_is_reachable {
  # Because of the indirection introduced when port forwarding,
  # it's not enough to wait for localhost:11345 open. We need to wait for it
  # to report that Gazebo is running. `nc -w1 -W1` will terminate after 1 second
  # or after receiving one packet.
  "${NC}" -w1 -W1 "$1" "$2" | grep -q gazebo
}

function inctl_recordings_list() {
  inctl recordings list \
      --workcell "${CLUSTER_NAME}" \
      "$@"
}

function inctl_recordings_generate() {
  inctl recordings generate \
      "$@"
}

function app_start() {
  local app="$(rlocation "$1")"
  shift
  args=(
    --cluster="${CLUSTER_NAME}"
  )
  "${app}" \
      "${args[@]}" \
      "$@"
}

function inctl_app_stop() {
  args=(
    --cluster="${CLUSTER_NAME}"
  )
  inctl solution stop \
      "${args[@]}" \
      "$@"
}
function chart_start() {
  local chart="$(rlocation "$1")"
  shift
  "${chart}" \
      --context "${CLUSTER_NAME}" \
      "$@"
}

function inctl_chart_stop() {
  inctl chart stop \
      --context "${CLUSTER_NAME}" \
      "$@"
}

function inctl_solution_start() {
  inctl solution start \
      "$@"
}

function inctl_solution_version_duplicate() {
  inctl solution_version duplicate \
      "$@"
}

function inctl_solution_delete() {
  inctl solution delete \
      "$@"
}

function inctl_vm_lease() {

  inctl vm lease \
      "$@"
}

function inctl_vm_return() {

  inctl vm return \
      "$@"
}

function inctl_service_add() {
  inctl service add \
      "$@"
}

function inctl_asset_install() {
  inctl asset install \
      "$@"
}

#######################################
# Ensures that a pod is in running phase or fails.
# Globals:
# Arguments:
#   $1: Name of the pod
#   $2: Namespace
#   $3: Name of the cluster
# Outputs:
#   None
#######################################
function wait_for_pod_or_fail {
  local pod_name="$1"
  local namespace="$2"

  while true; do
    KUBECTL_OUT=$(kc_retries --namespace "${namespace}" get pod -o json "${pod_name}")
    if [[ "${KUBECTL_OUT}" == *"\"phase\": \"Running\""* ]] || [[ "${KUBECTL_OUT}" == *"\"phase\": \"Pending\""* ]]; then
      ts_echo "Test is still running (potentially pending) ..."
      sleep 1
    else
      break
    fi
  done
  if [[ "${KUBECTL_OUT}" == *"\"phase\": \"Succeeded\""* ]]; then
    ts_echo "Pass: Test has completed successfully."
  else
    kc_retries --namespace "${namespace}" logs "${pod_name}"
    die "Failure: Test did not complete successfully."
  fi
}

#######################################
# Ensures that a pod finishes before moving on.
# Globals:
# Arguments:
#   $1: Name of the pod
#   $2: Namespace
#   $3: Name of the cluster
# Outputs:
#   None
#######################################
function wait_for_pod {
  local pod_name="$1"
  local namespace="$2"

  while true; do
    KUBECTL_OUT=$(kc_retries --namespace "${namespace}" get pod -o json "${pod_name}")
    if [[ "${KUBECTL_OUT}" == *"\"phase\": \"Running\""* ]] || [[ "${KUBECTL_OUT}" == *"\"phase\": \"Pending\""* ]]; then
      ts_echo "Pod is still running (potentially pending) ..."
      sleep 1
    else
      break
    fi
  done
  if [[ "${KUBECTL_OUT}" == *"\"phase\": \"Succeeded\""* ]]; then
    ts_echo "Pod has completed successfully."
  else
    kc_retries --namespace "${namespace}" logs "${pod_name}"
    ts_echo "Pod did not complete successfully."
  fi
}

#######################################
# Logs in to the given user account.
#
# See also: export_auth_cookies()
#
# Globals:
#   INTRINSIC_ORG, INTRINSIC_USER
# Arguments:
#   $1: User account to log in as
#   $2: Lifetime of the API key in minutes, optional defaults to 20m
# Outputs:
#   None
#######################################
function inctl_auth_login {
  local user
  user="${1}"
  local lifetime_minutes=${2:-20}
  local lifetime_seconds
  lifetime_seconds=$((lifetime_minutes * 60))
  local INTRINSIC_GCP_PROJECT="${INTRINSIC_ORG#*@}"

  # LINT.IfChange(inctl_auth_login)
  tg_bin="$(rlocation "intrinsic-core/intrinsic/frontend/token_generator/tokengenerator-bin")"
  # generate api key with given lifetime
  "${tg_bin}" \
    --project-id="${INTRINSIC_GCP_PROJECT}" \
    --test-account="${user}" \
    --keep-valid-tokens=true \
    --duration="${lifetime_seconds}" \
    --terse | \
    inctl auth login

  # preserve user for export_cookie_vars() below, OTA test-accounts always use
  # @gmail.com right now
  echo "${user}@gmail.com" >~/.config/intrinsic/user
  # LINT.ThenChange(//intrinsic/testing/vmpool_test_executor/vmpool_test_wrapper.sh)
}

#######################################
# Exports cookie variables for the test environment.
#
# See also: inctl_auth_login()
#
# Globals:
#   INTRINSIC_ORG, ~/.config/intrinsic/*
# Arguments:
#   None
# Outputs:
#   exports ORG_ID_COOKIE, ONPREM_TOKEN_COOKIE and LOGIN_COOKIE
#######################################
function export_cookie_vars {
  # ${INTRINSIC_ORG} is in the "org@project" form
  local INTRINSIC_ORG_NAME="${INTRINSIC_ORG%@*}"
  local INTRINSIC_GCP_PROJECT="${INTRINSIC_ORG#*@}"
  ORG_ID_COOKIE="${INTRINSIC_ORG_NAME}"
  ONPREM_TOKEN_COOKIE=$(inctl auth print-access-token --org="${INTRINSIC_ORG}")
  LOGIN_COOKIE=""
  local user
  user=$(cat ~/.config/intrinsic/user) || user=""
  if [[ -n "${user}" ]]; then
    # gcloud secrets create intrinsicengtester-password --project intrinsic-integration-tests
    # echo -n 'XXX' | gcloud secrets versions add intrinsicengtester-password --project intrinsic-integration-tests --data-file=-
    local password password_key
    password_key=$(echo "${user}" | cut -d'@' -f1 | tr -d '.')
    qsm_bin="$(rlocation "intrinsic-core/intrinsic/production/query_secret_manager")"
    password=$("${qsm_bin}" --secret="projects/${INTRINSIC_GCP_PROJECT}/secrets/${password_key}-password/versions/1")
    if [[ -n "${password}" ]]; then
      LOGIN_COOKIE="${user}:${password}"
    fi
  fi
  export ORG_ID_COOKIE ONPREM_TOKEN_COOKIE LOGIN_COOKIE
}

fi  # include guard
