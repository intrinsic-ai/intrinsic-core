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

// Package version provides functions to translate between inversion and UI versions
package version

import (
	"strings"
)

// TranslateOSInversionToUI translates an inversion style OS version to an UI style OS version
//
// This is the inverse of TranslateOSUIToInversion
func TranslateOSInversionToUI(inversion string) string {
	return strings.TrimPrefix(inversion, "xfa.")
}

// TranslateOSUIToInversion translates an UI style OS version to an inversion style OS version
//
// This is the inverse of TranslateOSInversionToUI
func TranslateOSUIToInversion(ui string) string {
	if ui == "" {
		return ""
	}
	return "xfa." + ui
}

// TranslateOSAPIToUI translates an api style OS version to an UI style OS version
//
// This is the inverse of TranslateOSUIToAPI
func TranslateOSAPIToUI(api string) string {
	return strings.TrimPrefix(api, "0.0.1+xfa.")
}

// TranslateOSUIToAPI translates an UI style OS version to an api style OS version
//
// This is the inverse of TranslateOSAPIToUI
func TranslateOSUIToAPI(ui string) string {
	if ui == "" {
		return ""
	}
	return "0.0.1+xfa." + ui
}

// TranslateOSInversionToAPI translates an inversion style OS version to an UI style OS version
//
// This is the inverse of TranslateOSAPIToInversion
func TranslateOSInversionToAPI(inversion string) string {
	if inversion == "" {
		return ""
	}
	return "0.0.1+" + inversion
}

// TranslateOSAPIToInversion translates an UI style OS version to an inversion style OS version
//
// This is the inverse of TranslateOSInversionToAPI
func TranslateOSAPIToInversion(api string) string {
	return strings.TrimPrefix(api, "0.0.1+")
}

// TranslateBaseInversionToUI translates an inversion style base version to an UI style base version
//
// This is the inverse of TranslateBaseUIToInversion
func TranslateBaseInversionToUI(inversion string) string {
	return strings.ToUpper(inversion)
}

// TranslateBaseUIToInversion translates an UI style base version to an inversion style base version
//
// This is the inverse of TranslateBaseInversionToUI
func TranslateBaseUIToInversion(ui string) string {
	return strings.ToLower(ui)
}

// TranslateBaseInversionToAPI translates an inversion style base version to an API style base version
//
// This is the inverse of TranslateBaseAPIToInversion
func TranslateBaseInversionToAPI(inversion string) string {
	return TranslateBaseUIToAPI(TranslateBaseInversionToUI(inversion))
}

// TranslateBaseAPIToInversion translates an API style base version to an inversion style base version
//
// This is the inverse of TranslateBaseInversionToAPI
func TranslateBaseAPIToInversion(api string) string {
	return TranslateBaseUIToInversion(TranslateBaseAPIToUI(api))
}

// TranslateBaseUIToAPI translates an UI style base version to an API style base version
//
// This is the inverse of TranslateBaseAPIToUI
func TranslateBaseUIToAPI(ui string) string {
	if ui == "" {
		return ""
	}
	return "0.0.1+intrinsic.platform." + ui
}

// TranslateBaseAPIToUI translates an API style base version to an UI style base version
//
// This is the inverse of TranslateBaseUIToAPI
func TranslateBaseAPIToUI(api string) string {
	return strings.TrimPrefix(api, "0.0.1+intrinsic.platform.")
}
