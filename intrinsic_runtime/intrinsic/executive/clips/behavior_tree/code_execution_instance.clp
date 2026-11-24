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

; Representation and handling for code execution instances.

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate code-execution-instance
  ; Uniquely associates a code-execution-instance to a specific node
  (slot operation-name (type STRING) (default ?NONE))
  (slot tree-id (type SYMBOL) (default ?NONE))
  (slot node-id (type INTEGER) (default ?NONE))

  ; The AnyWithAssignments defined for CodeExecution.
  ; This must never be mutated after import.
  (slot parameters-prototype-proto (type INTEGER))
  ; The Any proto with applied assignments of an executing instance.
  ; This must be set with assigned parameters before selecting this instance.
  (slot parameters-instance-proto (type INTEGER))

  ; The code proto that was set as the 'code' option in the CodeExecution proto.
  (slot code-proto (type INTEGER))

  ; Blackboard key to write the return value to.
  (slot return-value-key (type STRING))

  (slot parameter-message-full-name (type STRING))
  (slot return-value-message-full-name (type STRING))
  (slot file-descriptor-set-proto (type INTEGER))
  (slot file-descriptor-set-pool (type INTEGER))

  ; ACCEPTED: Code execution instance is ready to be executed.
  ;           This is the initial state for code-execution-instances and the
  ;           state after a reset.
  ; SELECTED: Instance has been selected for execution, i.e., it is
  ;           expected to parameterize and execute the associated code.
  ;           This is set be the enclosing task node.
  ; PENDING:  Code execution was requested to start.
  ; RUNNING:  Execution is on-going.
  ; CANCELATION-REQUESTED: code-execution-instance should cancel. As a result
  ;                        this instance should transition to canceling and
  ;                        eventually be canceled, fail or succeed.
  ;                        This is set be the enclosing task node.
  ; CANCELATION-PENDING:   Cancelation request was sent.
  ; CANCELING: code-execution-instance cancellation is on-going. If cancellation
  ;            finishes the code-execution-instance usually is canceled. If the
  ;            code-execution-instance execution completes before the
  ;            cancellation it can also either succeed or fail.
  ; SUCCEEDED: Execution has completed successfully.
  ; FAILED:    Execution has completed with failure.
  ; CANCELED:  Execution has canceled upon cancelation request.
  ; Note: code-execution-instances cannot suspend.
  (slot state (type SYMBOL)
        (allowed-values ACCEPTED SELECTED PENDING RUNNING
                        CANCELATION-REQUESTED CANCELATION-PENDING CANCELING
                        ; Terminal states
                        SUCCEEDED FAILED CANCELED)
        (default ACCEPTED))

  ; This is tracks the state slot and updates the run metadata proto if they get
  ; out of sync, i.e., when the state changes.
  (slot run-metadata-proto-state (type SYMBOL) (default ACCEPTED))

  ; An actual ExtendedStatus proto created and propagated on failure.
  (slot extended-status-proto (type INTEGER))

  ; The span-reference-id for tracing of this code execution instance.
  (slot span-reference-id (type INTEGER) (default ?*TRACING-INVALID-SPAN-ID*))

  (slot log-id (type INTEGER) (default 0))
  (slot logged-completion (type SYMBOL) (allowed-values TRUE FALSE) (default FALSE))
)

; --------------------------------- FUNCTIONS ---------------------------------

; Resets the code execution instance.
;
; Args:
;   ?operation-name: The operation that this instance is in.
;   ?tree-id: The tree that this instance is in.
;   ?node-id: The node that this instance belongs to.
(deffunction code-execution-instance-reset (?operation-name ?tree-id ?node-id)
  (do-for-fact ((?cei code-execution-instance))
    (and
      (eq ?cei:operation-name ?operation-name)
      (eq ?cei:tree-id ?tree-id)
      (eq ?cei:node-id ?node-id))
    (pb-remove ?cei:parameters-instance-proto)
    (pb-remove ?cei:extended-status-proto)
    (if (<> ?cei:span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
      (span-end-failure ?cei:span-reference-id ?*TRACING-STATUS-INTERNAL*
        "Span running when code-execution-instance-reset was called.")
    )
    (modify ?cei (state ACCEPTED) (log-id 0) (logged-completion FALSE)
                 (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
  )
)

; Removes the code-execution-instance.
;
; Args:
;   ?operation-name: The operation that this instance is in.
;   ?tree-id: The tree that this instance is in.
;   ?node-id: The node that this instance belongs to.
(deffunction code-execution-instance-remove (?operation-name ?tree-id ?node-id)
  (do-for-fact ((?cei code-execution-instance))
    (and
      (eq ?cei:operation-name ?operation-name)
      (eq ?cei:tree-id ?tree-id)
      (eq ?cei:node-id ?node-id))
    (pb-remove ?cei:parameters-prototype-proto)
    (pb-remove ?cei:parameters-instance-proto)
    (pb-remove ?cei:code-proto)
    (pb-remove ?cei:file-descriptor-set-proto)
    (pb-remove-descriptor-pool ?cei:file-descriptor-set-pool)
    (pb-remove ?cei:extended-status-proto)
    (if (<> ?cei:span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
      (span-end-failure ?cei:span-reference-id ?*TRACING-STATUS-INTERNAL*
        "Span running when code-execution-instance-remove was called.")
    )
    (retract ?cei)
  )
)

; Gives a string formulation identifying this code-execution-instance.
;
; Args:
;   ?cei: Fact-address of a code-execution-instance fact.
(deffunction code-execution-instance-to-string (?cei)
  (bind ?op-name (fact-slot-value ?cei operation-name))
  (bind ?tree-id (fact-slot-value ?cei tree-id))
  (bind ?node-id (fact-slot-value ?cei node-id))
  (return (str-cat "code-execution-instance"
                   "(operation_name: " ?op-name ", tree_id: " ?tree-id ", node_id: " ?node-id ")"))
)

; ----------------------------------- RULES -----------------------------------

; Processing the states of the code-execution-instance works as follows.
;
; The nominal execution is: SELECTED -> PENDING -> RUNNING -> SUCCEEDED/FAILED
; In case of cancelation the usual state transitions are:
;   SELECTED -> PENDING -> RUNNING -> CANCELATION-REQUESTED ->
;   CANCELATION-PENDING -> CANCELING -> CANCELED
; State transitions are triggered either from the behavior tree, e.g., setting
; the state to SELECTED or CANCELATION-REQUESTED, or via incoming
; code-execution-state-update facts.
;
; There can be up to two background threads in the dispatcher per
; code-execution-instance: the main execution thread and a cancel request
; thread. The latter only sends out the cancelation request and the reports an
; update: CANCELING. The CANCELED update arrives in the main thread via the LRO
; being canceled in the same way that the execution reports SUCCEEDED/FAILED.
;
; After starting a background request the state transitions to PENDING or
; CANCELATION-PENDING, respectively. From then on the state-updates can come in
; in parallel and must be processed accordingly.
;
; There is exactly one rule handling the terminal states SUCCEEDED, FAILED,
; CANCELED. These will be processed when there are no other state updates, so
; that there are not spurious updates left over when asserted in parallel.
; Either updates will be handled by the nominal flow (PENDING -> RUNNING or
; CANCELATION-PENDING -> CANCELATION-REQUESTED -> CANCELING) or discarded when
; arriving later than usual by -cancelation-update-running or
; -canceling-already-fininshed.

(defrule code-execution-instance-start
  "Start the code execution when selected."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
                                   (state SELECTED) (log-id ~0)
                                   (parameters-instance-proto ?parameter-proto)
                                   (code-proto ?code-proto)
                                   (parameter-message-full-name
                                     ?param-message-name)
                                   (return-value-message-full-name
                                     ?return-message-name)
                                   (file-descriptor-set-proto ?fds-proto)
                                   (span-reference-id ?span-id))
  (world (id ?world-id))
  (not (code-execution-state-update (operation-name ?op)
                                    (tree-id ?tree-id) (node-id ?node-id)))
 =>
  (code-execution-start ?op ?tree-id ?node-id ?world-id
                        ?parameter-proto ?code-proto ?param-message-name
                        ?return-message-name ?fds-proto ?span-id)
  (modify ?cei (state PENDING))
)

(defrule code-execution-instance-running
  "If there is a running update, set running state."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
                                   (state PENDING))
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state RUNNING))
 =>
  (modify ?cei (state RUNNING))
  (code-execution-state-update-remove ?su)
)

(defrule code-execution-instance-cancelation-update-running
  "Disregard running update when cancelation is requested or pending."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
            (state CANCELATION-REQUESTED|CANCELATION-PENDING|CANCELING))
  ; This can occur when a cancelation request is sent right after the code
  ; execution was started.
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state RUNNING))
 =>
  (code-execution-state-update-remove ?su)
)

(defrule code-execution-instance-succeeded
  "If there is a succeeded update, set succeeded state."
  (behavior-tree (id ?tree-id) (operation-name ?op)
                 (blackboard-scope ?bb-scope))
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
            (state RUNNING|CANCELATION-REQUESTED|CANCELATION-PENDING|CANCELING)
            (return-value-key ?return-key)
            (return-value-message-full-name ?return-message-name)
            (file-descriptor-set-pool ?pool-id)
            (span-reference-id ?span-id))
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state SUCCEEDED)
                                      (return-value-proto ?return-any))
  ; Ensure all other updates have been processed
  (not (code-execution-state-update (operation-name ?op)
                                    (tree-id ?tree-id) (node-id ?node-id)
                                    (state ~SUCCEEDED)))
 =>
  (bind ?cei-state SUCCEEDED)
  (bind ?cei-es-proto 0)
  (if (neq ?return-key "") then
    (if (eq ?return-any 0)
      then
        (if (neq ?return-message-name "") then
          (bind ?cei-es-proto (extended-status-create 13701 ERROR))
          (extended-status-set-message ?cei-es-proto USER
            (str-cat "Code Execution succeeded without providing a result, "
              "but a result of type " ?return-message-name " was expected."))
          (bind ?cei-state FAILED)
        )
      else
        (if (<> ?pool-id 0)
          then
            (bind ?cast-result
              (pb-cast-from-any-with-pool ?return-any ?pool-id
                                          ?return-message-name))
            (if (result-ok ?cast-result)
              then
                (assert (blackboard-update (key ?return-key) (scope ?bb-scope)
                                           (operation-name ?op)
                                           (proto-id (result-value ?cast-result))))
              else
                (bind ?cei-es-proto (result-error ?cast-result))
                (bind ?cei-state FAILED)
            )
          else
            ; Shouldn't happen as pool is created on import (or import fails)
            (bind ?cei-es-proto
              (extended-status-create 13701 ERROR))
            (extended-status-set-message ?cei-es-proto USER
              (str-cat "Code Execution: No Pool available for return value on: "
                (code-execution-instance-to-string ?cei)))
            (bind ?cei-state FAILED)
        )
    )
  )

  (if (<> ?cei-es-proto 0) then
    (printout error (str-cat "Failed to apply successful code execution: "
      (pb-tostring ?cei-es-proto)) crlf)
  )

  (if (eq ?cei-state SUCCEEDED)
    then
      (span-end ?span-id)
    else
      (span-end-failure ?span-id
        ?*TRACING-STATUS-ABORTED* "Code execution failed")
  )
  (modify ?cei (state ?cei-state) (extended-status-proto ?cei-es-proto)
               (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
  ; This will also remove ?return-any which is fine as the proto put
  ; on the blackboard is the casted proto
  (code-execution-state-update-remove ?su)
)

(defrule code-execution-instance-failed
  "If there is a failed update, set failed state."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
            (state PENDING|RUNNING|
                   CANCELATION-REQUESTED|CANCELATION-PENDING|CANCELING)
            (span-reference-id ?span-id))
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state FAILED)
                                      (return-value-proto ?return-proto)
                                      (extended-status-proto ?es-proto))
  ; Ensure all other updates have been processed
  (not (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state ~FAILED)))
 =>
  ; Should never be set, but remove to not leak anything unexpected
  (pb-remove ?return-proto)
  (span-end-failure ?span-id
    ?*TRACING-STATUS-ABORTED* "Code execution failed")
  (modify ?cei (state FAILED) (extended-status-proto ?es-proto)
               (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
  (retract ?su)
)

(defrule code-execution-instance-cancel
  "If cancelation is requested, cancel the code execution."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
                                   (state CANCELATION-REQUESTED))
  ; Make sure there are no pending updates (e.g. SUCCEEDED).
  (not (code-execution-state-update (operation-name ?op)
                                    (tree-id ?tree-id) (node-id ?node-id)))
 =>
  (code-execution-cancel-async ?op ?tree-id ?node-id)
  (modify ?cei (state CANCELATION-PENDING))
)

(defrule code-execution-instance-canceling
  "If there is a canceling update, set canceling state."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
                                   (state CANCELATION-PENDING))
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state CANCELING))
  (not (code-execution-state-update (operation-name ?op)
                                    (tree-id ?tree-id) (node-id ?node-id)
                                    (state RUNNING)))
 =>
  (modify ?cei (state CANCELING))
  (code-execution-state-update-remove ?su)
)

(defrule code-execution-instance-canceled
  "If there is a canceled update, set canceled state."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
                                   (state CANCELATION-PENDING|CANCELING)
                                   (span-reference-id ?span-id))
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state CANCELED))
  ; Ensure all other updates have been processed
  (not (code-execution-state-update (operation-name ?op)
                                    (tree-id ?tree-id) (node-id ?node-id)
                                    (state ~CANCELED)))
 =>
  (span-end ?span-id)
  (modify ?cei (state CANCELED)
               (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
  (code-execution-state-update-remove ?su)
)

(defrule code-execution-instance-canceling-already-finished
  "There is a canceling update, but the execution is already finished."
  ?cei <- (code-execution-instance (operation-name ?op)
                                   (tree-id ?tree-id) (node-id ?node-id)
                                   (state SUCCEEDED|FAILED|CANCELED))
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state CANCELING))
 =>
  (code-execution-state-update-remove ?su)
)

(defrule code-execution-instance-state-update-cleanup
  "Retract left-over code-execution-state-updates. Should never happen."
  (declare (salience ?*SALIENCE-LOW*))
  ?su <- (code-execution-state-update (operation-name ?op)
                                      (tree-id ?tree-id) (node-id ?node-id)
                                      (state ?state))
 =>
  (printout error (str-cat "code-execution-state-update unhandled for "
                           "operation " ?op " tree-id " ?tree-id
                           " node-id " ?node-id " with state " ?state ".") crlf)
  (code-execution-state-update-remove ?su)
)
