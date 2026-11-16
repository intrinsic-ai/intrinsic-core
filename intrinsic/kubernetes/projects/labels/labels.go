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

// Package labels defines Intrinsic's GCP Cloud Labels taxonomy (http://go/intrinsic-cloud-labels)
// and derives canonical service FQDNs from project IDs and Cloud Labels without hardcoded project lists.
package labels

import "fmt"

// GCP Cloud Labels taxonomy constants matching http://go/intrinsic-cloud-labels
// (provisioned in github.com/intrinsic-ai/infrastructure/blob/main/terraform/gcp/org/labels.tf).
const (
	// LabelEnvironment is the GCP project label key for the project environment ("prod", "staging", "dev").
	LabelEnvironment = "environment"
	// LabelScope is the GCP project label key for the owning scope ("product", "devops", "corporate").
	LabelScope = "scope"
	// LabelComponent is the GCP project label key for the project component ("compute" or "global").
	LabelComponent = "component"
	// LabelSubcomponent is the GCP project label key for a global project's subcomponent.
	LabelSubcomponent = "subcomponent"
	// LabelReleaseChannel is the GCP project label key for the project release channel.
	LabelReleaseChannel = "release_channel"

	// Prod is the production environment label value.
	Prod = "prod"
	// Staging is the staging environment label value.
	Staging = "staging"
	// Dev is the development environment label value.
	Dev = "dev"

	// ComponentCompute identifies workcell/compute projects.
	ComponentCompute = "compute"
	// ComponentGlobal identifies central/global infrastructure projects.
	ComponentGlobal = "global"

	// SubcomponentAccounts identifies global accounts service projects.
	SubcomponentAccounts = "accounts"
	// SubcomponentArtifacts identifies global artifact registry projects.
	SubcomponentArtifacts = "artifacts"
	// SubcomponentAssets identifies global asset catalog projects.
	SubcomponentAssets = "assets"
	// SubcomponentDevhub identifies global developer hub projects.
	SubcomponentDevhub = "devhub"
	// SubcomponentDSM identifies global dedicated solution management projects.
	SubcomponentDSM = "dsm"
	// SubcomponentOps identifies global operations/observability projects.
	SubcomponentOps = "ops"
	// SubcomponentPortal identifies global Flowstate portal projects.
	SubcomponentPortal = "portal"
	// SubcomponentSandboxes identifies global sandbox infrastructure projects.
	SubcomponentSandboxes = "sandboxes"
	// SubcomponentSecrets identifies global secret manager projects.
	SubcomponentSecrets = "secrets"
)

// IsValidEnvironment reports whether env is a recognized environment label value
// per http://go/intrinsic-cloud-labels.
func IsValidEnvironment(env string) bool {
	return env == Prod || env == Staging || env == Dev
}

// Because GCP label values only permit [a-z0-9_-]{0,63} (no dots '.'), canonical FQDNs are
// derived here from the http://go/intrinsic-cloud-labels environment and subcomponent values.
func intrinsicAIDomain(prefix, env string) string {
	switch env {
	case Prod:
		return fmt.Sprintf("%s.intrinsic.ai", prefix)
	case Staging:
		return fmt.Sprintf("%s-qa.intrinsic.ai", prefix)
	case Dev:
		return fmt.Sprintf("%s-dev.intrinsic.ai", prefix)
	default:
		return ""
	}
}

// PortalDomain returns the Flowstate portal API domain for env, or "" if env is unknown.
func PortalDomain(env string) string {
	return intrinsicAIDomain("flowstate", env)
}

// AccountsDomain returns the accounts service domain for env, or "" if env is unknown.
func AccountsDomain(env string) string {
	return intrinsicAIDomain("accounts", env)
}

// AssetsDomain returns the asset catalog domain for env, or "" if env is unknown.
func AssetsDomain(env string) string {
	return intrinsicAIDomain("assets", env)
}

// EquipmentDomain returns the parent *.intrinsic.equipment UI domain for env, or "" if env is unknown.
func EquipmentDomain(env string) string {
	switch env {
	case Prod:
		return "intrinsic.equipment"
	case Staging:
		return "qa.intrinsic.equipment"
	case Dev:
		return "dev.intrinsic.equipment"
	default:
		return ""
	}
}

// ProjectDomain returns the canonical primary service domain for projectID given its GCP Cloud Labels
// (http://go/intrinsic-cloud-labels). Global accounts, assets, and portal projects resolve to their
// *.intrinsic.ai service domains when projectLabels["environment"] is valid; all other projects resolve
// to www.endpoints.<projectID>.cloud.goog.
func ProjectDomain(projectID string, projectLabels map[string]string) string {
	if projectLabels[LabelComponent] == ComponentGlobal {
		env := projectLabels[LabelEnvironment]
		switch projectLabels[LabelSubcomponent] {
		case SubcomponentAccounts:
			if d := AccountsDomain(env); d != "" {
				return d
			}
		case SubcomponentAssets:
			if d := AssetsDomain(env); d != "" {
				return d
			}
		case SubcomponentPortal:
			if d := PortalDomain(env); d != "" {
				return d
			}
		}
	}
	if projectID == "" {
		return ""
	}
	return fmt.Sprintf("www.endpoints.%s.cloud.goog", projectID)
}
