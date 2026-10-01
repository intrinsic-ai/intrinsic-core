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

#
# lint.sh lints a workcell spec to find common errors in yaml/helm configuration.

set -euo pipefail

function helm {
  "$(rlocation "${HELM_BIN}")" "$@"
}

function kubeconform {
  "$(rlocation "${KUBECONFORM_BIN}")" "$@"
}

function yamllint {
  "$(rlocation "${YAMLLINT_BIN}")" "$@"
}

function check_route_secpolicy {
  "$(rlocation "${CHECK_ROUTE_SECPOLICY_BIN}")" "$@"
}

function lint_yaml {
  local -r chart=$1
  shift 1
  local -ra helm_args=( "$@" )

  # see go/intrinsic-k8s-playbook#crd-linting on updating these files
  local yamllint_config
  # shellcheck disable=SC2153 "${YAMLLINT_CONFIG}" is set from the environment.
  yamllint_config="$(rlocation "${YAMLLINT_CONFIG}")"

  tmpdir="$(mktemp -d)"
  trap "rm -rf ${tmpdir}" EXIT
  schema_tgz="$(rlocation "${KUBECONFORM_JSON_SCHEMA}")"

  schema_dir="${tmpdir}/schemas"
  mkdir "${schema_dir}"
  tar xfz "${schema_tgz}" -C "${schema_dir}"

  # Overlay third-party CRD schemas into master-standalone-strict.
  if [[ -n "${CRD_SCHEMAS_FILES:-}" ]]; then
    mkdir -p "${schema_dir}/master-standalone-strict"
    for schema_file in ${CRD_SCHEMAS_FILES}; do
      local resolved
      resolved="$(rlocation "${schema_file}")"
      if [[ -f "${resolved}" ]]; then
        cp -f "${resolved}" \
          "${schema_dir}/master-standalone-strict/" \
          || echo "WARN: Failed to copy ${resolved}" >&2
      fi
    done
  fi

  # Skip linting of legacy Ingress chart package if present.
  if [[ "$(basename "${chart}")" == "ingress-0.0.1.tgz" ]] ; then
    echo "INFO: Skipping linting of Ingress chart."
    return
  fi

  # The CustomResourceDefinition schema can't handle
  # http://intrinsic/frontend/cloud/devicemanager/templates/inversion-controller.yaml
  # but CRD authors know what they're doing.
  local -r skip_kinds=CustomResourceDefinition,WorkcellMode
  local chart_name
  chart_name=$(basename "${chart}" | head -c -5)
  readonly chart_name
  local -r templated_yaml="${TEST_UNDECLARED_OUTPUTS_DIR:-/tmp}/${chart_name}_${USER:-builder}_templated.yaml"

  helm template \
    "${helm_args[@]}" "${chart}" > "${templated_yaml}"

  # Catches errors such as spelling hostPath with a lowercase 'P'.
  kubeconform -strict -skip "${skip_kinds}" \
      -schema-location "${schema_dir}/master-standalone-strict/{{ .ResourceKind }}{{ .KindSuffix }}.json" \
      "${templated_yaml}" >&2
  # Catches errors such as specifying duplicate keys.
  yamllint -c "${yamllint_config}" "${templated_yaml}" >&2
  # Verifies HTTPRoute and GRPCRoute have matching SecurityPolicy targetRefs.
  check_route_secpolicy "${templated_yaml}" >&2
  echo "INFO: Successfully linted ${chart}"
}

function main {
  if [[ $# -lt 2 ]]; then
    echo >&2 "usage: lint.sh chart_path kubernetes_context (values_path)"
    exit 1
  fi

  local -r chart="$1"
  local -a helm_args=(
    "--kube-context=$2"
  )
  if [[ $# -eq 3 ]]; then
    helm_args+=("--values=$3")
  fi
  readonly helm_args

  # This is not supported as the passed values and file references likely should
  # only apply to one of the charts.
  if [[ $(echo "${chart}" | wc -l) -gt 1 ]]; then
    echo "INFO: Skipping lint.sh as more than one helm chart was found."
    exit 0
  fi

  lint_yaml "${chart}" "${helm_args[@]}"
  # TODO(rodrigoq): incorporate _check_image_usage from helm_chart.bzl
}

main "$@"
