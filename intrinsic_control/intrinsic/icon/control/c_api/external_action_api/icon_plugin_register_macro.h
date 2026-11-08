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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_PLUGIN_REGISTER_MACRO_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_PLUGIN_REGISTER_MACRO_H_

#include "intrinsic/icon/control/c_api/c_plugin_api.h"
#include "intrinsic/icon/control/c_api/external_action_api/make_icon_action_vtable.h"

// Implements the entrypoint for an ICON custom Action plugin for
// `ActionClassName`. Place this in a separate file (my_action_plugin.cc) and
// add a BUILD target with the following options to allow ICON to load your
// `ActionClassName` as a plugin:
//
// ```BUILD
// cc_binary(
//     name = "my_action_plugin.so",
//     srcs = ["my_action_plugin.cc"],
//     linkshared = 1,
//     linkstatic = 1,
//     deps = [
//         "//intrinsic/icon/control/c_api/external_action_api:icon_plugin_register_macro",
//         ":my_action",
//     ],
// )
// ```
//
// Before you do, make sure your Action not only implements the virtual
// functions from IconActionInterface, but also meets the additional
// requirements listed in icon_action_interface.h.
#define INTRINSIC_ICON_REGISTER_ICON_ACTION_PLUGIN(ActionClassName)      \
  extern "C" {                                                           \
  __attribute__((__visibility__("default"))) IntrinsicIconRealtimeStatus \
  INTRINSIC_ICON_ACTION_PLUGIN_ENTRY_POINT(                              \
      IntrinsicIconRegisterActionType register_action_type_fn) {         \
    return intrinsic::icon::RegisterIconAction<ActionClassName>(         \
        register_action_type_fn);                                        \
  }                                                                      \
  }

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_PLUGIN_REGISTER_MACRO_H_
