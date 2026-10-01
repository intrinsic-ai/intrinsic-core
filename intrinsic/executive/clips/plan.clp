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

; Plan representation and handling

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate plan
  ; High-level task plan to be executed.
  (slot id (type SYMBOL))
  (slot type (type SYMBOL)
             (allowed-values SEQUENTIAL BEHAVIOR-TREE))

  ; This Log ID for related log-context generation.
  ; 0 means that the plan has not yet been logged.
  (slot log-id (type INTEGER))
  (slot logged-completion (type SYMBOL) (allowed-values FALSE TRUE))
)

(deftemplate plan-action
  ; One unit of the execution which is performed while following a plan.
  ; This is a CLIPS executive internal id to identify this fact within a plan.
  (slot id (type INTEGER))

  ; The plan this action belongs to.
  (slot plan-id (type SYMBOL))

  ; Skill id, this triggers the respective action execution engine.
  ; For executing skills, this refers to intrinsic_proto.skills.Skill.id.
  ; For no-op actions, a no-op action with this skill-id must exist.
  (slot skill-id (type STRING))

  ; Names and values for action parameters
  (multislot param-names (type SYMBOL))
  ; TODO(timdn): reconcile with actual skill parameters
  (multislot param-values)

  ; This is associated with a globally unique ID in the scope of the current
  ; executive lifetime. Leave to automatically based on plan and action IDs.
  ; If setting a custom UID you must guarantee uniqueness by yourself.
  (slot uid (type SYMBOL) (default AUTOMATIC))

  ; Proto ID of the associated BehaviorCall proto.
  (slot behavior-call-proto-id (type INTEGER))

  ; This action's LogID for related log-context generation
  ; 0 means that the plan action has not yet been logged.
  (slot log-id (type INTEGER))
  (slot logged-completion (type SYMBOL) (allowed-values FALSE TRUE))

  ; Name of the return-value, this can be used to access the value on the
  ; blackboard.
  (slot return-value-name (type STRING))

  ; States follow convention from https://aip.dev/216.
  ; Initially, after creation an action is in ACCEPTED state.
  ; We have two processes that change the state of an action:
  ;
  ; *Action selection* (for example, sequential)
  ; Chooses one or more actions simulatenously for execution. The state then
  ; becomes SELECTED.
  ;
  ; *Action execution*
  ; Initiates the action by changing its state to PENDING; this may for example
  ; correspond to making a gRPC call to a skill. Subsequently, the state becomes
  ; RUNNING, waiting for it to complete. Once the action has been executed (the
  ; gRPC is finished), the state becomes SUCCEEDED or FAILED.
  ;
  ; These states need to be kept in sync with the BehaviorTree.TaskNode.State
  ; proto enum, except for the custom mappings defined in the
  ; behavior-tree-export-node function.
  ;
  ; ACCEPTED: action has been formulated and is awaiting selection. If and when
  ;           it is selected depends on the specific semantics, e.g., for a
  ;           behavior tree that task node must have been selected.
  ;           This is the initial state for all actions.
  ; SELECTED: action has been selected for execution, i.e., it is expected to
  ;           be processed by an action executor, such as skill execution,
  ;           shortly/next.
  ;           Actions in this state can be suspended.
  ; PROJECTING: projection is running for a selected action. This may
  ;             take some time. Some action executors may forego projection,
  ;             e.g., no-op actions.
  ; PROJECTED: projection has completed. The action is now being checked for
  ;            conflicts. Actions in this state can be suspended.
  ; FOOTPRINT-CHECKING: for selected action that finished projection the
  ;                     footprint is currently being checked against all other
  ;                     running/ready actions for conflicts
  ; FOOTPRINT-CONFLICT-WAITING: footprint conflict checking detected a conflict
  ;                             with a currently running action and the action's
  ;                             conflict handling mode is set to WAIT. The action
  ;                             waits until all conflicting blocking actions finish.
  ; READY: conflict checking has been completed and no conflicts have been
  ;        found. The action is now ready for execution. Actions in this state
  ;        can be suspended.
  ; PENDING: execution has been triggered, waiting for confirmation, e.g., a
  ;          skill has been called and the executive is awaiting confirmation
  ;          that the request is being processed/skill has started. Transitions
  ;          to RUNNING once this has been confirmed.
  ; RUNNING: execution is on-going. Actions in this state can be cancelled.
  ; CANCELLATION-REQUESTED: action should cancel. The action executor
  ;                         transitions the action to CANCELLATION-PENDING (or
  ;                         directly to one of its successor states) once it
  ;                         triggers cancellation (e.g., by requesting the skill
  ;                         dispatcher to cancel the skill).
  ; CANCELLATION-PENDING: cancellation has been triggered. The action
  ;                       transitions to CANCELING once cancellation begins
  ;                       (e.g., when the skill service client requests the
  ;                       skill to cancel).
  ; CANCELING: action cancellation is on-going. Execution fails if action
  ;             cancellation finishes. Execution either succeeds or fails if the
  ;             action completes before cancellation finishes. This kind of
  ;             cancellation is triggered externaly, typically by a user.
  ; CANCELING-EXECUTION-TIMEOUT: action cancellation is on-going, it occured
  ;                              because the execution timed out.
  ; EXECUTION-FAILED: the action executor sets this state on failure. It is not
  ;                   a final state, for example, recovery might interject in
  ;                   this state to analyze and/or initiate a recovery. If not
  ;                   preempted by, e.g., recovery, a low salience rule will
  ;                   transition the action to the terminal FAILED state.
  ; EXECUTION-SUCCEEDED: the action executor sets this state on successful
  ;                      action execution. It is not a final state. For example,
  ;                      reasoning about effects may occur in this stage. If not
  ;                      preempted by, e.g., effect application a low salience
  ;                      rule will transition to the terminal SUCCEEDED state.
  ; SUCCEEDED: execution has completed successfully
  ; FAILED: execution has completed with failure
  (slot state (type SYMBOL)
        (allowed-values ACCEPTED SELECTED PROJECTING PROJECTED
                        FOOTPRINT-CHECKING FOOTPRINT-CONFLICT-WAITING READY
                        PENDING RUNNING
                        CANCELLATION-REQUESTED CANCELLATION-PENDING
                        CANCELING CANCELING-EXECUTION-TIMEOUT
                        EXECUTION-FAILED EXECUTION-SUCCEEDED
                        ; Terminal states
                        SUCCEEDED FAILED CANCELED)
        (default ACCEPTED))

  ; Specifies how footprint conflicts detected after action projection should be handled.
  ; UNSPECIFIED / FAIL: default behavior; report footprint conflict error and fail action execution.
  ; WAIT: enter FOOTPRINT-CONFLICT-WAITING state and pause action execution until all conflicting running actions finish.
  (slot conflict-handling-mode (type SYMBOL)
        (allowed-values UNSPECIFIED FAIL WAIT)
        (default UNSPECIFIED))

  ; This is tracks the state slot and updates the run metadata proto if they
  ; get out of sync, i.e., when the state changes.
  (slot run-metadata-proto-state (type SYMBOL) (default ACCEPTED))

  ; True if action has been determined to be executable.
  (slot executable (type SYMBOL) (allowed-values FALSE TRUE) (default TRUE))

  (multislot execution-select-time (type INTEGER) (cardinality 2 2))
  (multislot execution-start-time (type INTEGER) (cardinality 2 2))
  (multislot execution-end-time (type INTEGER) (cardinality 2 2))
  (multislot projection-start-time (type INTEGER) (cardinality 2 2))
  (multislot projection-end-time (type INTEGER) (cardinality 2 2))
  ; Contains all IDs of actions which were running at the time projection
  ; started.
  (multislot projection-running-actions (type INTEGER))

  ; Temporary world used for projection. It will be cloned from the main world
  ; before projection and has a unique ID. After projection success or failure
  ; it will not be used any further and will be deleted.
  (slot world-id-projection (type STRING))

  ; World after execution. It will be a unique cloned ID after execution has
  ; finished. There world is cloned here to be logged for completed actions. It
  ; may be removed from this fact after logging is completed.
  (slot world-id-execution-after (type STRING))

  ; This is the message ID for the associated Footprint proto.
  (slot footprint-proto (type INTEGER))

  ; The span-reference-id for tracing this action.
  (slot span-reference-id (type INTEGER))

  ; An actual ExtendedStatus proto created and propagated on failure.
  (slot extended-status-proto-id (type INTEGER))
)

(deftemplate action-footprint-dependency
  ; uid of the action being blocked by other actions.
  (slot waiting-action-uid (type SYMBOL))
  ; uid of an action that was a conflict when trying to start waiting-action-uid
  (slot blocker-action-uid (type SYMBOL))
)

; --------------------------------- FUNCTIONS ---------------------------------

; Create a new number of new unique action IDs for a given plan.
; This iterates through all plan-actions associated to the respective plan and
; returns a sequence of new IDs larger than the maximum encountered ID of the
; given length. This can be used to inject new actions into a plan.
; If no plan for the given ID exists, returns nil.
(deffunction plan-next-action-ids (?plan-id ?num-ids)
  (bind ?max-id 0)
  (bind ?new-ids (create$))
  ; Fact set query, runs over all plan-actions with the wanted ?plan-id.
  ; Finds the maximum assigned action ID in a plan.
  (do-for-all-facts ((?pa plan-action)) (eq ?pa:plan-id ?plan-id)
    (if (> ?pa:id ?max-id) then (bind ?max-id ?pa:id))
  )
  ; Generate a sequence of new actions, starting at the max ID + 1 and for
  ; the number of requested IDs.
  (loop-for-count (?i ?num-ids)
    (bind ?new-ids (append$ ?new-ids (+ ?max-id ?i))))
  (return ?new-ids)
)

; Create a new unique action ID for a given plan.
; This iterates through all plan-actions associated to the respective plan and
; returns a new ID larger than the maximum encountered ID. This can be used
; to inject a new action into a plan.
; If no no plan for the given ID exists, returns nil.
(deffunction plan-next-action-id (?plan-id)
  (bind ?new-ids (plan-next-action-ids ?plan-id 1))
  (return (nth$ 1 ?new-ids))
)

(deffunction plan-action-remove (?uid)
  (delayed-do-for-all-facts ((?dep action-footprint-dependency))
      (or (eq ?dep:waiting-action-uid ?uid)
          (eq ?dep:blocker-action-uid ?uid))
    (retract ?dep)
  )
  (do-for-fact ((?pa plan-action)) (eq ?pa:uid ?uid)
    (if (neq ?pa:behavior-call-proto-id 0) then
      (pb-remove ?pa:behavior-call-proto-id))
    (if (neq ?pa:footprint-proto 0) then (pb-remove ?pa:footprint-proto))
    (if (neq ?pa:world-id-projection "") then
      (assert (world-request (type DELETE)
                             (world-id ?pa:world-id-projection))))
    (if (neq ?pa:world-id-execution-after "") then
      (assert (world-request (type DELETE)
                             (world-id ?pa:world-id-execution-after))))
    (if (<> ?pa:span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
      (tracing-end-action-span-failure ?pa ?*TRACING-STATUS-INTERNAL*
        "Span running when plan-action-remove was called.")
    )
    (pb-remove ?pa:extended-status-proto-id)
    (retract ?pa)
  )
)

; Remove a specific plan
(deffunction plan-remove (?plan-id)
  (delayed-do-for-all-facts ((?p plan)) (eq ?p:id ?plan-id)
    (retract ?p)
  )
  (delayed-do-for-all-facts ((?pa plan-action)) (eq ?pa:plan-id ?plan-id)
    (plan-action-remove ?pa:uid)
  )
  (return TRUE)
)

; Deletes all plans and plan actions.
(deffunction plan-delete-all ()
  (delayed-do-for-all-facts ((?p plan)) TRUE (plan-remove ?p:id))
  (delayed-do-for-all-facts ((?e error)) (eq ?e:type RECOVERABLE) (retract ?e))
)

; Determines if the action with ?name is a noop-action.
(deffunction plan-action-is-noop (?name)
  (do-for-fact ((?noop-action-info noop-action-info))
    (member$ ?name ?noop-action-info:noop-action-names)
    (return TRUE)
  )
  (return FALSE)
)

; Convert a given action to a nicely printable string from the skill id.
(deffunction plan-action-tostring (?plan-id ?action-id)
  (do-for-fact ((?pa plan-action)) (and (eq ?pa:plan-id ?plan-id)
                                        (eq ?pa:id ?action-id))
    (return (str-cat "Skill '" ?pa:skill-id "'"
                     (if (non-empty$ ?pa:param-values) then " "  else "")
                     (implode$ ?pa:param-values)))
  )
)

; Generate a unique ID symbol for an action in a plan.
(deffunction plan-action-uid (?plan-id ?action-id)
  (return (sym-cat ?plan-id "-" ?action-id "-" (gensym*)))
)

; ----------------------------------- RULES -----------------------------------

(defrule plan-set-plan-action-uid
  "For newly asserted plans set plan-action UID"
  (declare (salience ?*SALIENCE-HIGH*))
  (plan (id ?plan-id))
  ?pf <- (plan-action (plan-id ?plan-id) (id ?id) (uid AUTOMATIC))
 =>
  (modify ?pf (uid (plan-action-uid ?plan-id ?id)))
)

(defrule plan-action-failed-because-selected-but-not-executable
  "An action is not executable but has been selected. Fail the action.
   A higher salience rule might override this, e.g., by generating and injecting
   a recovery plan."
  ?pa <- (plan-action (plan-id ?plan-id) (id ?action-id)
                      (executable FALSE) (state SELECTED))
 =>
  (printout t "Action " ?action-id " " (plan-action-tostring ?plan-id ?action-id)
            " has been selected but is not executable" crlf)
  (tracing-end-action-span-failure ?pa
    ?*TRACING-STATUS-FAILED-PRECONDITION* "Action not executable")
  (modify ?pa (state FAILED) (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule plan-action-failed
  "Low salience rule to transition a plan-action from EXECUTION-FAILED to
   FAILED state if not preempted by any other higher salience rule."
  (declare (salience ?*SALIENCE-LOW*))
  ?pa <- (plan-action (state EXECUTION-FAILED))
 =>
  (tracing-end-action-span-failure ?pa
    ?*TRACING-STATUS-ABORTED* "Execution failed")
  (modify ?pa (state FAILED) (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule plan-action-succeeded
  "Low salience rule to transition a plan-action from EXECUTION-SUCCEEDED to
   SUCCEEDED state if not preempted by any other higher salience rule."
  (declare (salience ?*SALIENCE-LOW*))
  ?pa <- (plan-action (state EXECUTION-SUCCEEDED))
 =>
  (tracing-end-action-span ?pa)
  (modify ?pa (state SUCCEEDED) (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule plan-action-canceled
  "End the tracing span, when a plan-action is canceled."
  ?pa <- (plan-action (state CANCELED) (span-reference-id ~0))
 =>
  (tracing-end-action-span ?pa)
)
