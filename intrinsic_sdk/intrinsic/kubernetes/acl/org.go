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

// Package org provides helpers to deal with organizations in requests and code.
package org

import (
	"net/http"
	"regexp"  
	"strings" 

	log "github.com/golang/glog" 
	"github.com/rs/xid"
)

// OrgIDCookie is the cookie key for the organization identifier.
const OrgIDCookie = "org-id"

const OrgIDHeader = "x-intrinsic-org"



// IntrinsicOrgID is the organization identifier used for the Intrinsic in multi-tenant projects.
const IntrinsicOrgID = "intrinsic"

// PublicOrgID is an ACL-only organization used for public resources.
const PublicOrgID = "publicorg"



// Organization represents an organization inside the Intrinsic stack.
type Organization struct {
	ID string
}

// IDCookie returns a cookie with the given orgID.
func IDCookie(orgID string) *http.Cookie {
	return &http.Cookie{Name: OrgIDCookie, Value: orgID}
}

// GetID returns the identifier of the organization.
func (o *Organization) GetID() string {
	return o.ID
}

// ID returns a random organization ID.
func ID() string {
	return xid.New().String()
}


var (
	// use email as a base for org id, as per section 2.3 of RFC 3986 replace other chars by '_'.
	// The result must be a valid label value for compatibility with quota tracking:
	// https://cloud.google.com/compute/docs/labeling-resources#requirements
	stripEmail = regexp.MustCompile(`[^a-zA-Z0-9_-]`)
)

// NewFromUID makes a new org with an ID based on an email.
func NewFromUID(uid string) *Organization {
	orgid := strings.ToLower(stripEmail.ReplaceAllString(uid, "_"))
	// TODO(ensonic): for this to work as a cloud label, it would need to be 1...63 chars,
	// but this is not unique anymore
	if len(orgid) > 63 {
		log.Warningf("OrgID %q is too long, truncating to %q", orgid, orgid[:63])
		orgid = orgid[:63]
	}
	return &Organization{ID: orgid}
}


