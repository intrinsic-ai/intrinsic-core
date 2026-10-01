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

; The operation-envelope wraps the operation and everything that relates to it.

; --------------------------------- GLOBALS - ---------------------------------

(defglobal
  ?*OPERATION-WAITING-STATES* = (create$ ACCEPTED SUSPENDED
                                         SUCCEEDED FAILED CANCELED)
)

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate operation-envelope
  ; Name of this operation. Refers a google.longrunning.Operation.name.
  ; As such it is a unique identifier and not a user-configurable name.
  (slot name (type STRING))

  ; State of this operation depending on the behavior tree being executed.
  ; See: behavior_tree.clp
  (slot state (type SYMBOL) (allowed-values ACCEPTED PREPARING RUNNING CANCELING
                                            SUSPENDING SUSPENDED
                                            ; terminal states
                                            SUCCEEDED FAILED CANCELED)
                            (default ACCEPTED))

  ; Tracks the state of the operation in the metadata proto so we can keep it
  ; up-to-date
  (slot run-metadata-proto-state (type SYMBOL)
        (allowed-values ACCEPTED PREPARING RUNNING CANCELING SUSPENDING
                        SUSPENDED SUCCEEDED FAILED CANCELED)
        (default ACCEPTED))

  (slot execution-mode (type SYMBOL) (allowed-values NORMAL STEP-WISE)
                       (default NORMAL))

  ; Tracks the state of execution-mode in the metadata proto so we can keep it
  ; up-to-date
  (slot run-metadata-proto-execution-mode (type SYMBOL)
        (allowed-values NORMAL STEP-WISE)
        (default NORMAL))

  (slot simulation-mode (type SYMBOL) (allowed-values REALITY PREVIEW FAST-PREVIEW)
                        (default REALITY))

  ; Tracks the state of simulation-mode in the metadata proto so we can keep it
  ; up-to-date
  (slot run-metadata-proto-simulation-mode (type SYMBOL)
        (allowed-values REALITY PREVIEW FAST-PREVIEW)
        (default REALITY))

  (slot skill-trace-handling (type SYMBOL)
             (allowed-values UNSPECIFIED LINK EMBED)
             (default LINK))

  ; The proto representing this operation.
  ; Refers a google.longrunning.Operation.
  ; It's metadata Any field is *never set*. This is tracked independently in
  ; run-metadata-proto and will have to be serialized on request.
  ; TODO(493547558, 493546738): For now the contents are not initialized or
  ; updated. Do that to make this usable.
  (slot operation-proto (type INTEGER))
  ; An instance of the RunMetadata proto from this operation. Holds the current
  ; state of the behavior tree.
  ; TODO(493547558, 493546738): For now only the behavior_tree field is updated.
  ; This will be adapted, so that the full RunMetadata proto is a correct
  ; representation of the current state. Anything besides the behavior_tree
  ; field must *not* be touched.
  (slot run-metadata-proto (type INTEGER))

  ; span-reference-id to the currently active tracing span
  ; only valid during execution, ?*TRACING-INVALID-SPAN-ID* otherwise
  (slot span-reference-id (type INTEGER) (default ?*TRACING-INVALID-SPAN-ID*))

  ; Trace ID and URL as hex of the current or last active trace.
  ; This data will be set (or updated) when entering the RUNNING state. It will
  ; not be flushed in order to have valid TracingInfo in the ExecutiveState also
  ; after execution completion.
  (slot trace-id (type STRING))
  (slot trace-url (type STRING))

  ; The current top-level Behavior Tree which is provided by the
  ; user.
  ; Note that there may be any number of BTs at any point in time (e.g., a BT
  ; may include another for a condition, sub-trees, system trees), but only one
  ; is considered to be the top-level tree.
  (slot operation-tree-id (type SYMBOL))

  ; The tree that will be started/is running. This must be a sub-tree that is
  ; part of the tree referenced by operation-tree-id, or that exact tree.
  ; This is usually equal to operation-tree-id, but can be set to a different
  ; tree for debugging sub-trees. The overall state of the operation is
  ; determined by this tree.
  (slot start-tree-id (type SYMBOL))

  ; Tracks the state of start-tree-id and the start tree's start-node-id in the
  ; metadata proto so we can keep them up-to-date.
  (slot run-metadata-proto-start-tree-id (type SYMBOL) (default nil))
  (slot run-metadata-proto-start-node-id (type INTEGER) (default 0))

  ; Recovery state proto to be applied when starting. Only valid when starting
  ; (PREPARING) from an ACCEPTED operation.
  ; Will only be present for recovery, i.e., when recovery was requested and the
  ; process tree is not running, yet. Upon starting the process tree, this state
  ; proto will be applied instead and then the proto is discarded.
  (slot recovery-state-proto (type INTEGER))

  ; File descriptor set for the parameters; set if the operation tree is a PBT.
  (slot parameter-descriptor-set-proto (type INTEGER))
  (slot parameter-descriptor-pool (type INTEGER))
  (slot parameter-message-name (type STRING))
  ; The parameters proto of this operation as an Any proto of type specified in
  ; parameter-message-name.
  ; This is only set when the operation tree is a PBT.
  (slot parameter-proto (type INTEGER))

  ; File descriptor set for the return value
  ; only set if the operation tree is a PBT.
  (slot return-value-descriptor-set-proto (type INTEGER))
  (slot return-value-descriptor-pool (type INTEGER))
  (slot return-value-message-name (type STRING))
  ; This will be set, when the operation tree is a PBT that defines a
  ; return-value-expression and the operation succeeded. It is a message of type
  ; return-value-message-name (not an Any).
  (slot return-value-proto (type INTEGER))

  ; list of pairs <resource_slot_name> <resource_handle> that assign resource
  ; slots to the given resource_handle, when the operation tree is a PBT.
  (multislot resources (type STRING))

  ; FALSE as long as the completed operation has not been logged.
  (slot logged-completion (type SYMBOL) (allowed-values FALSE TRUE))
  ; FALSE as long as the SUSPENDED operation has not been logged.
  ; This needs to be reset when resuming execution of the operation.
  (slot logged-on-suspend (type SYMBOL) (allowed-values FALSE TRUE))

  ; Counts the consecutive number of times that the tree in operation-tree-id
  ; was RUNNING, but none of its actions (skills) were running. This indicates
  ; the executive not progressing although it should.
  (slot running-without-active-actions-count (type INTEGER))

  ; The scene ID that is associated with this operation. This is set only once
  ; conductor preparation completes (transitioning to RUNNING or SUSPENDED) and
  ; cleared when the operation is reset.
  (slot scene-id (type STRING))

  ; Tracks the state of scene-id in the metadata proto so we can keep it
  ; up-to-date
  (slot run-metadata-proto-scene-id (type STRING))

  ; Tracks the state of start-time in the metadata proto so we can keep it
  ; up-to-date
  (multislot run-metadata-proto-start-time (type INTEGER) (cardinality 2 2)
             (default 0 0))

  ; An ExtendedStatus proto created or propagated up on failure.
  (slot extended-status-proto-id (type INTEGER))

  ; TRUE if the skill instances have been cleared/reset for this operation.
  (slot skill-instances-are-reset (type SYMBOL) (allowed-values TRUE FALSE) (default FALSE))
)

; Requests an update to change the execution state of the operation. This is
; used to start (PREPARING), suspend (SUSPENDING), resume (RESUME), or cancel
; (CANCELING) this operation.
(deftemplate operation-update-state
  ; Name of the operation to update. Refers an operation-envelope.
  (slot operation-name (type STRING))

  (slot target-state (type SYMBOL)
                     (allowed-values PREPARING RUNNING RESUME SUSPENDING
                                     CANCELING)
                     (default ?NONE))
  (slot resume-mode (type SYMBOL) (allowed-values NONE CONTINUE STEP NEXT))

  ; An optional Any proto that might define the start parameters.
  (slot parameter-proto (type INTEGER))
  ; An optional multislot defining start resources.
  ; List of pairs <resource_slot_name> <resource_handle> that assign resource
  ; slots to the given resource_handle, when the operation tree is a PBT.
  (multislot resources (type STRING))

  ; Recovery state proto to be applied when starting. Only valid when starting
  ; (PREPARING) from an ACCEPTED operation.
  (slot recovery-state-proto (type INTEGER))

  ; The span-reference-id for the tracing span to be active in target-state.
  ; Use ?*TRACING-INVALID-SPAN-ID* (0, default) if no span should be active.
  (slot span-reference-id (type INTEGER))
)

; Requests to start the operation from a different node than the root of the
; operation tree. tree id must be within the operation-tree and node-id a node
; in the tree.
; To reset execution back to run the root of the operation tree pass in nil as
; the tree-id.
(deftemplate operation-update-start-node
  ; Name of the operation to update. Refers an operation-envelope.
  (slot operation-name (type STRING))

  (slot tree-id (type SYMBOL))
  (slot node-id (type INTEGER))
)

; Update the modes of the operation. simulation-mode indicates, if actions
; should be executed (REALITY) or run via preview (PREVIEW). execution-mode
; states, if execution should proceed as usual (NORMAL) or only a single node
; should be executed every time (STEP-WISE).
(deftemplate operation-update-mode
  ; Name of the operation to update. Refers an operation-envelope.
  (slot operation-name (type STRING))

  (slot target-simulation-mode (type SYMBOL)
                    (allowed-values NONE REALITY PREVIEW FAST-PREVIEW)
                    (default NONE))
  (slot target-execution-mode (type SYMBOL)
                    (allowed-values NONE NORMAL STEP-WISE)
                    (default NONE))
)

; Update the skill-trace-handling of the operation. By default skill traces are
; linked (LINK) and thus will be separated traces. If EMBED is chosen all skill
; traces are part of the execution trace.
(deftemplate operation-update-skill-trace-handling
  ; Name of the operation to update. Refers an operation-envelope.
  (slot operation-name (type STRING))

  (slot target-handling (type SYMBOL)
                        (allowed-values UNSPECIFIED LINK EMBED)
                        (default ?NONE))
)

; --------------------------------- FUNCTIONS ---------------------------------
