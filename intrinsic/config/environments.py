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

"""Helper functions to work with environments."""

import hashlib

# Environment constants
# LINT.IfChange(environments_constants)
PROD = "prod"
STAGING = "staging"
DEV = "dev"

# Accounts project constants
ACCOUNTS_PROJECT_DEV = "intrinsic-accounts-dev"
ACCOUNTS_PROJECT_STAGING = "intrinsic-accounts-staging"
ACCOUNTS_PROJECT_PROD = "intrinsic-accounts-prod"

# Accounts domain constants
ACCOUNTS_DOMAIN_DEV = "accounts-dev.intrinsic.ai"
ACCOUNTS_DOMAIN_STAGING = "accounts-qa.intrinsic.ai"
ACCOUNTS_DOMAIN_PROD = "accounts.intrinsic.ai"

# Portal project constants
PORTAL_PROJECT_DEV = "intrinsic-portal-dev"
PORTAL_PROJECT_STAGING = "intrinsic-portal-staging"
PORTAL_PROJECT_PROD = "intrinsic-portal-prod"

# Portal domain constants
PORTAL_DOMAIN_DEV = "flowstate-dev.intrinsic.ai"
PORTAL_DOMAIN_STAGING = "flowstate-qa.intrinsic.ai"
PORTAL_DOMAIN_PROD = "flowstate.intrinsic.ai"

# Equipment domain constants (parent domains for per-cluster UIs)
EQUIPMENT_DOMAIN_DEV = "dev.intrinsic.equipment"
EQUIPMENT_DOMAIN_STAGING = "qa.intrinsic.equipment"
EQUIPMENT_DOMAIN_PROD = "intrinsic.equipment"

# Assets project constants
ASSETS_PROJECT_DEV = "intrinsic-assets-dev"
ASSETS_PROJECT_STAGING = "intrinsic-assets-staging"
ASSETS_PROJECT_PROD = "intrinsic-assets-prod"

# Assets domain constants
ASSETS_DOMAIN_DEV = "assets-dev.intrinsic.ai"
ASSETS_DOMAIN_STAGING = "assets-qa.intrinsic.ai"
ASSETS_DOMAIN_PROD = "assets.intrinsic.ai"

# DSM project constants
DSM_PROJECT_DEV = "intrinsic-dsm-dev"
DSM_PROJECT_STAGING = "intrinsic-dsm-staging"
DSM_PROJECT_PROD = "intrinsic-dsm-prod"

# Ops project constants
OPS_PROJECT_PROD = "intrinsic-ops"

_DEV_PROJECT = (
    "b7219186c3255926d0c158c14b3e0363d6b386115d4c3f1d8e0c9723369ea3b4"
)
_STAGING_PROJECT = (
    "bb46d3dc2d207a46a66397e36698c40b66d3c0c364cd3fd2d196f60f4b1d9fd9"
)

# All environments
ALL = [PROD, STAGING, DEV]
# LINT.ThenChange(
#     //intrinsic/config/environments.go:environments_constants
# )


def from_domain(domain: str) -> str:
  """Returns the environment for a given domain."""
  if domain in (ACCOUNTS_DOMAIN_PROD, PORTAL_DOMAIN_PROD, ASSETS_DOMAIN_PROD):
    return PROD
  if domain in (
      ACCOUNTS_DOMAIN_STAGING,
      PORTAL_DOMAIN_STAGING,
      ASSETS_DOMAIN_STAGING,
  ):
    return STAGING
  if domain in (ACCOUNTS_DOMAIN_DEV, PORTAL_DOMAIN_DEV, ASSETS_DOMAIN_DEV):
    return DEV
  raise ValueError(f"Unknown domain: {domain}")


def from_project(project: str) -> str:
  """Returns the environment for a given project."""
  if project in (
      ACCOUNTS_PROJECT_PROD,
      PORTAL_PROJECT_PROD,
      ASSETS_PROJECT_PROD,
      DSM_PROJECT_PROD,
      OPS_PROJECT_PROD,
  ):
    return PROD
  if project in (
      ACCOUNTS_PROJECT_STAGING,
      PORTAL_PROJECT_STAGING,
      ASSETS_PROJECT_STAGING,
      DSM_PROJECT_STAGING,
  ):
    return STAGING
  if project in (
      ACCOUNTS_PROJECT_DEV,
      PORTAL_PROJECT_DEV,
      ASSETS_PROJECT_DEV,
      DSM_PROJECT_DEV,
  ):
    return DEV
  raise ValueError(f"Unknown project: {project}")


def from_any_project(project: str) -> str:
  """Infers environment of the project from the project name."""
  try:
    return from_project(project)
  except ValueError:
    return from_compute_project(project)


def from_compute_project(project: str) -> str:
  """Returns the environment for a given compute project."""
  if "-prod-" in project:
    return PROD

  hashed = _hash_project_name(project)
  if hashed == _DEV_PROJECT:
    return DEV
  if hashed == _STAGING_PROJECT:
    return STAGING
  return PROD


def portal_domain(env: str) -> str:
  """Returns the portal domain for a given environment."""
  if env == PROD:
    return PORTAL_DOMAIN_PROD
  if env == STAGING:
    return PORTAL_DOMAIN_STAGING
  if env == DEV:
    return PORTAL_DOMAIN_DEV
  raise ValueError(f"Unknown environment: {env}")


def equipment_domain(env: str) -> str:
  """Returns the equipment domain for a given environment."""
  if env == PROD:
    return EQUIPMENT_DOMAIN_PROD
  if env == STAGING:
    return EQUIPMENT_DOMAIN_STAGING
  if env == DEV:
    return EQUIPMENT_DOMAIN_DEV
  raise ValueError(f"Unknown environment: {env}")


def portal_project(env: str) -> str:
  """Returns the portal project for a given environment."""
  if env == PROD:
    return PORTAL_PROJECT_PROD
  if env == STAGING:
    return PORTAL_PROJECT_STAGING
  if env == DEV:
    return PORTAL_PROJECT_DEV
  raise ValueError(f"Unknown environment: {env}")


def accounts_domain(env: str) -> str:
  """Returns the accounts domain for a given environment."""
  if env == PROD:
    return ACCOUNTS_DOMAIN_PROD
  if env == STAGING:
    return ACCOUNTS_DOMAIN_STAGING
  if env == DEV:
    return ACCOUNTS_DOMAIN_DEV
  raise ValueError(f"Unknown environment: {env}")


def accounts_project_from_env(env: str) -> str:
  """Returns the accounts project for a given environment."""
  if env == PROD:
    return ACCOUNTS_PROJECT_PROD
  if env == STAGING:
    return ACCOUNTS_PROJECT_STAGING
  if env == DEV:
    return ACCOUNTS_PROJECT_DEV
  raise ValueError(f"Unknown environment: {env}")


def accounts_project_from_project(project: str) -> str:
  """Returns the accounts project for a given project."""
  return accounts_project_from_env(from_any_project(project))


def assets_domain(env: str) -> str:
  """Returns the assets domain for a given environment."""
  if env == PROD:
    return ASSETS_DOMAIN_PROD
  if env == STAGING:
    return ASSETS_DOMAIN_STAGING
  if env == DEV:
    return ASSETS_DOMAIN_DEV
  raise ValueError(f"Unknown environment: {env}")


def assets_project(env: str) -> str:
  """Returns the assets project for a given environment."""
  if env == PROD:
    return ASSETS_PROJECT_PROD
  if env == STAGING:
    return ASSETS_PROJECT_STAGING
  if env == DEV:
    return ASSETS_PROJECT_DEV
  raise ValueError(f"Unknown environment: {env}")


def domain(project: str) -> str:
  """Returns the domain name for a project.

  Mirrors Go's Domain(project) function in
  //intrinsic/config/environments.go.
  """
  if project == PORTAL_PROJECT_PROD:
    return PORTAL_DOMAIN_PROD
  if project == PORTAL_PROJECT_STAGING:
    return PORTAL_DOMAIN_STAGING
  if project == PORTAL_PROJECT_DEV:
    return PORTAL_DOMAIN_DEV
  if project == ACCOUNTS_PROJECT_PROD:
    return ACCOUNTS_DOMAIN_PROD
  if project == ACCOUNTS_PROJECT_STAGING:
    return ACCOUNTS_DOMAIN_STAGING
  if project == ACCOUNTS_PROJECT_DEV:
    return ACCOUNTS_DOMAIN_DEV
  if project == ASSETS_PROJECT_DEV:
    return ASSETS_DOMAIN_DEV
  if project == ASSETS_PROJECT_STAGING:
    return ASSETS_DOMAIN_STAGING
  if project == ASSETS_PROJECT_PROD:
    return ASSETS_DOMAIN_PROD
  return f"www.endpoints.{project}.cloud.goog"


_PROJECT_NAME_SALT = "2lJEUX97RpOzOvXQJhN+NRt0+KJ4z1KyPXtfe7"


def _hash_project_name(name: str) -> str:
  hasher = hashlib.sha256()
  hasher.update(_PROJECT_NAME_SALT.encode("utf-8"))
  hasher.update(name.encode("utf-8"))
  return hasher.hexdigest()
