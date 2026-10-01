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

; Processing rules for a node of type TASK.

; The TASK node runs an action, e.g., a skill. If succeeds if the action
; succeeds, and fails if the action fails.


(defglobal
 ; Limit the output of parameter printouts.
 ; TODO(b/380029897): Find a concise way to enable debugging the full parameters
 ; without spamming logs.
 ?*TASK-NODE-MAX-PARAM-PRINT-LINES* = 10
)

; --------------------------------- FUNCTIONS ---------------------------------


; Prints all actions that the operation has some request running for.
;
; As a side-effect the function modifies the operation-envelope fact.
;
; Args:
;   ?op: Fact-address of the operation-envelope to print active action for.
(deffunction behavior-tree-task-node-print-active-actions (?op)
  (bind ?active-actions (create$))
  ; This filters for all plan-actions that are in an active state (e.g. RUNNING,
  ; etc.) and are the action (not prototype) of a task node in the process tree
  ; of the operation.
  ; The state of the task node is explicitly excluded as only the state of the
  ; actions matters.
  (do-for-all-facts ((?action plan-action))
    (isoneof ?action:state PROJECTING PENDING RUNNING
                           FOOTPRINT-CHECKING FOOTPRINT-CONFLICT-WAITING
                           CANCELLATION-REQUESTED CANCELLATION-PENDING
                           CANCELING CANCELING-EXECUTION-TIMEOUT)
    (bind ?action-uid ?action:uid)
    (do-for-fact
        ((?task-node behavior-tree-node))
        (and
          (eq ?task-node:type TASK)
          (eq ?action-uid ?task-node:task-action-uid)
          (eq (fact-slot-value ?op operation-tree-id)
              (behavior-tree-get-top-level-tree-id ?task-node:tree-id))
        )
        (bind ?active-actions (append$ ?active-actions
          (str-cat ?action:skill-id ":" ?action:state)))
    )
  )
  (do-for-all-facts ((?cei code-execution-instance))
    (and
      (isoneof ?cei:state PENDING RUNNING
                          CANCELATION-REQUESTED CANCELATION-PENDING CANCELING)
      (eq (fact-slot-value ?op name) ?cei:operation-name)
    )
    (bind ?active-actions (append$ ?active-actions
      (str-cat (code-execution-instance-to-string ?cei) ":" ?cei:state)))
  )

  (bind ?cur-state (fact-slot-value ?op state))
  (bind ?new-running-without-active-actions-count 0)
  (if (non-empty$ ?active-actions)
    then
      (printout t (str-cat "Operation: " (fact-slot-value ?op name)
        " State: " ?cur-state
        " Active actions: " (str-join " " ?active-actions)) crlf)
    else
      ; Only report on no active action, when one might expect running actions.
      (if (in$ ?cur-state (create$ RUNNING SUSPENDING CANCELING)) then
        (printout t (str-cat "Operation: " (fact-slot-value ?op name)
          " State: " ?cur-state " No active actions.") crlf)
        (bind ?new-running-without-active-actions-count
          (+ (fact-slot-value ?op running-without-active-actions-count) 1))
        (do-for-fact
          ((?process-tree behavior-tree))
          (and (eq (fact-slot-value ?op operation-tree-id) ?process-tree:id)
               (> ?new-running-without-active-actions-count 5))
          (assert (error (type RECOVERABLE)
                         (behavior-tree-id ?process-tree:id)
                         (name RUNNING-NO-ACTIVE-ACTIONS)
                         (message (str-cat "An operation is running, but "
                         "there are no active skills being executed. "
                         "This is an internal error."))))
          (facts)
          ; TODO(b/394318297) print the tree with expanded Anys.
          (if (<> (fact-slot-value ?op run-metadata-proto) 0) then
            (printout t (pb-tostring (fact-slot-value ?op run-metadata-proto)))
          )
        )
      )
  )

  (modify ?op (running-without-active-actions-count
                ?new-running-without-active-actions-count))
)

; Update the currently executed parameters in the state proto.
;
; Args:
;   ?state-proto: Proto id of the state proto
;   ?state-path: Proto path for this task node
;   ?parameter-proto: In the case of call_behavior must be the paramterized
;                     BehaviorCall proto, In the case of execute_code must be
;                     the paramterized Any proto.
(deffunction behavior-tree-task-node-update-state-params (?state-path
                                                          ?parameter-proto
                                                          ?operation-name
                                                          ?task-type)
  (switch ?task-type
    (case CALL-BEHAVIOR then
      (bind ?s-path-call-behavior
        (proto-path-join ?state-path "task.call_behavior"))
      (if (= ?parameter-proto 0)
       then
        (printout error "BehaviorTree proto task has set " ?s-path-call-behavior
                  "but no parameterized behavior call proto was generated" crlf)
       else
        (run-metadata-proto-sync-task-resources
          ?s-path-call-behavior ?parameter-proto ?operation-name)
        (run-metadata-proto-sync-task-skill-data
          ?s-path-call-behavior ?parameter-proto ?operation-name)
      )
    )
    (case EXECUTE-CODE then
      (return)
    )
    (default
      (printout error "BehaviorTree proto task does not have "
                ?state-path " set." crlf)
    )
  )
)

; Adds the parameters as a string attribute to the given span.
;
; Args:
;   ?node-span: span-id to add the attribute to.
;   ?proto-id: Proto to extract the parameters from. Can be a BehaviorCall proto
;              or an Any directly.
;   ?attribute: The name of the attribute to add
;   ?desc-pool-id: Pool id for converting the proto to string
(deffunction behavior-tree-task-node-try-add-params-to-span
  (?node-span ?proto-id ?attribute ?desc-pool-id)
  (if (= ?node-span ?*TRACING-INVALID-SPAN-ID*) then
    (return)
  )

  (bind ?proto-type (pb-get-type-name ?proto-id))
  (if (eq ?proto-type "intrinsic_proto.executive.BehaviorCall") then
   (if (pb-has-field ?proto-id "parameters") then
     (bind ?param-proto (pb-get-field ?proto-id "parameters"))
     (if (neq ?param-proto MISSING-FIELD) then
       (span-add-attribute ?node-span ?attribute
         (pb-tostring-with-pool ?param-proto ?desc-pool-id
                                ?*TASK-NODE-MAX-PARAM-PRINT-LINES*))
       (pb-remove ?param-proto)
     )
   )
  )
  (if (eq ?proto-type "google.protobuf.Any") then
    (span-add-attribute ?node-span ?attribute
      (pb-tostring-with-pool ?proto-id ?desc-pool-id
                             ?*TASK-NODE-MAX-PARAM-PRINT-LINES*))
  )
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-task-node-plan-action-start
  "For a ready task node, start the corresponding plan-action"
  ?tree <- (behavior-tree (id ?tree-id) (plan-id ?plan-id) (state RUNNING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state READY)
                               (task-action-prototype-uid ?action-prototype-uid)
                               (span-reference-id ?node-span-reference-id)
                               (run-metadata-proto-path ?run-metadata-proto-path))
  ?prototype-action <- (plan-action (id ?id) (uid ?action-prototype-uid)
                          (state ACCEPTED)
                          (skill-id ?skill-id)
                          (behavior-call-proto-id ?behavior-call-proto-id)
                          (footprint-proto ?footprint-proto-id))
 =>
  (bind ?new-behavior-call-proto-id 0)
  (if (<> ?behavior-call-proto-id 0) then
    (bind ?new-behavior-call-proto-id (pb-clone ?behavior-call-proto-id))
  )

  (if (<> ?new-behavior-call-proto-id 0) then
    (bind ?param-assign-result
      (action-parameterization-assign-behavior-call-proto
          ?new-behavior-call-proto-id ?bb-scope ?op-name))
    (if (not (result-ok ?param-assign-result)) then
      (pb-remove ?new-behavior-call-proto-id)

      (bind ?param-assign-es-proto (result-error ?param-assign-result))
      (bind ?es-proto (extended-status-create 11302))
      (extended-status-set-message ?es-proto USER
        (str-cat "Failed to parameterize skill " ?skill-id))
      (extended-status-set-related-to ?es-proto ?tree ?node)
      (extended-status-add-context ?es-proto ?param-assign-es-proto)

      (behavior-tree-set-node-failed ?node EXECUTION
        (str-cat "Failed to parameterize skill " ?skill-id)
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?param-assign-es-proto)
      (pb-remove ?es-proto)
      (return)
    )

    (bind ?equipment-assign-result
      (behavior-call-instance-parameterize-resources
        ?new-behavior-call-proto-id ?tree-id))
    (if (not (result-ok ?equipment-assign-result)) then
      (bind ?message (str-cat "Resource parameterization for skill '"
                        (pb-get-field ?new-behavior-call-proto-id
                          "skill_id")
                        "' failed: " (result-error ?equipment-assign-result)))
      (pb-remove ?new-behavior-call-proto-id)

      (bind ?es-proto (extended-status-create 11302))
      (extended-status-set-message ?es-proto USER ?message)
      (extended-status-set-related-to ?es-proto ?tree ?node)

      (behavior-tree-set-node-failed ?node EXECUTION ?message
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?es-proto)
      (return)
    )
  )

  (bind ?new-action-uid (plan-action-uid ?plan-id ?id))

  (bind ?new-footprint-proto-id 0)
  (if (<> ?footprint-proto-id 0) then
    (bind ?new-footprint-proto-id (pb-clone ?footprint-proto-id)))

  (if (neq ?run-metadata-proto-path "") then
    (behavior-tree-task-node-update-state-params ?run-metadata-proto-path
                                                 ?new-behavior-call-proto-id
                                                 ?op-name
                                                 CALL-BEHAVIOR))

  (bind ?new-action (duplicate ?prototype-action (plan-id ?plan-id)
                                       (uid ?new-action-uid)
                                       (state SELECTED)
                                       (behavior-call-proto-id
                                         ?new-behavior-call-proto-id)
                                       (footprint-proto ?new-footprint-proto-id)
                                       (execution-select-time (now))))

  ; By default we do not embed action spans,
  ; thus creating a new trace for each action execution
  (bind ?action-parent-span ?*TRACING-INVALID-SPAN-ID*)
  (if (eq (operation-get-skill-trace-handling-by-tree ?tree-id) EMBED) then
    (bind ?action-parent-span ?node-span-reference-id)
  )
  (bind ?action-span
    (tracing-start-action-span ?new-action ?action-parent-span))
  (bind ?new-action (modify ?new-action (span-reference-id ?action-span)))
  (if (eq (operation-get-skill-trace-handling-by-tree ?tree-id) LINK) then
    (tracing-link-skill-trace ?node ?new-action)
  )

  (do-for-fact ((?si skill-info)) (eq ?si:skill-id ?skill-id)
    (printout t "Parameterized '" ?skill-id "' with parameters:" crlf
              (pb-tostring-with-pool ?new-behavior-call-proto-id
                                     ?si:parameter-descriptor-pool-id
                                     ?*TASK-NODE-MAX-PARAM-PRINT-LINES*) crlf)
  )

  (modify ?node (state RUNNING)
                (task-action-uid ?new-action-uid))
)

(defrule behavior-tree-task-node-behavior-call-start
  "For a ready task node, start the corresponding behavior-call"
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state READY)
                               (behavior-call-instance-uid ?bci-uid)
                               (run-metadata-proto-path ?run-metadata-proto-path))
  ?bci <- (behavior-call-instance (uid ?bci-uid)
                          (state ACCEPTED)
                          (behavior-call-prototype-proto ?prototype-proto))
 =>
  (bind ?new-behavior-call-instance-proto 0)
  (if (<> ?prototype-proto 0) then
    (bind ?new-behavior-call-instance-proto (pb-clone ?prototype-proto))
  )

  (if (<> ?new-behavior-call-instance-proto 0) then
    (bind ?param-assign-result
      (action-parameterization-assign-behavior-call-proto
        ?new-behavior-call-instance-proto ?bb-scope ?op-name))
    (if (not (result-ok ?param-assign-result)) then
      (bind ?message
        (str-cat "Failed to parameterize behavior tree "
                 (pb-get-field ?new-behavior-call-instance-proto "skill_id")))
      (pb-remove ?new-behavior-call-instance-proto)

      (bind ?param-assign-es-proto (result-error ?param-assign-result))
      (bind ?es-proto (extended-status-create 11302))
      (extended-status-set-message ?es-proto USER ?message)
      (extended-status-set-related-to ?es-proto ?tree ?node)
      (extended-status-add-context ?es-proto ?param-assign-es-proto)

      (behavior-tree-set-node-failed ?node EXECUTION ?message
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?param-assign-es-proto)
      (pb-remove ?es-proto)
      (return)
    )

    (bind ?equipment-assign-result
      (behavior-call-instance-parameterize-resources
        ?new-behavior-call-instance-proto ?tree-id))
    (if (not (result-ok ?equipment-assign-result)) then
      (bind ?message (str-cat "Resource parameterization for behavior tree '"
                        (pb-get-field ?new-behavior-call-instance-proto
                          "skill_id")
                        "' failed: " (result-error ?equipment-assign-result)))
      (pb-remove ?new-behavior-call-instance-proto)

      (bind ?es-proto (extended-status-create 11302))
      (extended-status-set-message ?es-proto USER ?message)
      (extended-status-set-related-to ?es-proto ?tree ?node)

      (behavior-tree-set-node-failed ?node EXECUTION ?message
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?es-proto)
      (return)
    )
  )

  (if (neq ?run-metadata-proto-path "") then
    (behavior-tree-task-node-update-state-params ?run-metadata-proto-path
                                                 ?new-behavior-call-instance-proto
                                                 ?op-name
                                                 CALL-BEHAVIOR))


  (bind ?skill-id  (pb-get-field ?new-behavior-call-instance-proto "skill_id"))
  (do-for-fact ((?si skill-info)) (eq ?si:skill-id ?skill-id)
    (printout t "Executing '" ?skill-id "' with parameters:" crlf
              (pb-tostring-with-pool ?new-behavior-call-instance-proto
                                     ?si:parameter-descriptor-pool-id
                                     ?*TASK-NODE-MAX-PARAM-PRINT-LINES*) crlf)
  )

  (modify ?bci (state SELECTED)
               (behavior-call-instance-proto ?new-behavior-call-instance-proto))
  (modify ?node (state RUNNING))
)

(defrule behavior-tree-task-node-code-execution-start
  "For a ready task node, start the corresponding code-execution-instance"
  ?tree <- (behavior-tree (id ?tree-id) (operation-name ?op-name)
                          (state RUNNING)
                          (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type EXECUTE-CODE)
                               (state READY)
                               (span-reference-id ?node-span-reference-id)
                               (run-metadata-proto-path ?run-metadata-proto-path))
  ?cei <- (code-execution-instance (operation-name ?op-name) (tree-id ?tree-id)
                                   (node-id ?node-id)
                                   (state ACCEPTED)
                                   (parameters-prototype-proto ?param-proto)
                                   (parameter-message-full-name
                                     ?param-message-name)
                                   (file-descriptor-set-pool
                                     ?file-descriptor-set-pool-id))
 =>
  (bind ?assigned-params-proto 0)
  (if (neq ?param-message-name "") then
    (if (= ?file-descriptor-set-pool-id 0) then
      (bind ?cei-es-proto (extended-status-create 13701 ERROR))
      (extended-status-set-message ?cei-es-proto USER
        (str-cat "Code Execution: No Pool available for parameters on: "
          (code-execution-instance-to-string ?cei)))
      (extended-status-set-related-to ?cei-es-proto ?tree ?node)
      (behavior-tree-set-node-failed ?node EXECUTION "No pool available"
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?cei-es-proto)
      (pb-remove ?cei-es-proto)
      (return)
    )

    (bind ?param-assign-result
      (action-parameterization-assign-any-with-assignments ?param-proto ?bb-scope
        ?op-name ?file-descriptor-set-pool-id ?param-message-name))
    (if (not (result-ok ?param-assign-result)) then
      (bind ?param-assign-es-proto (result-error ?param-assign-result))

      (bind ?es-proto (extended-status-create 11302))
      (extended-status-set-message ?es-proto USER
        "Failed to parameterize script node")
      (extended-status-set-related-to ?es-proto ?tree ?node)
      (extended-status-add-context ?es-proto ?param-assign-es-proto)

      (behavior-tree-set-node-failed ?node EXECUTION
        "Code execution parameterization failed"
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?param-assign-es-proto)
      (pb-remove ?es-proto)
      (return)
    )
    (bind ?assigned-params-proto (result-value ?param-assign-result))
  )

  (if (neq ?run-metadata-proto-path "") then
    (behavior-tree-task-node-update-state-params ?run-metadata-proto-path
                                                 ?assigned-params-proto
                                                 ?op-name
                                                 EXECUTE-CODE))

  (bind ?msg (str-cat "Executing '" (code-execution-instance-to-string ?cei) "'"))
  (if (<> ?assigned-params-proto 0) then
    (bind ?msg (format nil "%s with parameters:%n%s"
        ?msg (pb-tostring-with-pool ?assigned-params-proto
                                    ?file-descriptor-set-pool-id
                                    ?*TASK-NODE-MAX-PARAM-PRINT-LINES*)))
  )
  (printout t ?msg crlf)

  (bind ?action-parent-span ?*TRACING-INVALID-SPAN-ID*)
  (if (eq (operation-get-skill-trace-handling-by-tree ?tree-id) EMBED) then
    (bind ?action-parent-span ?node-span-reference-id)
  )
  (bind ?action-span
    (tracing-start-code-execution-span ?cei ?action-parent-span))
  (bind ?cei (modify ?cei (state SELECTED)
                          (parameters-instance-proto ?assigned-params-proto)
                          (span-reference-id ?action-span)))
  (if (eq (operation-get-skill-trace-handling-by-tree ?tree-id) LINK) then
    (tracing-link-code-execution-trace ?node ?cei)
  )
  (modify ?node (state RUNNING))
)

; This rule is triggered on resume for actions that were reset to ACCEPTED
; during suspension (specifically, actions that were in FOOTPRINT-CONFLICT-WAITING).
; It transitions them back to SELECTED, starting the projection/conflict check from scratch.
(defrule behavior-tree-task-node-restart-action
  "For a selected task node, select the corresponding plan-action if ACCEPTED"
  (behavior-tree (id ?tree-id) (plan-id ?plan-id) (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type TASK) (task-type CALL-BEHAVIOR)
                      (state RUNNING) (task-action-uid ?action-uid))
  ?action <- (plan-action (uid ?action-uid) (state ACCEPTED) (plan-id ?plan-id))
 =>
  (modify ?action (state SELECTED))
)

(defrule behavior-tree-task-node-plan-action-succeeded
  "The plan-action has succeeded, let the node succeed"
  (behavior-tree (id ?tree-id) (plan-id ?plan-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state RUNNING|CANCELING)
                               (task-action-uid ?action-uid)
                               (span-reference-id ?node-span))
  (plan-action (uid ?action-uid) (plan-id ?plan-id) (state SUCCEEDED)
               (skill-id ?skill-id) (behavior-call-proto-id ?bc-proto-id))
  (skill-info (skill-type SKILL) (skill-id ?skill-id)
              (parameter-descriptor-pool-id ?param-desc-pool-id))
 =>
  (behavior-tree-task-node-try-add-params-to-span
    ?node-span ?bc-proto-id "action_parameters" ?param-desc-pool-id)

  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-task-node-noop-action-succeeded
  "The noop plan-action has succeeded, let the node succeed"
  (behavior-tree (id ?tree-id) (plan-id ?plan-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state RUNNING|CANCELING)
                               (task-action-uid ?action-uid)
                               (span-reference-id ?node-span))
  (plan-action (uid ?action-uid) (plan-id ?plan-id) (state SUCCEEDED)
               (skill-id ?skill-id))
  (noop-action-info (noop-action-names $? ?skill-id $?))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-task-node-behavior-call-succeeded
  "The behavior-call-instance has succeeded, let the node succeed"
  (behavior-tree (id ?tree-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state RUNNING|CANCELING)
                               (behavior-call-instance-uid ?bci-uid)
                               (span-reference-id ?node-span))
  (behavior-call-instance (uid ?bci-uid) (state SUCCEEDED)
                          (behavior-call-instance-proto ?proto-id&~0))
  (skill-info (skill-id ?skill-id
                        &:(eq ?skill-id (pb-get-field ?proto-id "skill_id")))
              (parameter-descriptor-pool-id ?param-desc-pool-id))
 =>
  (behavior-tree-task-node-try-add-params-to-span
    ?node-span ?proto-id "behavior_call_parameters" ?param-desc-pool-id)

  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-task-node-code-execution-succeeded
  "The code-execution-instance has succeeded, let the node succeed"
  (behavior-tree (id ?tree-id) (operation-name ?op-name)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type EXECUTE-CODE)
                               (state RUNNING|CANCELING)
                               (span-reference-id ?node-span))
  (code-execution-instance (operation-name ?op-name) (tree-id ?tree-id)
                           (node-id ?node-id)
                           (state SUCCEEDED)
                           (parameters-instance-proto ?param-proto)
                           (file-descriptor-set-pool ?desc-pool-id))
 =>
  (behavior-tree-task-node-try-add-params-to-span
    ?node-span ?param-proto "code_execution_parameters" ?desc-pool-id)

  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-task-node-code-execution-failed
  "The code-execution-instance has failed, let the node fail"
  (behavior-tree (id ?tree-id) (operation-name ?op-name)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type EXECUTE-CODE)
                               (state RUNNING|CANCELING)
                               (span-reference-id ?node-span))
  (code-execution-instance (operation-name ?op-name) (tree-id ?tree-id)
                           (node-id ?node-id)
                           (state FAILED)
                           (extended-status-proto ?es-proto)
                           (parameters-instance-proto ?param-proto)
                           (file-descriptor-set-pool ?desc-pool-id))
 =>
  (behavior-tree-task-node-try-add-params-to-span
    ?node-span ?param-proto "code_execution_parameters" ?desc-pool-id)

  (behavior-tree-set-node-failed ?node EXECUTION "Code execution failed"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
)

(defrule behavior-tree-task-node-plan-action-failed
  "The plan-action failed, let the node fail"
  (behavior-tree (id ?tree-id) (plan-id ?plan-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state RUNNING|CANCELING)
                               (task-action-uid ?action-uid)
                               (span-reference-id ?node-span))
  (plan-action (uid ?action-uid) (plan-id ?plan-id) (state FAILED)
               (skill-id ?skill-id) (behavior-call-proto-id ?bc-proto-id)
               (extended-status-proto-id ?es-proto-id))
 =>
  (do-for-fact ((?si skill-info))
      (and (eq ?si:skill-type SKILL) (eq ?si:skill-id ?skill-id))
    (behavior-tree-task-node-try-add-params-to-span
      ?node-span ?bc-proto-id "action_parameters"
      ?si:parameter-descriptor-pool-id)
  )

  (behavior-tree-set-node-failed ?node EXECUTION "Action failed"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto-id)
)

(defrule behavior-tree-task-node-plan-action-failed-on-invalid-cancel
  "The plan-action is canceled but wasn't canceling, let the node fail"
  ?tree <- (behavior-tree (id ?tree-id) (plan-id ?plan-id))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state ?state&~CANCELING&~FAILED&~CANCELED)
                               (task-action-uid ?action-uid)
                               (span-reference-id ?node-span))
  (plan-action (uid ?action-uid) (plan-id ?plan-id) (state CANCELED)
               (skill-id ?skill-id) (behavior-call-proto-id ?bc-proto-id))
  (skill-info (skill-type SKILL) (skill-id ?skill-id)
              (parameter-descriptor-pool-id ?param-desc-pool-id))
 =>
  (behavior-tree-task-node-try-add-params-to-span
    ?node-span ?bc-proto-id "action_parameters" ?param-desc-pool-id)

  (bind ?es-proto (extended-status-create 31500))
  (bind ?message (str-cat "Unexpected cancelled report from skill execution "
    "while the task node is in state " ?state))
  (extended-status-set-message ?es-proto USER ?message)
  (extended-status-set-related-to ?es-proto ?tree ?node)

  (behavior-tree-set-node-failed ?node EXECUTION ?message
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
  (pb-remove ?es-proto)
)

(defrule behavior-tree-task-node-plan-action-canceled
  "The plan-action has been canceled, mark node canceled"
  (behavior-tree (id ?tree-id) (plan-id ?plan-id))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state CANCELING)
                               (task-action-uid ?action-uid)
                               (span-reference-id ?node-span))
  (plan-action (uid ?action-uid) (plan-id ?plan-id) (state CANCELED)
               (skill-id ?skill-id) (behavior-call-proto-id ?bc-proto-id))
  (skill-info (skill-type SKILL) (skill-id ?skill-id)
              (parameter-descriptor-pool-id ?param-desc-pool-id))
 =>
  (behavior-tree-task-node-try-add-params-to-span
    ?node-span ?bc-proto-id "action_parameters" ?param-desc-pool-id)

  (behavior-tree-set-node-canceled ?node)
)

(defrule behavior-tree-task-node-noop-action-failed
  "The noop plan-action failed, let the node fail"
  ?tree <- (behavior-tree (id ?tree-id) (plan-id ?plan-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state RUNNING|CANCELING)
                               (task-action-uid ?action-uid)
                               (span-reference-id ?node-span))
  (plan-action (uid ?action-uid) (plan-id ?plan-id) (state FAILED)
               (skill-id ?skill-id))
  (noop-action-info (noop-action-names $? ?skill-id $?))
 =>
  (bind ?es-proto (extended-status-create 31500))
  (bind ?message (str-cat "Noop action " ?skill-id " failed unexpectedly"))
  (extended-status-set-message ?es-proto USER ?message)
  (extended-status-set-related-to ?es-proto ?tree ?node)

  (behavior-tree-set-node-failed ?node EXECUTION ?message
    EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
  (pb-remove ?es-proto)
)

(defrule behavior-tree-task-node-behavior-call-failed
  "The behavior-call-instance failed, let the node fail"
  (behavior-tree (id ?tree-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state RUNNING|CANCELING)
                               (behavior-call-instance-uid ?bci-uid)
                               (span-reference-id ?node-span))
  (behavior-call-instance (uid ?bci-uid) (state FAILED)
                          (behavior-call-instance-proto ?proto-id&~0)
                          (extended-status-proto-id ?es-context-proto))
  (skill-info (skill-id ?skill-id
                        &:(eq ?skill-id (pb-get-field ?proto-id "skill_id")))
              (parameter-descriptor-pool-id ?param-desc-pool-id))
 =>
  (if (<> ?node-span ?*TRACING-INVALID-SPAN-ID*) then
    (if (pb-has-field ?proto-id "parameters") then
      (bind ?param-proto (pb-get-field ?proto-id "parameters"))
      (if (neq ?param-proto MISSING-FIELD) then
        (span-add-attribute ?node-span "behavior_call_parameters"
          (pb-tostring-with-pool ?param-proto ?param-desc-pool-id
                                 ?*TASK-NODE-MAX-PARAM-PRINT-LINES*))
        (pb-remove ?param-proto)
      )
    )
  )

  (behavior-tree-set-node-failed
    ?node EXECUTION "Behavior Call failed"
    EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-context-proto)
)

; Suspend/Resume for task nodes currently works as follows:
; When the behavior-tree is SUSPENDING:
; * For states before RUNNING suspending is handled by generic rules.
; * RUNNING task-node with plan-action: It is only SUSPENDED once then
;   underlying plan-action is in a settled state (i.e., not PROJECTING/RUNNING)
;   -> Task node does not actively suspend, but waits for the plan-action
; * plan-action is responsible to only project/start in a RUNNING tree
; * RUNNING task-node with behavior-call-instance: Switch behavior-call-instance
;   to SUSPENDING and transition task-node to SUSPENDED, when
;   behavior-call-instance is SUSPENDED
;
; When the behavior-tree is RUNNING, resume works top-down, i.e., a node is
; resumed if its parent is RUNNING again.
; * States before RUNNING are restored from suspended-from-state.
; * SUSPENDED task-node with plan-action: Move task-node back to RUNNING state
; * plan-action is responsible to resume operation
; * SUSPENDED task-node with behavior-call-instance: Move task-node back to
;   RUNNING state and behavior-call-instance back to RUNNING

(defrule behavior-tree-task-node-suspended
  "Suspends the node when the action is in a compatible state."
  (behavior-tree (id ?tree-id) (plan-id ?plan-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state ?state&RUNNING)
                               (task-action-uid ?action-uid))
  ?action <- (plan-action (uid ?action-uid) (plan-id ?plan-id)
                          (state ?action-state&ACCEPTED|SELECTED|PROJECTED|READY|FOOTPRINT-CONFLICT-WAITING))
 =>
  ; Suspend/resume of a node in FOOTPRINT-CONFLICT-WAITING resets the action,
  ; i.e., we remove all waiting dependencies here and put the action back to
  ; ACCEPTED. On resume everything will be re-run and re-checked.
  (delayed-do-for-all-facts ((?dep action-footprint-dependency))
      (eq ?dep:waiting-action-uid ?action-uid)
    (retract ?dep)
  )
  ; Reset back to ACCEPTED to start the action from scratch (predict/get
  ; footprint) when resumed.
  (if (eq ?action-state FOOTPRINT-CONFLICT-WAITING) then
    (modify ?action (state ACCEPTED))
  )
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-task-node-resume
  "Resumes the node when the tree is running again."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?id) (tree-id ?tree-id) (parent-id ?parent-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (state SUSPENDED)
                               (task-action-uid ?action-uid)
                               (behavior-call-instance-uid nil)
                               (suspended-from-state ?resume-state))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-task-node-behavior-call-suspend
  "Initiate suspending the behavior call instance."
  (declare (salience ?*SALIENCE-SUSPENDING*))
  (behavior-tree (state SUSPENDING) (id ?tree-id))
  (behavior-tree-node (type TASK) (task-type CALL-BEHAVIOR)
                      (state RUNNING) (tree-id ?tree-id)
                      (behavior-call-instance-uid ?bci-uid))
  ?bci <- (behavior-call-instance (uid ?bci-uid) (state RUNNING))
 =>
  (modify ?bci (state SUSPENDING))
)

(defrule behavior-tree-task-node-behavior-call-suspended
  "Transition node to SUSPENDED when the behavior-call-instance is suspended."
  (behavior-tree (state SUSPENDING) (id ?tree-id))
  ?node <- (behavior-tree-node (type TASK) (task-type CALL-BEHAVIOR)
                      (state ?state&RUNNING)
                      (tree-id ?tree-id) (behavior-call-instance-uid ?bci-uid))
  (behavior-call-instance (uid ?bci-uid) (state SUSPENDED))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-task-node-behavior-call-resume
  "Resumes the node and behavior-call-instance when the tree is running again."
  (behavior-tree (state RUNNING) (id ?tree-id) (start-node-id ?start-node-id))
  ?node <- (behavior-tree-node (id ?id) (type TASK) (task-type CALL-BEHAVIOR)
                      (state SUSPENDED)
                      (tree-id ?tree-id) (behavior-call-instance-uid ?bci-uid)
                      (suspended-from-state ?resume-state)
                      (parent-id ?parent-id))
  ?bci <- (behavior-call-instance (uid ?bci-uid)
                                  (state ?bci-state&ACCEPTED|SUSPENDED))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
  (if (eq ?bci-state SUSPENDED) then
    (modify ?bci (state RUNNING))
  )
)

(defrule behavior-tree-task-node-code-execution-resume
  "Resumes the node and code-execution-instance when the tree is running again."
  (behavior-tree (state RUNNING) (id ?tree-id) (start-node-id ?start-node-id)
                 (operation-name ?op-name))
  ?node <- (behavior-tree-node (id ?id) (type TASK) (task-type EXECUTE-CODE)
                      (state SUSPENDED)
                      (tree-id ?tree-id)
                      (suspended-from-state ?resume-state)
                      (parent-id ?parent-id))
  ?cei <- (code-execution-instance (node-id ?id) (tree-id ?tree-id)
                                   (operation-name ?op-name)
                                   (state ?cei-state&ACCEPTED))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-task-node-cancel-action
  "Requests cancellation of a running action.
  The action executor will transition the action from CANCELLATION-REQUESTED to
  CANCELLATION-PENDING when it triggers cancellation."
  (declare (salience ?*SALIENCE-CANCELING*))
  (behavior-tree-node (type TASK) (task-type CALL-BEHAVIOR)
                      (task-action-uid ?action-uid)
                      (state CANCELING))
  ?action <- (plan-action (uid ?action-uid) (state RUNNING))
 =>
  (modify ?action (state CANCELLATION-REQUESTED))
)

(defrule behavior-tree-task-node-canceled-action
  "Cancels the node if the action is waiting."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?node <- (behavior-tree-node (type TASK) (task-type CALL-BEHAVIOR)
                               (task-action-uid ?action-uid)
                               (state CANCELING))
  ?action <- (plan-action (uid ?action-uid)
                          (state ACCEPTED|SELECTED|PROJECTED|READY|FOOTPRINT-CONFLICT-WAITING|CANCELED))
 =>
  (delayed-do-for-all-facts ((?dep action-footprint-dependency))
      (eq ?dep:waiting-action-uid ?action-uid)
    (retract ?dep)
  )
  (modify ?action (state CANCELED))
  (behavior-tree-set-node-canceled ?node)
)

(defrule behavior-tree-task-node-cancel-without-running-action
  "Cancels the node if there is no running action."
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type CALL-BEHAVIOR)
                               (task-action-uid nil)
                               (behavior-call-instance-uid nil)
                               (state CANCELING))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  (not (code-execution-instance (node-id ?node-id) (tree-id ?tree-id)
                                (operation-name ?op-name)))
 =>
  (behavior-tree-set-node-canceled ?node)
)

(defrule behavior-tree-task-node-cancel-behavior-call-waiting-instance
  "Cancels the node if the behavior-call-instance is waiting."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?node <- (behavior-tree-node (type TASK) (task-type CALL-BEHAVIOR)
                               (behavior-call-instance-uid ?bci-uid)
                               (state CANCELING))
  ?bci <- (behavior-call-instance (uid ?bci-uid)
                                  (state ACCEPTED|SELECTED))
 =>
  (behavior-tree-set-node-canceled ?node)
  (modify ?bci (state ACCEPTED))
)

(defrule behavior-tree-task-node-cancel-behavior-call-running-instance
  "Requests cancellation of a running behavior-call-instance."
  (declare (salience ?*SALIENCE-CANCELING*))
  (behavior-tree-node (type TASK) (task-type CALL-BEHAVIOR)
                      (behavior-call-instance-uid ?bci-uid)
                      (state CANCELING))
  ?bci <- (behavior-call-instance (uid ?bci-uid)
                                  (state RUNNING|SUSPENDING|SUSPENDED))
 =>
  (modify ?bci (state CANCELLATION-REQUESTED))
)

(defrule behavior-tree-task-node-canceled-behavior-call
  "Cancels the node if the action is waiting."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?node <- (behavior-tree-node (type TASK) (task-type CALL-BEHAVIOR)
                               (behavior-call-instance-uid ?bci-uid)
                               (state CANCELING))
  (behavior-call-instance (uid ?bci-uid) (state CANCELED))
 =>
  (behavior-tree-set-node-canceled ?node)
)

(defrule behavior-tree-task-node-cancel-code-execution-waiting-instance
  "Cancels the node if the code-execution-instance is waiting."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type EXECUTE-CODE)
                               (state CANCELING))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  ?cei <- (code-execution-instance (node-id ?node-id) (tree-id ?tree-id)
                                   (operation-name ?op-name)
                                   (state ?cei-state&ACCEPTED|SELECTED)
                                   (span-reference-id ?span-id))
 =>
  (if (eq ?cei-state SELECTED) then
    (span-end ?span-id)
  )
  (behavior-tree-set-node-canceled ?node)
  (modify ?cei (state ACCEPTED) (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-task-node-cancel-code-execution-running-instance
  "Requests cancellation of a running code-execution-instance."
  (declare (salience ?*SALIENCE-CANCELING*))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type TASK) (task-type EXECUTE-CODE)
                      (state CANCELING))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  ?cei <- (code-execution-instance (node-id ?node-id) (tree-id ?tree-id)
                                   (operation-name ?op-name)
                                   (state RUNNING))
 =>
  (modify ?cei (state CANCELATION-REQUESTED))
)

(defrule behavior-tree-task-node-canceled-code-execution
  "Cancels the node if the action is waiting."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type TASK) (task-type EXECUTE-CODE)
                               (state CANCELING)
                               (span-reference-id ?node-span))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  (code-execution-instance (node-id ?node-id) (tree-id ?tree-id)
                           (operation-name ?op-name)
                           (state CANCELED)
                           (parameters-instance-proto ?param-proto)
                           (file-descriptor-set-pool ?desc-pool-id))
 =>
  (behavior-tree-task-node-try-add-params-to-span
    ?node-span ?param-proto "code_execution_parameters" ?desc-pool-id)

  (behavior-tree-set-node-canceled ?node)
)
