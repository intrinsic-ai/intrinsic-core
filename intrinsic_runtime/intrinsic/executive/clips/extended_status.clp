; Copyright 2026 Intrinsic Innovation LLC
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;     https://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

; Utilities to deal with ExtendedStatus.

; For status codes in the executive, see and update executive/status_specs.txtpb

; ---------------------------- FORWARD DECLARATIONS ----------------------------

; Defined in logging.clp
(deffunction log-find-parent-log-id (?tree-id))

; --------------------------------- FUNCTIONS ---------------------------------

; Sets timestamp of ExtendedStatus proto.
;
; Args:
;   ?es-proto: ExtendedStatus proto
;   ?time: Time to set
(deffunction extended-status-set-timestamp (?es-proto $?time)
  (bind ?ts-proto (time-to-timestamp-proto ?time))
  (pb-set-field ?es-proto "timestamp" ?ts-proto)
  (pb-remove ?ts-proto)
)

; Creates an ExtendedStatus proto with the given component and code.
;
; This is for executive errors, i.e., not errors that originate, e.g., from a
; skill. The component will always be "ai.intrinsic.executive".
;
; Args:
;   ?code: component-specific error code.
;   ?options: Supported create options:
;               SEVERITY INFO|WARNING|ERROR|FATAL (default ERROR)
;
; Returns:
;   Proto message id of ExtendedStatus proto with status_code field being set.
(deffunction extended-status-create (?code $?options)
  (bind ?severity ERROR)
  (if (member$ SEVERITY ?options) then
    (bind ?severity (nth$ (+ (member$ SEVERITY ?options) 1) ?options)))

  (bind ?es-proto (extended-status-create-from-spec ?code ""))
  (pb-set-field ?es-proto "severity" ?severity)
  (return ?es-proto)
)

; Sets title of textended status.
;
; Prefer to not use this function, but rely on the titles specified in
; executive/status_specs.txtpb.
;
; Args:
;   ?es-proto: ExtendedStatus proto
;   ?title: string title to set
(deffunction extended-status-set-title (?es-proto ?title)
  (pb-set-field ?es-proto "title" ?title)
)

; Sets relations data for behavior tree node.
;
; Args:
;   ?es-proto: ExtendedStatus proto
;   ?tree-id: behavior tree id
;   ?node-id: behavior tree node id
(deffunction extended-status-set-related-to-by-ids (?es-proto ?tree-id ?node-id)
  (pb-set-field ?es-proto "related_to.behavior_tree_node.tree_id" ?tree-id)
  (pb-set-field ?es-proto "related_to.behavior_tree_node.node_id" ?node-id)
)

; Sets relations data for behavior tree node and log context.
;
; Args:
;   ?es-proto: ExtendedStatus proto
;   ?tree: behavior tree fact address
;   ?node: behavior tree node fact address
(deffunction extended-status-set-related-to (?es-proto ?tree ?node)
  (bind ?tree-id (fact-slot-value ?tree id))
  (bind ?node-id (fact-slot-value ?node id))
  (bind ?node-type (fact-slot-value ?node type))

  ; Set related behavior tree node
  (extended-status-set-related-to-by-ids ?es-proto ?tree-id ?node-id)

  ; Set related log context
  (bind ?log-id-session 0)
  (bind ?log-id-bt (fact-slot-value ?tree log-id))
  (bind ?log-id-action 0)
  (do-for-fact ((?executive-state executive-state)) TRUE
    (bind ?log-id-session ?executive-state:session-log-id))
  (bind ?log-id-action 0)
  (if (eq ?node-type TASK) then
    (bind ?task-action-uid (fact-slot-value ?node task-action-uid))
    (do-for-fact ((?pa plan-action)) (eq ?pa:uid ?task-action-uid)
      (bind ?log-id-action ?pa:log-id)
    )
  )
  (bind ?log-id-parent (log-find-parent-log-id ?tree-id))
  (bind ?context-proto
    (log-context-create ?log-id-session ?log-id-bt ?log-id-action ?log-id-parent
                        ""))
  (pb-set-field ?es-proto "related_to.log_context" ?context-proto)
  (pb-remove ?context-proto)
)

; Sets message (user or debug) of ExtendedStatus.
;
; Args:
;   ?es-proto: ExtendedStatus proto
;   ?which: One of DEBUG or USER
;   ?message: Message to set.
(deffunction extended-status-set-message (?es-proto ?which ?message)
  (if (eq ?which DEBUG)
   then (pb-set-field ?es-proto "debug_report.message" ?message))
  (if (eq ?which USER)
   then (pb-set-field ?es-proto "user_report.message" ?message))
)

; Sets user instructions ExtendedStatus.
;
; Args:
;   ?es-proto: ExtendedStatus proto
;   ?instructions: Instructions to set.
(deffunction extended-status-set-instructions (?es-proto ?instructions)
  (pb-set-field ?es-proto "user_report.instructions" ?instructions)
)

; Adds another ExtendedStatus as context.
;
; Args:
;   ?es-proto: ExtendedStatus proto
;   ?context-proto: ExtendedStatus proto to set as context
(deffunction extended-status-add-context (?es-proto ?context-proto)
  (pb-set-field ?es-proto "context[*]" ?context-proto)
)
