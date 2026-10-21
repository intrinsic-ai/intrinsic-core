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

# Generic main script for e2e tests of Intrinsic apps. Notebooks and scripts
# to-be-tested can be passed by flag and this script will create a test
# case for each one of them at runtime.
set -euo pipefail

source "$(rlocation "intrinsic-core/intrinsic/testing/util/integration_test_util.sh")"

readonly GRPC_TIMEOUT=60

NAMESPACE=""
TEST_START_TIME="$(date +%s)"
readonly TEST_START_TIME

# Passes if running the binary succeeded.
# Parameters:
#   $1: Path to the binary in the runfiles. E.g.,
#       "intrinsic/script_under_test".
#   $@: Additional parameters passed to the test script, optional.
function test_binary {
  local test_result

  TEST_CMD=(
    "$@"
  )

  if [[ -n "${TIMEOUT_MINUTES:-}" && "${TIMEOUT_MINUTES}" -gt 0 ]]; then
    ts_echo "Running test with timeout ${TIMEOUT_MINUTES}m"
    TEST_CMD=(timeout "${TIMEOUT_MINUTES}m" "${TEST_CMD[@]}")
  fi

  # Run inside TEST_TMPDIR so that, e.g., the binary can write files.
  (cd "${TEST_TMPDIR}" && \
    GRPC_CONNECTION_TIMEOUT="${GRPC_TIMEOUT}" \
      "${TEST_CMD[@]}")
  test_result=$?

  if [[ ${test_result} -ne 0 ]]
  then
    print_app_logs "executive skills-cpp"
  fi
  return ${test_result}
}

# Passes if running the notebook succeeded.
# Parameters:
#   $1: Path to the notebook in the runfiles. E.g.,
#       "intrinsic/notebook_under_test.ipynb".
#   $@: Additional parameters passed to the test_notebook script, optional.
#       Defaults to "".
function test_notebook {
  TEST_CMD=("$(rlocation "intrinsic-core/intrinsic/apps/testing/e2e/test_notebook")")
  NOTEBOOK_PATH="$1"
  shift 1
  TEST_CMD+=(
    --notebook_path="${NOTEBOOK_PATH}"
    --hostname="${HOSTNAME}"
    "$@"
  )

  test_binary "${TEST_CMD[@]}"
}

# Passes if running the behavior tree succeeded.
# Parameters:
#   $1: Path to the behavior tree in the runfiles. E.g.,
#       "intrinsic/behavior_tree_under_test.ipynb".
#   $@: Additional parameters passed to the test_behavior_tree script, optional.
#       Defaults to "".
function test_behavior_tree {
  TEST_CMD=("$(rlocation "intrinsic-core/intrinsic/apps/testing/e2e/test_behavior_tree")")
  BEHAVIOR_TREE_PATH="$1"
  shift 1
  TEST_CMD+=(
    --behavior_tree_path="${BEHAVIOR_TREE_PATH}"
    "$@"
  )

  test_binary "${TEST_CMD[@]}"
}

function wait_for_pod_ready {
  local pod_name="$1"
  local namespace="$2"
  ts_echo "Waiting for pod ${pod_name} in namespace ${namespace} to be Running and Ready..."
  for i in $(seq 60); do
    local phase
    phase=$(kc --namespace "${namespace}" get pod "${pod_name}" -o jsonpath='{.status.phase}' 2>/dev/null || true)
    if [[ "${phase}" == "Running" ]]; then
      local ready
      ready=$(kc --namespace "${namespace}" get pod "${pod_name}" -o jsonpath='{.status.containerStatuses[0].ready}' 2>/dev/null || true)
      if [[ "${ready}" == "true" ]]; then
        ts_echo "Pod ${pod_name} is Running and Ready."
        return 0
      fi
    fi
    sleep 2
  done
  die "Pod ${pod_name} in namespace ${namespace} failed to reach Running and Ready state."
}

function setup_all_tests() {
  if [[ "${E2E_DEBUG-false}" != "true" ]]; then
    ts_echo "Deleting all running workcells"
    delete_all_running_apps
    ts_echo "Deleted all running workcells"
  else
    echo "Not stopping running app because E2E_DEBUG=true was given."
  fi

  ts_echo "Starting app ${INTRINSIC_SOLUTION}"
  # We specify --registry and --skip_direct_upload to avoid adding significant
  # load onto the API relay.  In the future we may want to remove those flags in
  # order to load test it internally.
  args=(
    --cluster="${CLUSTER_NAME}"
    "--operation_mode=${OPERATION_MODE}"
    "--registry=gcr.io/${INTRINSIC_ORG#*@}"
    "--skip_direct_upload"
  )
  "${INTRINSIC_SOLUTION}" "${args[@]}"
  ts_echo "Started app ${INTRINSIC_SOLUTION}"

  NAMESPACE=$(get_app_namespace)

  if [[ "${RUN_AS_SERVICE}" == "true" ]]; then
    ts_echo "Installing and adding test runner service"
    local service_bundle_path="${SERVICE_BUNDLE}"
    if [[ -n "${service_bundle_path}" && ! -f "${service_bundle_path}" ]] && declare -f rlocation >/dev/null; then
      service_bundle_path="$(rlocation "${SERVICE_BUNDLE}")"
    fi
    local install_args=(
      --cluster="${CLUSTER_NAME}"
    )
    if [[ -n "${INTRINSIC_ORG:-}" ]]; then
      install_args+=(--org="${INTRINSIC_ORG}")
      if [[ "${INTRINSIC_ORG}" == *"@"* ]]; then
        local -r gcp_project="${INTRINSIC_ORG#*@}"
        install_args+=(
          --registry="gcr.io/${gcp_project}"
        )
      fi
    fi
    inctl_asset_install "${install_args[@]}" "${service_bundle_path}"
    local -r service_name_dashes="${SERVICE_NAME//_/-}"
    local service_add_args=(
      --cluster="${CLUSTER_NAME}"
    )
    if [[ -n "${INTRINSIC_ORG:-}" ]]; then
      service_add_args+=(--org="${INTRINSIC_ORG}")
    fi
    inctl_service_add "${SERVICE_ID}" "${service_add_args[@]}"
    # Wait for the service pod to be ready and running
    local -r pod_name="rs-${service_name_dashes}-0"
    wait_for_pod_ready "${pod_name}" "app-resources"

    # Set up port forwarding to localhost:5051
    ts_echo "Setting up port forwarding to ${pod_name} on port 5051"
    kc --namespace="app-resources" port-forward "pod/${pod_name}" "5051:9090" >/dev/null 2>&1 &
    # Give port forward a moment to start
    sleep 20
    ts_echo "Finished up setting up port forward."
  fi

  ts_echo "General test setup complete"
}

function teardown_all_tests() {
  ts_echo " Starting teardown of all tests"

  pkill -f "port-forward.*5051:9090" || true

  if [[ "${E2E_DEBUG:-false}" != "true" ]] && [[ "${RUN_AS_SERVICE:-false}" != "true" ]]; then
    inctl_app_stop
  else
    echo "Not stopping running app because E2E_DEBUG=true or RUN_AS_SERVICE=true was given."
  fi

  ts_echo " Finished teardown of all tests"
}

function test::setup_all_tests() {
  setup_all_tests "$@"
}

function test::teardown_all_tests() {
  teardown_all_tests "$@"
}
