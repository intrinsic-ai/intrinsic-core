// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package environments provides utilities and helpers for working with the various environments.
package environments

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"strings"
)

// LINT.IfChange(environments_constants)
const (
	// Prod is the production environment.
	Prod = "prod"
	// Staging is the staging environment.
	Staging = "staging"
	// Dev is the development environment.
	Dev = "dev"

	// AccountsProjectDev is the project for the accounts service in dev.
	AccountsProjectDev = "intrinsic-accounts-dev"
	// AccountsProjectStaging is the project for the accounts service in staging.
	AccountsProjectStaging = "intrinsic-accounts-staging"
	// AccountsProjectProd is the project for the accounts service in prod.
	AccountsProjectProd = "intrinsic-accounts-prod"

	// AccountsDomainDev is the domain for the accounts service in dev.
	AccountsDomainDev = "accounts-dev.intrinsic.ai"
	// AccountsDomainStaging is the domain for the accounts service in staging.
	AccountsDomainStaging = "accounts-qa.intrinsic.ai"
	// AccountsDomainProd is the domain for the accounts service in prod.
	AccountsDomainProd = "accounts.intrinsic.ai"

	// PortalProjectDev is the project for the portal service in dev.
	PortalProjectDev = "intrinsic-portal-dev"
	// PortalProjectStaging is the project for the portal service in staging.
	PortalProjectStaging = "intrinsic-portal-staging"
	// PortalProjectProd is the project for the portal service in prod.
	PortalProjectProd = "intrinsic-portal-prod"

	// PortalDomainDev is the domain for the portal service in dev.
	PortalDomainDev = "flowstate-dev.intrinsic.ai"
	// PortalDomainStaging is the domain for the portal service in staging.
	PortalDomainStaging = "flowstate-qa.intrinsic.ai"
	// PortalDomainProd is the domain for the portal service in prod.
	PortalDomainProd = "flowstate.intrinsic.ai"

	// EquipmentDomainDev is the parent domain for per-cluster UIs in dev.
	EquipmentDomainDev = "dev.intrinsic.equipment"
	// EquipmentDomainStaging is the parent domain for per-cluster UIs in staging.
	EquipmentDomainStaging = "qa.intrinsic.equipment"
	// EquipmentDomainProd is the parent domain for per-cluster UIs in prod.
	EquipmentDomainProd = "intrinsic.equipment"

	// AssetsProjectDev is the project for the asset service in dev.
	AssetsProjectDev = "intrinsic-assets-dev"
	// AssetsProjectStaging is the project for the asset service in staging.
	AssetsProjectStaging = "intrinsic-assets-staging"
	// AssetsProjectProd is the project for the asset service in prod.
	AssetsProjectProd = "intrinsic-assets-prod"

	// AssetsDomainDev is the domain for the asset service in dev.
	AssetsDomainDev = "assets-dev.intrinsic.ai"
	// AssetsDomainStaging is the domain for the asset service in staging.
	AssetsDomainStaging = "assets-qa.intrinsic.ai"
	// AssetsDomainProd is the domain for the asset service in prod.
	AssetsDomainProd = "assets.intrinsic.ai"

	// DsmProjectDev is the project for the dsm service in dev.
	DsmProjectDev = "intrinsic-dsm-dev"
	// DsmProjectStaging is the project for the dsm service in staging.
	DsmProjectStaging = "intrinsic-dsm-staging"
	// DsmProjectProd is the project for the dsm service in prod.
	DsmProjectProd = "intrinsic-dsm-prod"

	// ArtifactsProjectProd is the project for artifacts in prod.
	ArtifactsProjectProd = "intrinsic-artifacts-prod"
	// ArtifactsProjectDev is the project for artifacts in dev.
	ArtifactsProjectDev = "intrinsic-artifacts-dev"

	// OpsProjectProd is the project for the ops cluster in prod.
	OpsProjectProd = "intrinsic-ops"

	devProject     = "b7219186c3255926d0c158c14b3e0363d6b386115d4c3f1d8e0c9723369ea3b4"
	stagingProject = "bb46d3dc2d207a46a66397e36698c40b66d3c0c364cd3fd2d196f60f4b1d9fd9"
)

// All is the list of all environments.
var All = []string{Prod, Staging, Dev}

// LINT.ThenChange(//intrinsic/config/environments.py:environments_constants)
const (
	Prometheus           = "prometheus"
	Grafana              = "grafana"
	Alertmanager         = "alertmanager"
	ComponentsPlayground = "components-playground"
	// VMGrafana is the Grafana instance bundled with the VictoriaMetrics migration chart, distinct from the existing Prometheus-backed Grafana so both can be reachable during the parallel-deploy transition period. See go/intrinsic-metrics-backend-migration.
	VMGrafana            = "vm-grafana"
	Workflows            = "workflows"
	VictoriaMetricsCloud = "victoriametrics/cloud"
	VictoriaMetricsRobot = "victoriametrics/robots"
)

var CloudPortalInternalObservabilityServices = []string{Prometheus, Grafana, Alertmanager, VMGrafana, Workflows, VictoriaMetricsCloud, VictoriaMetricsRobot}
var CloudPortalInternalComputeServices = []string{ComponentsPlayground}

// FromDomain returns the environment for the given domain of portal, accounts or assets projects.
func FromDomain(domain string) (string, error) {
	switch domain {
	case PortalDomainProd, AccountsDomainProd, AssetsDomainProd:
		return Prod, nil
	case PortalDomainStaging, AccountsDomainStaging, AssetsDomainStaging:
		return Staging, nil
	case PortalDomainDev, AccountsDomainDev, AssetsDomainDev:
		return Dev, nil
	default:
		return "", fmt.Errorf("unknown domain %q", domain)
	}
}

// FromProject returns the environment for the given portal, accounts or assets project.
func FromProject(project string) (string, error) {
	switch project {
	case PortalProjectProd, AccountsProjectProd, AssetsProjectProd, DsmProjectProd, OpsProjectProd:
		return Prod, nil
	case PortalProjectStaging, AccountsProjectStaging, AssetsProjectStaging, DsmProjectStaging:
		return Staging, nil
	case PortalProjectDev, AccountsProjectDev, AssetsProjectDev, DsmProjectDev:
		return Dev, nil
	default:
		return "", fmt.Errorf("unknown project %q", project)
	}
}

// FromComputeProject returns the environment for the given compute project.
func FromComputeProject(project string) string {
	if strings.Contains(project, "-prod-") {
		return Prod
	}

	switch hashProjectName(project) {
	case devProject:
		return Dev
	case stagingProject:
		return Staging
	default:
		return Prod
	}
}

// IsGlobalProject returns true if the given project name matches a known global project
// (e.g., portal, accounts, assets, or ops project).
func IsGlobalProject(project string) bool {
	_, err := FromProject(project)
	return err == nil
}

// IsComputeProject returns true if the given project name matches a known compute project.
func IsComputeProject(project string) bool {
	switch hashProjectName(project) {
	case devProject, stagingProject:
		return true
	}
	return false
}

// IsKnownProject returns true if the given project name is a known global or compute project.
func IsKnownProject(project string) bool {
	return IsGlobalProject(project) || IsComputeProject(project)
}

// ArtifactsProjects returns the list of artifact projects to check for releases.
func ArtifactsProjects(project string) []string {
	projects := []string{ArtifactsProjectProd}
	if FromComputeProject(project) == Dev {
		projects = append(projects, ArtifactsProjectDev)
	}
	return projects
}

// PortalDomain returns the portal domain for the given environment.
func PortalDomain(env string) string {
	switch env {
	case Prod:
		return PortalDomainProd
	case Staging:
		return PortalDomainStaging
	case Dev:
		return PortalDomainDev
	default:
		return ""
	}
}

// EquipmentDomain returns the parent domain for per-cluster UIs
// (<cluster>.<domain>) for the given environment.
func EquipmentDomain(env string) string {
	switch env {
	case Prod:
		return EquipmentDomainProd
	case Staging:
		return EquipmentDomainStaging
	case Dev:
		return EquipmentDomainDev
	default:
		return ""
	}
}

// PortalProject returns the portal project for the given environment.
func PortalProject(env string) string {
	switch env {
	case Prod:
		return PortalProjectProd
	case Staging:
		return PortalProjectStaging
	case Dev:
		return PortalProjectDev
	default:
		return ""
	}
}

// AccountsDomain returns the accounts domain for the given environment.
func AccountsDomain(env string) string {
	switch env {
	case Prod:
		return AccountsDomainProd
	case Staging:
		return AccountsDomainStaging
	case Dev:
		return AccountsDomainDev
	default:
		return ""
	}
}

// AccountsProjectFromEnv returns the accounts project for the given environment.
func AccountsProjectFromEnv(env string) string {
	switch env {
	case Prod:
		return AccountsProjectProd
	case Staging:
		return AccountsProjectStaging
	case Dev:
		return AccountsProjectDev
	default:
		return ""
	}
}

// AccountsProjectFromProject returns the accounts project for the given project.
// Accepts both portal and compute projects.
func AccountsProjectFromProject(project string) string {
	return AccountsProjectFromEnv(FromAnyProject(project))
}

// AccountsDomainFromProject returns the accounts domain for the given project.
// Accepts both portal and compute projects.
func AccountsDomainFromProject(project string) string {
	return AccountsDomain(FromAnyProject(project))
}

// AssetsDomain returns the assets domain for the given environment.
func AssetsDomain(env string) string {
	switch env {
	case Prod:
		return AssetsDomainProd
	case Staging:
		return AssetsDomainStaging
	case Dev:
		return AssetsDomainDev
	default:
		return ""
	}
}

// AssetsProject returns the assets project for the given environment.
func AssetsProject(env string) string {
	switch env {
	case Prod:
		return AssetsProjectProd
	case Staging:
		return AssetsProjectStaging
	case Dev:
		return AssetsProjectDev
	default:
		return ""
	}
}

// Domain returns the domain name for a project. This covered both central projects as well as
// compute projects where it maps to the www.endpoints.* domain.
func Domain(project string) string {
	switch project {
	case PortalProjectProd:
		return PortalDomainProd
	case PortalProjectStaging:
		return PortalDomainStaging
	case PortalProjectDev:
		return PortalDomainDev
	case AccountsProjectProd:
		return AccountsDomainProd
	case AccountsProjectStaging:
		return AccountsDomainStaging
	case AccountsProjectDev:
		return AccountsDomainDev
	case AssetsProjectDev:
		return AssetsDomainDev
	case AssetsProjectStaging:
		return AssetsDomainStaging
	case AssetsProjectProd:
		return AssetsDomainProd
	default:
		return fmt.Sprintf("www.endpoints.%s.cloud.goog", project)
	}
}

const projectNameSalt = "2lJEUX97RpOzOvXQJhN+NRt0+KJ4z1KyPXtfe7"

func hashProjectName(name string) string {
	hasher := sha256.New()
	hasher.Write([]byte(projectNameSalt))
	hasher.Write([]byte(name))
	digest := hasher.Sum(nil)
	return hex.EncodeToString(digest)
}

// FromAnyProject returns the environment for the given portal, accounts, assets or compute project.
func FromAnyProject(project string) string {
	if env, err := FromProject(project); err == nil {
		return env
	}
	return FromComputeProject(project)
}
