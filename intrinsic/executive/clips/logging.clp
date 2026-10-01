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

; This file contains rules to trigger logging of Executive data as necessary.
; Requires: log_context.clp
; behavior_call_instance_tree.clp (finding parent PBT for parent log id)

; Contract on logged context IDs.
; The executive logs LoggedOperation log items at specific points in time:
; - on startup
;   Context IDs set: session
;   Note: This `LoggedOperation` has no operation. This item should only be used
;         to get the session ID as early as possible during execution.
; - when starting or ending a behavior tree.
;   Context IDs set: session, plan
;   The contexts will look the same for the start and end items. They can be
;   disambiguated by the timestamp (the start item is guaranteed to be logged
;   before the end item).
;   We additionally log the final `LoggedOperation` to a separate event source
;   at the end. This serves as a history of completed operations.
; - when starting, restarting, or ending a skill execution attempt
;   Context IDs set: session, plan, plan_action, (parent_skill)
;   Actions have a lifecycle, the relevant pieces are:
;   ACCEPTED -> SELECTED -> ... -> EXECUTION_SUCCEEDED -> SUCCEEDED
;                 ^        \      \                    \
;                  \        \      \-> EXECUTION_FAILED -> FAILED
;                    \---<-- \ -------<-------/             ^
;                             \-----------------------------/
;   So, a skill is selected for execution. It may fail then, e.g., if a
;   precondition is not fulfilled and recovery is not enabled. If it is
;   executed, this execution can succeed or fail (EXECUTION_SUCCEEDED or
;   EXECUTION_FAILED). If it succeeded, it can still overall fail (e.g.,
;   expected effects have not been observed, this is future work). If it
;   failed, it may go back to SELECTED (if we retry), or ultimately fail.
;   Note: A skill can also be a parameterizable behavior tree here, where the
;   EXECUTION_SUCCEEDED and EXECUTION_FAILED states do not exist. The log items
;   are generated at exactly the same points: That is when the task node is
;   selected/completed.
;   parent_skill is optionally set, when the respective log-item is a skill
;   or parameterizable behavior tree (PBT) execution that happened within a PBT.
;   In that case parent_skill is the id of the plan_action of the containing
;   PBT.
;
;   LoggedOperation log items will be:
;   - 1 or more SELECTED states (one for each attempt, since the world may have
;     changed between attempts, we need to log every time)
;   - exactly one of FAILED or SUCCEEDED (terminal state)
;   - At most 1 EXECUTION_SUCCEEDED (once for successful execution attempt)
;   - 0, 1, or more of or EXECUTION_FAILED (if an action failed, once for each
;     failed attempt)
;
;   Some samples for context for log items (format: state/plan-action-id)
;   (IDs will be random, same numbers only indicate equality):
;   - Successful execution:
;     SELECTED/1 EXECUTION-SUCCEEDED/1 SUCCEEDED/1
;   - Fail, retry, succeed:
;     SELECTED/1 EXECUTION-FAILED/1 SELECTED/2 EXECUTION-SUCCEEDED/2 SUCCEEDED/2
;   - Fail, retry once, fail:
;     SELECTED/1 EXECUTION-FAILED/1 SELECTED/2 EXECUTION-FAILED/2 FAILED/2
;   - Fail execution (no retry/recovery):
;     SELECTED/1 EXECUTION-FAILED/1 FAILED/1
;   - Fail precondition, replan, run again later, succeed:
;     SELECTED/1 SELECTED/2 EXECUTION-SUCCEEDED/2 SUCCEEDED/2
;   - Fail precondition, fail (no recovery by planning):
;     SELECTED/1 FAILED/1

; ------------------------------- FUNCTIONS -----------------------------------

(deffunction tracing-conditionally-start-log-tracing (?span-name)
  (bind ?run-log-span ?*TRACING-INVALID-SPAN-ID*)
  (do-for-fact ((?sc tracing-clips-run-context)) TRUE
    (bind ?run-log-span (span-start ?span-name
      ?sc:span-reference-id "log-call"))
  )
  (return ?run-log-span)
)

; Determines the log-id of an enclosing call for a plan-action or
; behavior-call-instance.
;
; This is 0 when running in a process tree and returns the log-id of the
; running behavior-call-instance when the ?tree is running within a PBT.
(deffunction log-find-parent-log-id (?tree-id)
  (bind ?enclosing-behavior-call-uid
    (behavior-call-instance-find-enclosing-pbt-instance-uid ?tree-id))
  (if (eq ?enclosing-behavior-call-uid nil) then
    (return 0)
  )
  (do-for-fact ((?bci behavior-call-instance))
    (eq ?bci:uid ?enclosing-behavior-call-uid)
    (return ?bci:log-id)
  )
  (printout error (str-cat "When finding parent-log-id, tree " ?tree-id
                           " reported " ?enclosing-behavior-call-uid
                           " to be its enclosing UID, but that did not have a"
                           " matching behavior-call-instance") crlf)
  (return 0)
)

; Creates a new FlowstateEvent proto for behavior tree events.
;
; Args:
;   ?action: the action executed triggering the event
;   ?result: result of the action
;   ?labels: list of labels, must be of the form (key1 value1 key2 value2 ...)
;
; Returns:
;   FlowstateEvent proto with the given information.
(deffunction log-create-flowstate-event (?action ?result $?labels)
  (bind ?fe (pb-create "intrinsic_proto.flowstate_event.FlowstateEvent"))
  (pb-set-field ?fe "event_type" "BEHAVIOR-TREE")
  (pb-set-field ?fe "action" ?action)
  (pb-set-field ?fe "result" ?result)
  (loop-for-count (?i 1 (div (length$ ?labels) 2))
    (bind ?key-idx (* ?i 2))
    (bind ?value-idx (+ ?key-idx 1))
    (bind ?key (nth$ ?key-idx ?labels))
    (bind ?value (nth$ ?value-idx ?labels))
    (pb-set-map-value ?fe "labels" ?key ?value)
  )
  (return ?fe)
)

; ----------------------------------- RULES -----------------------------------

(defrule log-create-session
  "Log logged operation on startup and use as session log ID"
  (declare (salience ?*SALIENCE-LOGGING*))
  ?es <- (executive-state (state CREATING) (session-log-id 0))
  (world (id ?world-id))
 =>
  (bind ?session-log-id (log-gen-uid))
  (bind ?log-context (log-context-create ?session-log-id 0 0 0 ""))
  ; no world loaded yet, do not log, hence empty world id ("")
  (bind ?logged-op-proto (logged-operation-create-proto "" "" 0))

  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                      ?log-context
                                                      ?session-log-id
                                                      ?*TRACING-INVALID-SPAN-ID*
                                                      ""))
  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation to create session failed" crlf)
  )
  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  (modify ?es (session-log-id ?session-log-id))
)

(defrule log-sub-tree-propagate-log-id
  "Propagate log-id running subtrees"
  (declare (salience ?*SALIENCE-LOGGING*))
  ?bt <- (behavior-tree (parent-behavior-tree-id ?parent-tree-id) (log-id 0))
  (behavior-tree (id ?parent-tree-id) (log-id ?bt-log-id&~0))
 =>
  (modify ?bt (log-id ?bt-log-id))
)

(defrule log-new-operation
  "Log logged operation for new toplevel behavior tree"
  (declare (salience ?*SALIENCE-LOGGING*))
  (executive-state (session-log-id ?session-log-id&~0))
  (operation-envelope (name ?op-name) (operation-tree-id ?bt-id)
                      (span-reference-id ?trace-span-id))
  ; Initial log request for a new operation. Thus use the operation-tree, not
  ; the start-tree to initialize all sub-tree's log-ids (see
  ; log-sub-tree-propagate-log-id).
  ; Note that the trace-span-id is likely still 0 as the operation hasn't
  ; started, yet. This is to be expected.
  ?bt <- (behavior-tree (id ?bt-id) (parent-behavior-tree-id nil)
                        (operation-name ?op-name)
                        (log-id 0) (state ACCEPTED|RUNNING))
  (world (id ?world-id))
 =>
  (bind ?bt-log-id (log-gen-uid))

  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_new_operation" ?trace-span-id))
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id 0 0
                                         ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id "" 0))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                      ?log-context
                                                      ?bt-log-id
                                                      ?trace-span-id
                                                      ?log-logged-op-world-id))
  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for top-level tree" crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )
  (pb-remove ?logged-op-proto)

  (bind ?fe-proto (log-create-flowstate-event "create" "succeeded"))
  (bind ?log-fe-ok (log-flowstate-event-async ?fe-proto ?log-context
                                              (log-gen-uid)
                                              ?trace-span-id))
  (if (not ?log-fe-ok) then
    (printout error "Logging flowstate event failed for top-level tree" crlf)
  )

  (pb-remove ?fe-proto)
  (pb-remove ?log-context)
  (modify ?bt (log-id ?bt-log-id))
)

(defrule log-completed-operation
  "Log logged operation for completed operation"
  (declare (salience ?*SALIENCE-LOGGING*))
  (executive-state (session-log-id ?session-log-id&~0))
  ?op <- (operation-envelope (name ?op-name) (operation-tree-id ?bt-id)
                             (state ?state&FAILED|SUCCEEDED|CANCELED)
                             (span-reference-id ?trace-span-id)
                             (logged-completion FALSE))
  (behavior-tree (id ?bt-id) (log-id ?bt-log-id&~0))
  (world (id ?world-id))
 =>
  (bind ?pregenerated-log-id (log-gen-uid))

  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_completed_operation" ?trace-span-id)
  )
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id 0 0
                                         ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id "" 0))
  ; Exactly one logged operation must be logged to history so that we have an
  ; event source with a linear history and no additional noise.
  (bind ?log-logged-op-ok
    (log-logged-operation-with-history-async ?logged-op-proto
                                             ?log-context
                                             ?pregenerated-log-id
                                             ?trace-span-id
                                             ?log-logged-op-world-id)
  )
  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for top-level tree" crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )
  (pb-remove ?logged-op-proto)

  (bind ?fe-proto (log-create-flowstate-event "run" (lowcase (str-cat ?state))))
  (bind ?log-fe-ok (log-flowstate-event-async ?fe-proto ?log-context
                                              (log-gen-uid)
                                              ?trace-span-id))
  (if (not ?log-fe-ok) then
    (printout error "Logging flowstate event failed for top-level tree" crlf)
  )

  (pb-remove ?fe-proto)
  (pb-remove ?log-context)
  (modify ?op (logged-completion TRUE))
)

(defrule log-suspended-operation
  "Log logged operation for suspended operation"
  (declare (salience ?*SALIENCE-LOGGING*))
  (executive-state (session-log-id ?session-log-id&~0))
  ?op <- (operation-envelope (name ?op-name) (operation-tree-id ?bt-id)
                             (state SUSPENDED)
                             (span-reference-id ?trace-span-id)
                             (logged-on-suspend FALSE))
  (behavior-tree (id ?bt-id) (log-id ?bt-log-id&~0))
  (world (id ?world-id))
 =>
  (bind ?pregenerated-log-id (log-gen-uid))

  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_suspended_operation" ?trace-span-id)
  )
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id 0 0
                                         ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id "" 0))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                      ?log-context
                                                      ?pregenerated-log-id
                                                      ?trace-span-id
                                                      ?log-logged-op-world-id))

  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for suspended process tree"
                    crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )

  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  (modify ?op (logged-on-suspend TRUE))
)

(defrule log-selected-action
  "Log logged operation when an action was selected for execution"
  (declare (salience ?*SALIENCE-LOGGING*))
  (world (id ?world-id))
  (executive-state (session-log-id ?session-log-id&~0))
  (behavior-tree (id ?bt-id) (plan-id ?plan-id) (log-id ?bt-log-id&~0)
                 (operation-name ?op-name) (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?bt-id) (type TASK)
                      (task-action-uid ?action-uid)
                      (span-reference-id ?trace-span-id))
  ?pa <- (plan-action (uid ?action-uid) (plan-id ?plan-id)
                      (state SELECTED) (log-id 0)
                      (span-reference-id ?skill-trace-span-id))
 =>
  (bind ?action-log-id (log-gen-uid))
  (bind ?parent-log-id (log-find-parent-log-id ?bt-id))

  ; Skill execution is blocked until this rule has finished. Thus record this
  ; call in the skill-trace-span in contrast to the trace-span from the node,
  ; which runs async in the log-calls.
  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_selected_action" ?skill-trace-span-id))
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id
                                         ?action-log-id ?parent-log-id
                                         ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id ?bt-id
                                                        ?node-id))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                      ?log-context
                                                      ?action-log-id
                                                      ?trace-span-id
                                                      ?log-logged-op-world-id))

  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for selected action "
                    ?action-uid crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )

  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  (modify ?pa (log-id ?action-log-id))
)

(defrule log-completed-action
  "Log logged operation when an action has completed execution"
  (declare (salience ?*SALIENCE-LOGGING*))
  (executive-state (session-log-id ?session-log-id&~0))
  (behavior-tree (id ?bt-id) (plan-id ?plan-id) (log-id ?bt-log-id&~0)
                 (operation-name ?op-name) (state RUNNING|CANCELING|SUSPENDING))
  (behavior-tree-node (id ?node-id) (tree-id ?bt-id) (type TASK)
                      (state SUCCEEDED|FAILED|CANCELED)
                      (task-action-uid ?action-uid))
  ?pa <- (plan-action (uid ?action-uid) (plan-id ?plan-id)
                      (state EXECUTION-SUCCEEDED|EXECUTION-FAILED|
                             SUCCEEDED|FAILED|CANCELED)
                      (log-id ?action-log-id&~0)
                      (world-id-execution-after ?world-id&~"")
                      (logged-completion FALSE))
 =>
  (bind ?parent-log-id (log-find-parent-log-id ?bt-id))
  (bind ?pregenerated-log-id (log-gen-uid))
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id
                                         ?action-log-id ?parent-log-id
                                         ?op-name))

  (bind ?logged-op-proto (logged-operation-create-proto ?world-id ?bt-id
                                                        ?node-id))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                      ?log-context
                                                      ?pregenerated-log-id
                                                      ?*TRACING-INVALID-SPAN-ID*
                                                      ?world-id))

  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for completed action " ?action-uid
                    crlf)
    (if (neq ?world-id "") then
      (assert (world-request (type DELETE) (world-id ?world-id))))
  )

  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  ; log-logged-operation-async takes ownership of the passed ?world-id and
  ; deletes that (or the following assert in case of failure). Do not track this
  ; any more in the plan-action!
  (modify ?pa (logged-completion TRUE) (world-id-execution-after ""))
)

(defrule log-selected-behavior-call
  "Log logged operation when a behavior-call was selected for execution"
  (declare (salience ?*SALIENCE-LOGGING*))
  (world (id ?world-id))
  (executive-state (session-log-id ?session-log-id&~0))
  (behavior-tree (id ?bt-id) (log-id ?bt-log-id&~0) (operation-name ?op-name)
                 (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?bt-id) (type TASK)
                      (behavior-call-instance-uid ?bci-uid)
                      (span-reference-id ?trace-span-id))
  ?bci <- (behavior-call-instance (uid ?bci-uid) (state SELECTED) (log-id 0))
 =>
  (bind ?parent-log-id (log-find-parent-log-id ?bt-id))
  (bind ?bc-log-id (log-gen-uid))

  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_selected_behavior" ?trace-span-id))
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id
                                         ?bc-log-id ?parent-log-id ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id ?bt-id
                                                        ?node-id))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                      ?log-context
                                                      ?bc-log-id
                                                      ?trace-span-id
                                                      ?log-logged-op-world-id))

  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for selected behavior-call "
                    ?bci-uid crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )

  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  (modify ?bci (log-id ?bc-log-id))
)

(defrule log-completed-behavior-call
  "Log logged operation when a behavior-call has completed execution"
  (declare (salience ?*SALIENCE-LOGGING*))
  (world (id ?world-id))
  (executive-state (session-log-id ?session-log-id&~0))
  (behavior-tree (id ?bt-id) (log-id ?bt-log-id&~0) (operation-name ?op-name)
                 (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?bt-id) (type TASK)
                      (state SUCCEEDED|FAILED)
                      (behavior-call-instance-uid ?bci-uid))
  ?bci <- (behavior-call-instance (uid ?bci-uid) (state SUCCEEDED|FAILED)
                      (log-id ?bc-log-id&~0) (logged-completion FALSE))
 =>
  (bind ?parent-log-id (log-find-parent-log-id ?bt-id))
  (bind ?pregenerated-log-id (log-gen-uid))

  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_completed_behavior" ?*TRACING-INVALID-SPAN-ID*))
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id
                                         ?bc-log-id ?parent-log-id
                                         ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id ?bt-id
                                                        ?node-id))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                      ?log-context
                                                      ?pregenerated-log-id
                                                      ?*TRACING-INVALID-SPAN-ID*
                                                      ?log-logged-op-world-id))

  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for completed behavior-call "
                    ?bci-uid crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )

  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  (modify ?bci (logged-completion TRUE))
)

(defrule log-selected-code-execution
  "Log logged operation when a code execution was selected for execution"
  (declare (salience ?*SALIENCE-LOGGING*))
  (world (id ?world-id))
  (executive-state (session-log-id ?session-log-id&~0))
  (behavior-tree (id ?bt-id) (log-id ?bt-log-id&~0) (operation-name ?op-name)
                 (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?bt-id) (type TASK)
                      (span-reference-id ?trace-span-id))
  ?cei <- (code-execution-instance (operation-name ?op-name) (tree-id ?bt-id)
                                   (node-id ?node-id) (state SELECTED) (log-id 0))
 =>
  (bind ?parent-log-id (log-find-parent-log-id ?bt-id))
  (bind ?cei-log-id (log-gen-uid))

  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_selected_code_execution" ?trace-span-id))
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id
                                         ?cei-log-id ?parent-log-id ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id ?bt-id
                                                        ?node-id))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                       ?log-context
                                                       ?cei-log-id
                                                       ?trace-span-id
                                                       ?log-logged-op-world-id))

  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for selected code-execution "
                    ?op-name ":" ?node-id crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )

  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  (modify ?cei (log-id ?cei-log-id))
)

(defrule log-completed-code-execution
  "Log logged operation when a code execution has completed execution"
  (declare (salience ?*SALIENCE-LOGGING*))
  (world (id ?world-id))
  (executive-state (session-log-id ?session-log-id&~0))
  (behavior-tree (id ?bt-id) (log-id ?bt-log-id&~0) (operation-name ?op-name)
                 (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?bt-id) (type TASK) (task-type EXECUTE-CODE)
                      (state SUCCEEDED|FAILED))
  ?cei <- (code-execution-instance (operation-name ?op-name) (tree-id ?bt-id)
                                   (node-id ?node-id) (state SUCCEEDED|FAILED)
                                   (log-id ?cei-log-id&~0) (logged-completion FALSE))
 =>
  (bind ?parent-log-id (log-find-parent-log-id ?bt-id))
  (bind ?pregenerated-log-id (log-gen-uid))

  (bind ?log-logged-op-world-id
    (world-clone ?world-id "log_completed_code_execution" ?*TRACING-INVALID-SPAN-ID*))
  (if (eq ?log-logged-op-world-id "") then
    (printout error "Failed to clone world " ?world-id ", logging without" crlf)
  )
  (bind ?log-context (log-context-create ?session-log-id ?bt-log-id
                                         ?cei-log-id ?parent-log-id
                                         ?op-name))
  (bind ?logged-op-proto (logged-operation-create-proto ?world-id ?bt-id
                                                        ?node-id))
  (bind ?log-logged-op-ok (log-logged-operation-async ?logged-op-proto
                                                       ?log-context
                                                       ?pregenerated-log-id
                                                       ?*TRACING-INVALID-SPAN-ID*
                                                       ?log-logged-op-world-id))

  (if (not ?log-logged-op-ok) then
    (printout error "Logging operation failed for completed code-execution "
                    ?op-name ":" ?node-id crlf)
    (if (neq ?log-logged-op-world-id "") then
      (assert (world-request (type DELETE) (world-id ?log-logged-op-world-id))))
  )

  (pb-remove ?logged-op-proto)
  (pb-remove ?log-context)
  (modify ?cei (logged-completion TRUE))
)
