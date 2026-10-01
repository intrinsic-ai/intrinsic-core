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

; Handling changing and updating an operation's process tree.

; Requires:
; - operation.clp: operation-envelope and update facts
; - blackboard.clp: blackboard access to publish process state

; ---------------------- FORWARD DECLARATIONS ---------------------------------

; defined in stepwise.clp
(deffunction behavior-tree-stepwise-step-through-tree (?top-tree-id))
; defined in task_node.clp
(deffunction behavior-tree-task-node-print-active-actions (?op))

; --------------------------------- FUNCTIONS ---------------------------------

; Helper functions to get attributes from the state of the current operation.

(deffunction operation-execution-mode-to-proto-mode (?mode)
  (return (sym-cat EXECUTION_MODE_ (str-replace-all ?mode "-" "_")))
)

(deffunction operation-sim-mode-to-proto-mode (?mode)
  (return (sym-cat SIMULATION_MODE_ (str-replace-all ?mode "-" "_")))
)

(deffunction operation-skill-trace-handling-to-proto-mode (?mode)
  (return (sym-cat SKILL_TRACES_ ?mode))
)

; Functions that interact with the current operation's process tree

; Resets the operation including its process tree.
;
; Args:
;   ?operation-name: the operation to reset
;   ?keep-blackboard: if TRUE, then the blackboard is not flushed on reset.
;
; Returns:
;   A multifield-pair with TRUE/FALSE and as the second value an optional error
;   message.
(deffunction operation-reset (?operation-name ?keep-blackboard)
  (do-for-fact ((?op operation-envelope))
    (eq ?op:name ?operation-name)

    (if (not (in$ ?op:state ?*OPERATION-WAITING-STATES*)) then
      (return (result-create FALSE
                (str-cat "Cannot reset operation in state " ?op:state)))
    )

    (if (neq ?op:span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
      (span-end-failure ?op:span-reference-id ?*TRACING-STATUS-ABORTED*
          "Aborting active span as the operation is being deleted.")
    )

    (if (not ?keep-blackboard) then
      (bind ?bb-scopes
        (behavior-tree-get-contained-blackboard-scopes ?op:operation-tree-id))
      (foreach ?bb-scope ?bb-scopes
        (blackboard-flush ?bb-scope ?operation-name)
      )
      (pubsub-clear-kv-capture-results)
    )

    (pb-remove ?op:parameter-proto)
    (pb-remove ?op:return-value-proto)
    (pb-remove ?op:recovery-state-proto)

    (behavior-tree-reset ?op:operation-tree-id ?keep-blackboard)

    (do-for-fact ((?tm time-measurement))
      (eq ?tm:operation-name ?operation-name)
      (retract ?tm)
    )

    (conductor-preparation-client-operation-delete ?operation-name)

    (pb-remove ?op:extended-status-proto-id)

    (modify ?op (state ACCEPTED) (trace-id "") (trace-url "")
      (parameter-proto 0) (resources (create$ )) (return-value-proto 0)
      (span-reference-id ?*TRACING-INVALID-SPAN-ID*) (scene-id "")
      (extended-status-proto-id 0) (recovery-state-proto 0)
      (skill-instances-are-reset FALSE))
  )
  (return (result-create TRUE ""))
)

; Deletes the operation and its associate behavior tree
;
; Returns:
;   A multifield-pair with TRUE/FALSE and as the second value an optional error
;   message.
(deffunction operation-delete (?operation-name)
  "Remove the operation and its process behavior tree (and related facts)"

  (pubsub-clear-kv-capture-results)

  (do-for-fact ((?op operation-envelope) (?bt behavior-tree))
    (and (eq ?op:name ?operation-name) (eq ?op:operation-tree-id ?bt:id))

    (if (not (in$ ?op:state ?*OPERATION-WAITING-STATES*)) then
      (return (result-create FALSE
                (str-cat "Cannot delete operation in state " ?op:state)))
    )

    (if (neq ?op:span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
      (span-end-failure ?op:span-reference-id ?*TRACING-STATUS-ABORTED*
          "Aborting active span as the operation is being deleted.")
    )
    (pb-remove ?op:parameter-proto)

    (if (<> ?op:parameter-descriptor-pool 0) then
      (pb-remove-descriptor-pool ?op:parameter-descriptor-pool)
    )
    (pb-remove ?op:parameter-descriptor-set-proto)
    (pb-remove ?op:return-value-proto)
    (pb-remove ?op:recovery-state-proto)

    (if (<> ?op:return-value-descriptor-pool 0) then
      (pb-remove-descriptor-pool ?op:return-value-descriptor-pool)
    )
    (pb-remove ?op:return-value-descriptor-set-proto)

    (blackboard-remove-operation-protos ?operation-name)

    (behavior-tree-delete ?bt:id)

    (conductor-preparation-client-operation-delete ?operation-name)

    (pb-remove ?op:operation-proto)
    (pb-remove ?op:run-metadata-proto)
    (pb-remove ?op:extended-status-proto-id)

    (pb-remove-operation-protos-and-pools ?operation-name)

    (retract ?op)
  )
  (return (result-create TRUE ""))
)

; Abandons all ongoing skill calls in the given operation.
(deffunction operation-abandon (?operation-name)
  (do-for-all-facts ((?action plan-action))
    (isoneof ?action:state RUNNING
                           CANCELLATION-REQUESTED CANCELLATION-PENDING
                           CANCELING CANCELING-EXECUTION-TIMEOUT)
    (bind ?action-uid ?action:uid)
    (do-for-fact
        ((?task-node behavior-tree-node) (?tree behavior-tree))
        (and
          (eq ?task-node:type TASK)
          (eq ?action-uid ?task-node:task-action-uid)
          (eq ?tree:id ?task-node:tree-id)
          (eq ?tree:operation-name ?operation-name)
        )
        (skill-abandon-call-async ?action-uid)
    )
  )
)

; Find the operation's fact address for a given tree id.
;
; Args:
;   ?tree-id: Id of any behavior tree. This need not be a top-level tree.
;
; Returns:
;   The fact address of the operation if ?tree-id is in the tree of an
;   operation. Otherwise nil is returned.
(deffunction operation-get-by-tree (?tree-id)
  (do-for-fact ((?bt behavior-tree) (?op operation-envelope))
      (and (eq ?bt:id ?tree-id) (eq ?bt:operation-name ?op:name))
    (return ?op)
  )
  (return nil)
)

; Find the simulation-mode for a given tree id.
;
; Args:
;   ?tree-id: Id of any behavior tree. This need not be a top-level tree.
;
; Returns:
;   The simulation mode symbol as specified in the simulation-mode slot.
(deffunction operation-get-simulation-mode-by-tree (?tree-id)
  (do-for-fact ((?bt behavior-tree) (?op operation-envelope))
      (and (eq ?bt:id ?tree-id) (eq ?bt:operation-name ?op:name))
    (return ?op:simulation-mode)
  )
  (return REALITY)
)

; Find the skill-trace-handling for a given tree id.
;
; Args:
;   ?tree-id: Id of any behavior tree. This need not be a top-level tree.
;
; Returns:
;   The skill-trace-handling symbol as specified in operation-envelope.
(deffunction operation-get-skill-trace-handling-by-tree (?tree-id)
  (do-for-fact ((?bt behavior-tree) (?op operation-envelope))
      (and (eq ?bt:id ?tree-id) (eq ?bt:operation-name ?op:name))
    (return ?op:skill-trace-handling)
  )
  (return UNSPECIFIED)
)

; Find the execution span for a given tree id.
;
; Args:
;   ?tree-id: Id of any behavior tree. This need not be a top-level tree.
;
; Returns:
;   The current span from the execution of the operation.
;   This can be ?*TRACING-INVALID-SPAN-ID*, if it is not running.
;
; This function assumes that the operation-envelope exists.
(deffunction operation-get-execution-span-by-tree (?tree-id)
  (do-for-fact ((?bt behavior-tree) (?op operation-envelope))
      (and (eq ?bt:id ?tree-id) (eq ?bt:operation-name ?op:name))
    (return ?op:span-reference-id)
  )
  (return ?*TRACING-INVALID-SPAN-ID*)
)

; Check if all operations are in a waiting state.
;
; Returns:
;   TRUE if all operations are in a waiting state, FALSE otherwise.
(deffunction operation-are-all-waiting ()
  (return (not (any-factp ((?op operation-envelope))
                          (not (in$ ?op:state ?*OPERATION-WAITING-STATES*)))))
)

; Attaches an extended status proto as the extended status of the operation.
;
; This creates a new extended-status-proto and attaches it on operation.
; Usually there is no extended status on the operation. In this case a clone of
; the input proto is set as the extended status proto for the operation. If
; there already is an extended-status, the given proto is added to its context.
;
; This function might invalidate the input ?op fact.
;
; Args:
;   ?op: Fact address of an operation
;   ?extended-status-proto: proto id of an ExtendedStatus (can be 0). This
;     function does not take ownership of that proto.
;
; Returns:
;   Fact-address of the (possibly new) operation-envelope fact.
(deffunction operation-attach-extended-status (?op ?extended-status-proto)
  (if (= ?extended-status-proto 0) then
    (return ?op)
  )

  (bind ?current-es (fact-slot-value ?op extended-status-proto-id))
  (if (<> ?current-es 0) then
    (extended-status-add-context ?current-es ?extended-status-proto)
    (return ?op)
  )

  (bind ?new-es (pb-clone ?extended-status-proto))
  (bind ?new-op (modify ?op (extended-status-proto-id ?new-es)))
  (return ?new-op)
)


; ----------------------------------- RULES -----------------------------------

(defrule operation-state-update
  "Transition to new operation state on request"
  (declare (salience ?*SALIENCE-HIGH*))
  ?of <- (operation-envelope (name ?operation-name)
                             (state ?current-state)
                             (span-reference-id ?current-span)
                             (trace-id ?trace-id)
                             (trace-url ?trace-url)
                             (operation-tree-id ?operation-tree-id)
                             (start-tree-id ?start-tree-id)
                             (parameter-proto ?old-parameter-proto)
                             (resources $?old-resources)
                             (recovery-state-proto ?old-recovery-state-proto)
                             (execution-mode ?execution-mode))
  ?uf <- (operation-update-state (operation-name ?operation-name)
                                 (target-state ?target-state)
                                 (resume-mode ?resume-mode)
                                 (parameter-proto ?parameter-proto)
                                 (resources $?resources)
                                 (recovery-state-proto ?recovery-state-proto)
                                 (span-reference-id ?target-span))
  (behavior-tree (id ?start-tree-id) (state ?start-tree-state))
 =>
  (bind ?new-span-reference-id
    (tracing-determine-operation-update-state-execution-span
      ?current-state ?target-state ?current-span ?target-span))

  (if (eq ?target-state PREPARING) then
    (if (<> ?new-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
      (bind ?trace-id (span-get-trace-id ?new-span-reference-id))
      (bind ?trace-url "")
      (do-for-fact ((?gcp-flag flag))
          (and (eq ?gcp-flag:name google-cloud-project)
               (eq ?gcp-flag:type STRING))

        (bind ?trace-url
          (span-get-trace-url ?new-span-reference-id ?gcp-flag:value))
      )
    )
  )

  (if (eq ?target-state RESUME) then
    (span-add-event-annotation ?new-span-reference-id (str-cat ?target-state))

    (bind ?conductor-resume-result
      (conductor-prepare-process-resume-with-tracing ?new-span-reference-id))
    (if (result-ok ?conductor-resume-result)
     then
      (bind ?target-state RUNNING)
      (if (eq ?resume-mode NEXT) then
        (behavior-tree-stepwise-step-through-tree ?operation-tree-id)
      )
      (if (and (or (eq ?resume-mode STEP) (eq ?resume-mode NEXT))
              (eq ?execution-mode NORMAL))
      then
        (printout warn "Resume mode is " ?resume-mode ", but execution mode is "
                  "NORMAL. Switching to STEP-WISE mode." crlf)
        (bind ?execution-mode STEP-WISE)
      )
     else
      (bind ?es-proto (result-error ?conductor-resume-result))
      (bind ?of (operation-attach-extended-status ?of ?es-proto))
      (pb-remove ?es-proto)
      (bind ?target-state FAILED)
    )
  )

  (if (eq ?target-state SUSPENDING) then
    (if (eq ?start-tree-id nil)
     then
       (printout error "Tried to suspend while no behavior tree running" crlf)
       (bind ?target-state FAILED)
     else
       ; If the start tree is an ACCEPTED state, behavior-tree-suspend is not
       ; called as that call will fail. Instead, the operation state is set to
       ; SUSPENDING, and explicitly handled in
       ; operation-transition-to-suspended-on-conductor-preparation-done.
       (if (and (neq ?start-tree-state ACCEPTED)
                (not (behavior-tree-suspend ?start-tree-id))) then
         (printout error "Failed to suspend process tree" crlf)
         (bind ?target-state ?current-state)
       )
    )
  )

  (if (eq ?target-state CANCELING) then
    (if (eq ?start-tree-id nil)
     then
       (printout error "Tried to cancel while no behavior tree running." crlf)
       (bind ?target-state FAILED)
     else
       ; If the start tree is an ACCEPTED state, behavior-tree-cancel is not
       ; called as that call will fail. Instead, the operation state is set to
       ; CANCELING, and explicitly handled in
       ; operation-canceling-with-start-tree-accepted.
       (if (and (neq ?start-tree-state ACCEPTED)
                (not (behavior-tree-cancel ?start-tree-id))) then
         (printout error "Failed to cancel process tree." crlf)
         (bind ?target-state ?current-state)
       )
    )
  )

  (bind ?new-parameter-proto ?old-parameter-proto)
  (if (<> ?parameter-proto 0) then ; Setting updated parameters
    (if (<> ?old-parameter-proto 0) then
      (printout error (str-cat "When transitioning to " ?target-state ": There "
        "was a parameter-proto already set and a new "
        "parameter-proto is specified. This should not happen.") crlf)
      (pb-remove ?old-parameter-proto)
    )
    (bind ?new-parameter-proto ?parameter-proto)
  )

  (bind ?new-resources ?old-resources)
  (if (<> (length$ ?resources) 0) then
    (if (<> (length$ ?old-resources) 0) then
      (printout error (str-cat "When transitioning to " ?target-state ": There "
        "were resources already set and new resources specified. "
        "This should not happen.") crlf)
    )
    (bind ?new-resources ?resources)
  )

  (if (and (<> ?recovery-state-proto 0) (<> ?old-recovery-state-proto 0)) then
    (printout error (str-cat "When transitioning to " ?target-state ": There "
      "was an old recovery state proto already set and a new recovery state "
      "proto specified. This should not happen.") crlf)
    (pb-remove ?old-recovery-state-proto)
  )

  (bind ?new-recovery-state-proto ?old-recovery-state-proto)
  (if (<> ?recovery-state-proto 0) then
    (if (<> ?old-recovery-state-proto 0) then
      (printout error (str-cat "When transitioning to " ?target-state ": There "
        "was an old recovery state proto already set and a new recovery state "
        "proto specified. This should not happen.") crlf)
      (pb-remove ?old-recovery-state-proto)
    )
    (bind ?new-recovery-state-proto ?recovery-state-proto)
  )

  (modify ?of (state ?target-state) (span-reference-id ?new-span-reference-id)
              (parameter-proto ?new-parameter-proto)
              (resources ?new-resources)
              (recovery-state-proto ?new-recovery-state-proto)
              (trace-id ?trace-id) (trace-url ?trace-url)
              (execution-mode ?execution-mode)
              (logged-on-suspend FALSE))
  (retract ?uf)
)

(defrule operation-mode-update
  "Transition to new simulation mode from external request"
  (declare (salience ?*SALIENCE-HIGHER*))
  ?of <- (operation-envelope (name ?operation-name)
                             (simulation-mode ?current-sim-mode)
                             (execution-mode ?current-exec-mode))
  ?uf <- (operation-update-mode
           (operation-name ?operation-name)
           (target-simulation-mode ?target-sim-mode)
           (target-execution-mode ?target-exec-mode))
 =>
  (if (eq ?target-exec-mode NONE)
   then (bind ?target-exec-mode ?current-exec-mode)
  )
  (if (eq ?target-sim-mode NONE)
   then (bind ?target-sim-mode ?current-sim-mode)
  )
  (if (neq ?current-exec-mode ?target-exec-mode) then
    (printout t "Switching execution mode from " ?current-exec-mode
                " to " ?target-exec-mode crlf)
  )
  (if (neq ?current-sim-mode ?target-sim-mode) then
    (printout t "Switching simulation mode from " ?current-sim-mode
                " to " ?target-sim-mode crlf)
  )
  (if (or (neq ?current-exec-mode ?target-exec-mode)
          (neq ?current-sim-mode ?target-sim-mode))
   then
     (modify ?of (execution-mode ?target-exec-mode)
                 (simulation-mode ?target-sim-mode)))
  (retract ?uf)
)

(defrule operation-skill-trace-handling-update
  "Transition to new skill trace handling from external request"
  (declare (salience ?*SALIENCE-HIGHER*))
  ?of <- (operation-envelope (name ?operation-name))
  ?uf <- (operation-update-skill-trace-handling
           (operation-name ?operation-name)
           (target-handling ?target-handling))
 =>
  (modify ?of (skill-trace-handling ?target-handling))
  (retract ?uf)
)

(defrule operation-start-node-update-start-tree-and-node
  "Update the start tree and node"
  (declare (salience ?*SALIENCE-HIGHER*))
  ?of <- (operation-envelope (name ?operation-name)
                             (operation-tree-id ?operation-tree-id)
                             (start-tree-id ?cur-start-tree-id))
  ?cur-st <- (behavior-tree (id ?cur-start-tree-id)
                            (start-node-id ?cur-start-node-id)
                            (root ?cur-start-tree-root))
  ?new-st <- (behavior-tree (id ?new-start-tree-id) (root ?new-start-tree-root))
  (behavior-tree-node (id ?new-start-node-id) (tree-id ?new-start-tree-id))

  ?uf <- (operation-update-start-node
           (operation-name ?operation-name)
           (tree-id ?new-start-tree-id&~nil)
           (node-id ?new-start-node-id&~0))

  (test (neq ?cur-start-tree-id ?new-start-tree-id))
 =>
  ; Start tree changes:
  ; 1. Make sure the current start tree is running its root again
  (modify ?cur-st (start-node-id ?cur-start-tree-root))

  ; 2. Make sure the new start tree is running its new start node
  (modify ?new-st (start-node-id ?new-start-node-id))

  ; 3. Set the new start tree in the operation
  (modify ?of (start-tree-id ?new-start-tree-id))

  (retract ?uf)
)

(defrule operation-start-node-update-start-node
  "Update the start node, when the start tree does not change"
  (declare (salience ?*SALIENCE-HIGHER*))
  (operation-envelope (name ?operation-name)
                      (operation-tree-id ?operation-tree-id)
                      (start-tree-id ?start-tree-id))
  ?cur-st <- (behavior-tree (id ?start-tree-id)
                            (start-node-id ?cur-start-node-id)
                            (root ?cur-start-tree-root))
  (behavior-tree-node (id ?new-start-node-id) (tree-id ?start-tree-id))

  ?uf <- (operation-update-start-node
           (operation-name ?operation-name)
           (tree-id ?start-tree-id&~nil)
           (node-id ?new-start-node-id&~0))
 =>
  ; Start tree does not change, start node might
  (modify ?cur-st (start-node-id ?new-start-node-id))

  (retract ?uf)
)

(defrule operation-start-node-update-start-node-non-existing
  "Update the start node, but that node does not exist"
  (declare (salience ?*SALIENCE-HIGHER*))
  ?uf <- (operation-update-start-node
           (operation-name ?operation-name)
           (tree-id ?start-tree-id&~nil)
           (node-id ?start-node-id&~0))
  (not (behavior-tree-node (id ?start-node-id) (tree-id ?start-tree-id)))
 =>
  (printout error (str-cat "operation-update-start-node requested for tree id '"
                           ?start-tree-id "' and node id " ?start-node-id " in "
                           "operation " ?operation-name ", but that node does "
                           "not exist.")
                  crlf)
  (retract ?uf)
)


(defrule operation-start-node-update-reset
  "Update the start tree and node to run the operation tree"
  (declare (salience ?*SALIENCE-HIGHER*))
  ?of <- (operation-envelope (name ?operation-name)
                             (operation-tree-id ?operation-tree-id)
                             (start-tree-id ?cur-start-tree-id))
  ?cur-st <- (behavior-tree (id ?cur-start-tree-id)
                            (start-node-id ?cur-start-node-id)
                            (root ?cur-start-tree-root))

  ?uf <- (operation-update-start-node
           (operation-name ?operation-name)
           (tree-id nil))
 =>
  ; Reset to run the full operation tree
  (modify ?cur-st (start-node-id ?cur-start-tree-root))
  (modify ?of (start-tree-id ?operation-tree-id))

  (retract ?uf)
)

(defrule operation-start-node-update-invalid
  "Invalid operation-update-start-node fact present"
  (declare (salience ?*SALIENCE-HIGH*))
  ?uf <- (operation-update-start-node
           (operation-name ?operation-name)
           (tree-id ?tree-id&~nil)
           (node-id 0))
 =>
  (printout error (str-cat "operation-update-start-node requested for tree id '"
                           ?tree-id "' without valid start node-id (0 given).")
                  crlf)
  (retract ?uf)
)

(defrule operation-fail-on-conductor-preparation-error
  "Fails the process tree if the conductor failed to prepare."
  ?op <- (operation-envelope (state ?op-state&PREPARING|SUSPENDING)
                             (name ?op-name))
  ?cf <- (conductor-preparation-client-operation
           (operation-name ?op-name) (is-done TRUE) (has-error TRUE)
           (extended-status-proto-id ?conductor-es-proto-id))
 =>
  (bind ?es-proto (pb-clone ?conductor-es-proto-id))
  (if (eq ?op-state PREPARING)
   then
    (extended-status-set-message ?es-proto USER
      (str-cat "Cannot start operation " ?op-name))
   else
    (extended-status-set-message ?es-proto USER
      (str-cat "Cannot suspend operation " ?op-name))
  )
  (bind ?op (operation-attach-extended-status ?op ?es-proto))
  (pb-remove ?es-proto)

  (modify ?op (state FAILED))

  (conductor-preparation-client-operation-delete ?op-name)
)

(defrule operation-stop-conductor-preparation-in-accepted-state
  "Handle stop signal for conductor preparation if operation failed to start."
  (operation-envelope (state ACCEPTED) (name ?op-name))
  (conductor-preparation-client-operation
    (conductor-operation-name ?conductor-op-name)
    (operation-name ?op-name))
  ?mf <- (conductor-modify-client-operation
           (action STOP-PREPARATION)
           (conductor-operation-name ?conductor-op-name))
 =>
  (conductor-preparation-client-operation-delete ?op-name)
  (if (operation-are-all-waiting) then
    (conductor-notify-all-processes-stopped)
  )
  (retract ?mf)
)

(defrule operation-stop-conductor-preparation-in-other-states
  "Handle stop signal for conductor preparation after the operation started."
  (operation-envelope (state ?op-state&~ACCEPTED) (name ?op-name))
  (conductor-preparation-client-operation
    (conductor-operation-name ?conductor-op-name)
    (is-done ?conductor-op-is-done) (operation-name ?op-name))
  ?mf <- (conductor-modify-client-operation
           (action STOP-PREPARATION)
           (conductor-operation-name ?conductor-op-name))
 =>
  (if (not ?conductor-op-is-done) then
    ; Cancel the conductor client operation and wait for the
    ; conductor-preparation-client-operation fact to be updated.
    (printout t (str-cat "Stopping conductor preparation client operation "
                         " in executive operation state: " ?op-state) crlf)
    (conductor-cancel-preparation-async ?conductor-op-name)
  )
  (retract ?mf)
)

(defrule operation-transition-to-running-on-conductor-preparation-done
  "Transition from PREPARING to RUNNING when conductor preparation is done."
  ?op <- (operation-envelope (state PREPARING) (name ?op-name))
  (conductor-preparation-client-operation (operation-name ?op-name)
                                          (is-done TRUE) (has-error FALSE)
                                          (scene-id ?scene-id))
 =>
  (modify ?op (state RUNNING) (scene-id ?scene-id))
  (conductor-preparation-client-operation-delete ?op-name)
)

(defrule operation-select-start-tree
  "Start the execution of the operation's start tree."
  ?op <- (operation-envelope (state RUNNING)
                             (name ?op-name)
                             (operation-tree-id ?operation-tree-id)
                             (start-tree-id ?start-tree-id)
                             (recovery-state-proto ?recovery-state-proto)
                             (parameter-descriptor-pool ?param-pool)
                             (parameter-message-name ?param-message-name)
                             (parameter-proto ?param-proto)
                             (simulation-mode ?sim-mode)
                             (span-reference-id ?execution-span))
  (behavior-tree (id ?operation-tree-id) (blackboard-scope ?bb-scope))
  ?start-tree <- (behavior-tree (id ?start-tree-id) (state ACCEPTED)
                   (start-node-id ?start-node-id))
  ?start-node <- (behavior-tree-node (id ?start-node-id)
                                     (tree-id ?start-tree-id))
 =>
  (bind ?tree-span (tracing-cc-start-tree-span ?start-tree-id ?execution-span))

  (bind ?world-updater-successful TRUE)
  (if (or (eq ?sim-mode PREVIEW) (eq ?sim-mode FAST-PREVIEW))
   then
    ; For a process tree in PREVIEW sim we need to pause the world updater
    (bind ?world-updater-pause-span
      (span-start "World Updater Pause" ?tree-span "world-updater-pause"))
    (bind ?world-updater-successful (world-updater-pause))
    (span-end ?world-updater-pause-span)
   else
    ; For a process tree NOT in PREVIEW sim we need to resume the world updater
    (bind ?world-updater-resume-span
    (span-start "World Updater Resume" ?tree-span "world-updater-resume"))
    (bind ?world-updater-successful (world-updater-resume))
    (span-end ?world-updater-resume-span)
  )

  (if (not ?world-updater-successful) then
    (bind ?es-proto (extended-status-create 21001 ERROR))
    (bind ?op (operation-attach-extended-status ?op ?es-proto))
    (pb-remove ?es-proto)
    (pb-remove ?recovery-state-proto)
    (modify ?op (state FAILED) (recovery-state-proto 0))
    (return)
  )

  ; The operation has parameters - parameterize it like a PBT
  (if (<> ?param-pool 0) then
    ; Put the param proto on the blackboard under its actual type, not as an Any
    (bind ?parameter-cast-result
      (pb-cast-from-any-with-pool ?param-proto ?param-pool ?param-message-name))
    (if (not (result-ok ?parameter-cast-result)) then
      (bind ?es-proto (extended-status-create 13002 ERROR))
      (extended-status-set-message ?es-proto USER
        (str-cat "Failed to cast the parameters when starting process tree"
                 " for operation " ?op-name))
      (bind ?es-detail-proto (result-error ?parameter-cast-result))
      (extended-status-add-context ?es-proto ?es-detail-proto)
      (pb-remove ?es-detail-proto)
      (bind ?op (operation-attach-extended-status ?op ?es-proto))
      (pb-remove ?es-proto)
      (pb-remove ?recovery-state-proto)
      (modify ?op (state FAILED) (recovery-state-proto 0))
      (return)
    )
    (bind ?parameter-proto-id (result-value ?parameter-cast-result))
    (printout t (str-cat "Starting Process tree with parameters: "
                         (pb-tostring-with-pool
                           ?parameter-proto-id ?param-pool 0)) crlf)

    (assert (blackboard-update (key "params")
                               (scope ?bb-scope)
                               (operation-name ?op-name)
                               (proto-id ?parameter-proto-id)
                               (keep-on-flush FALSE)))
  )

  (span-add-event-annotation ?execution-span ?*TRACING-TREE-START-EVENT*)

  (if (= ?recovery-state-proto 0)
    then
      ; No recovery-state-proto => Normal start
      (modify ?start-tree (state RUNNING) (span-reference-id ?tree-span))
    else
      (printout t "Applying recovery nodes to current state before starting tree" crlf)
      (bind ?recovery-result
        (recovery-recover-operation-state ?recovery-state-proto ?op-name
                                          ?tree-span))
      (pb-remove ?recovery-state-proto)
      (if (not (result-ok ?recovery-result)) then
        (bind ?op
          (operation-attach-extended-status ?op
            (result-error ?recovery-result)))
        (modify ?op (state FAILED) (recovery-state-proto 0))
        (behavior-tree-reset ?operation-tree-id FALSE)
        (return)
      )
  )

  (modify ?op (recovery-state-proto 0))
)

(defrule operation-resume-operation-tree
  (operation-envelope (state RUNNING) (start-tree-id ?tree-id))
  ?tree <- (behavior-tree (id ?tree-id) (state SUSPENDED))
 =>
  (printout t "Resuming the operation's start tree " ?tree-id crlf)
  (modify ?tree (state RUNNING))
)

(defrule operation-transition-to-suspended-on-conductor-preparation-done
  "Transition from SUSPENDING to SUSPENDED when conductor preparation is done."
  ; Note that the start tree is expected to be in an ACCEPTED state since
  ; the operation transitioned to SUSPENDING before the conductor preparation
  ; was done and the start tree was started.
  ?op <- (operation-envelope (state SUSPENDING) (start-tree-id ?bt-id)
                             (span-reference-id ?span-id) (name ?op-name))
  (behavior-tree (id ?bt-id) (state ACCEPTED))
  (conductor-preparation-client-operation (operation-name ?op-name)
                                          (is-done TRUE) (has-error FALSE)
                                          (scene-id ?scene-id))
  =>
  (span-add-event-annotation ?span-id (str-cat SUSPENDED))
  (printout t "Operation " ?op-name " is SUSPENDED" crlf)
  (modify ?op (state SUSPENDED) (scene-id ?scene-id))

  ; Notify conductor if all operations are now in a waiting state.
  (if (operation-are-all-waiting)
   then
    (bind ?conductor-notify-span
      (span-start "Notify conductor all processes stopped" ?span-id
        "conductor-notify-all-processes-stopped"))
    (conductor-notify-all-processes-stopped)
    (span-end ?conductor-notify-span)
  )

  ; Note that conductor-preparation-client-operation-delete is not called here
  ; since that state is needed when resuming the operation.
)

; Rules that update the execute state based on the process tree state

(defrule operation-transition-to-suspended
  "Transitioning from SUSPENDING to SUSPENDED"
  ?op <- (operation-envelope (state SUSPENDING) (start-tree-id ?bt-id)
                             (span-reference-id ?span-id) (name ?op-name))
  (behavior-tree (id ?bt-id) (state ?state&SUSPENDED))
  =>
  (span-add-event-annotation ?span-id (str-cat ?state))
  (printout t "Operation " ?op-name " is SUSPENDED" crlf)
  (modify ?op (state SUSPENDED))

  ; Notify conductor if all operations are now in a waiting state.
  (if (operation-are-all-waiting)
   then
    (bind ?conductor-notify-span
      (span-start "Notify conductor all processes stopped" ?span-id
        "conductor-notify-all-processes-stopped"))
    (conductor-notify-all-processes-stopped)
    (span-end ?conductor-notify-span)
  )
)

(defrule operation-start-tree-failed
  "If start tree has failed, set operation state, report error"
  ?op <- (operation-envelope (name ?op-name) (start-tree-id ?start-bt-id)
                             (state RUNNING|SUSPENDING|CANCELING))
  (behavior-tree (id ?start-bt-id) (start-node-id ?start-node) (state FAILED))
  (behavior-tree-node (id ?start-node) (tree-id ?start-bt-id)
                      (extended-status-proto-id ?es-proto))
 =>
  (printout t "Start tree of operation " ?op-name " has FAILED" crlf)
  (bind ?op-es-proto (extended-status-create 13001 ERROR))
  (if (<> ?es-proto 0) then
    (extended-status-add-context ?op-es-proto ?es-proto)
  )
  (bind ?op (operation-attach-extended-status ?op ?op-es-proto))
  (pb-remove ?op-es-proto)

  (modify ?op (state FAILED))
)

(defrule operation-start-tree-succeeded
  "If start tree has succeeded, set operation state"
  ?op <- (operation-envelope (name ?op-name)
                             (operation-tree-id ?operation-tree-id)
                             (start-tree-id ?start-tree-id)
                             (state RUNNING|SUSPENDING|CANCELING)
                             (return-value-descriptor-pool
                               ?return-value-pool-id)
                             (return-value-message-name ?return-value-type))
  (behavior-tree (id ?start-tree-id) (state SUCCEEDED)
                 (start-node-id ?start-node-id) (root ?root-node-id))
  (behavior-tree (id ?operation-tree-id)
                 (return-value-expression ?return-expression)
                 (blackboard-scope ?bb-scope))
  ?start-node <- (behavior-tree-node (id ?start-node-id)
                                     (tree-id ?start-tree-id))
 =>
  (bind ?outcome SUCCEEDED)
  (bind ?return-proto 0)
  ; If the operation tree is a PBT (has ?return-value-pool-id) that defines a
  ; return value expression: Evaluate the operation's return value.
  (if (and (<> ?return-value-pool-id 0) (neq ?return-expression "")) then
    (bind ?evaluate-return-value-result
      (cel-eval-to-proto-with-type ?return-expression
           ?return-value-type ?return-value-pool-id ?bb-scope ?op-name))
    (if (result-ok ?evaluate-return-value-result)
      then
        (bind ?return-proto (result-value ?evaluate-return-value-result))
      else
        (bind ?error-msg "Failed to evaluate operation return value.")
        (bind ?extended-status-proto
          (result-error ?evaluate-return-value-result))
        ; If only part of the process was run (e.g. only a single node), it is
        ; likely that this did not produce the necessary outputs for the
        ; operation.
        ; This is not a failure, but rather expected. Thus only warn.
        (if (or (neq ?start-tree-id ?operation-tree-id)
                (<> ?start-node-id ?root-node-id))
          then
            ; TODO(b/377221844) Report this to the user.
            (printout warn (str-cat ?error-msg ": "
              (pb-tostring ?extended-status-proto)
              " This might be expected as only part of the tree was executed.")
              crlf)
          else
            (bind ?es-proto (extended-status-create 13003 ERROR))
            (extended-status-set-message ?es-proto USER ?error-msg)
            (extended-status-add-context ?es-proto ?extended-status-proto)
            (bind ?op (operation-attach-extended-status ?op ?es-proto))
            (pb-remove ?es-proto)
            (bind ?outcome FAILED)
        )
        (pb-remove ?extended-status-proto)
    )
  )
  (printout t (str-cat "Operation " ?op-name " has " ?outcome) crlf)
  (modify ?op (state ?outcome) (return-value-proto ?return-proto))
)

(defrule operation-start-tree-canceled
  "If start tree was canceled, set operation state"
  ?op <- (operation-envelope (name ?op-name) (start-tree-id ?top-bt-id)
                             (state CANCELING))
  (behavior-tree (id ?top-bt-id) (state CANCELED))
 =>
  (printout t "Operation " ?op-name " is CANCELED" crlf)
  (modify ?op (state CANCELED))
)

(defrule operation-start-tree-canceled-unexpected
  "Handle the start tree being canceled, while the operation wasn't CANCELING"
  ?op <- (operation-envelope (name ?op-name) (start-tree-id ?start-bt-id)
                             (state ?state&RUNNING|SUSPENDING))
  (behavior-tree (id ?start-bt-id) (state CANCELED))
 =>
  (bind ?msg (str-cat "Operation (" ?op-name ")'s behavior tree was canceled "
                      "unexpectedly, while the operation was: " ?state))
  (printout warn ?msg crlf)

  (bind ?es-proto (extended-status-create 13000 ERROR))
  (extended-status-set-message ?es-proto USER ?msg)

  (bind ?op (operation-attach-extended-status ?op ?es-proto))
  (pb-remove ?es-proto)

  (modify ?op (state FAILED))
)

(defrule operation-canceling-with-start-tree-accepted
  "Handle the operation being CANCELING while the start tree is still ACCEPTED"
  ?op <- (operation-envelope (state CANCELING) (name ?op-name)
                             (start-tree-id ?top-bt-id))
  (behavior-tree (id ?top-bt-id) (state ACCEPTED))
 =>
  (conductor-preparation-client-operation-delete ?op-name)
  (modify ?op (state CANCELED))
)

(defrule operation-terminated
  "Clean up after the operation has terminated."
  ?op <- (operation-envelope (span-reference-id ?execution-span&~0)
                             (state ?state&SUCCEEDED|FAILED|CANCELED)
                             (logged-completion TRUE))
 =>
  ; Notify conductor if all operations are now in a waiting state.
  (if (operation-are-all-waiting)
   then
    (bind ?conductor-notify-span
      (span-start "Notify conductor all processes stopped" ?execution-span
        "conductor-notify-all-processes-stopped"))
    (conductor-notify-all-processes-stopped)
    (span-end ?conductor-notify-span)
    (bind ?code-execution-service-restart-kernels-span
      (span-start "Restart code execution service kernels" ?execution-span
        "code-execution-service-restart-kernels"))
    (code-execution-service-restart-kernels)
    (span-end ?code-execution-service-restart-kernels-span)
  )

  ; End the operation's execution span
  (switch ?state
    (case SUCCEEDED then
      (span-end ?execution-span)
    )
    (case FAILED then
      (span-end-failure ?execution-span ?*TRACING-STATUS-ABORTED*
        "Execution failed")
    )
    (case CANCELED then
      (span-end-failure ?execution-span ?*TRACING-STATUS-CANCELLED*
        "Execution canceled")
    )
  )

  (modify ?op (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

; Handle the active actions timer

(defrule operation-report-active-actions-timer-start
  "Start a timer to repeatetly printout information, such running actions."
  (executive-state (state ACTIVE))
  (not (timer (name operation-report-active-actions-timer)))
  =>
  (assert (timer (name operation-report-active-actions-timer)))
)

(defrule operation-report-active-actions-timer-timeout
  "Print active actions every interval of report-active-actions-timeout."
  (declare (salience ?*SALIENCE-LOW*))
  (executive-state (state ACTIVE))
  (flag (name report-active-actions-timeout) (type FLOAT) (value ?interval))
  ?now-fact <- (time (timestamp $?now-time))
  ?t <- (timer (name operation-report-active-actions-timer)
          (start-time $?st&:(timeout ?now-time ?st ?interval)))
 =>
  (do-for-fact ((?op operation-envelope)) TRUE
    (if (any-factp ((?cf conductor-preparation-client-operation))
          (and (eq ?cf:operation-name ?op:name)
               (eq ?cf:is-done FALSE)))
     then
      (printout t (str-cat "Operation: " ?op:name
        " State: " ?op:state
        " Active actions: Waiting for conductor to prepare.") crlf)
     else
      (behavior-tree-task-node-print-active-actions ?op)
    )
  )
  (modify ?t (start-time ?now-time))
)
