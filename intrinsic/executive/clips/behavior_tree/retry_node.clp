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

; Processing rules for a node of type RETRY.

; The RETRY node runs its child. It runs the same child again for a given
; number of maximum tries if the child fails. The node succeeds when the child
; succeeds. It fails when the child has failed as often as tries are set.

; --------------------------------- FUNCTIONS ---------------------------------

; Update the current counter of the given ?node to ?counter-value
;
; Args:
;   ?node: must be a fact-address of a retry node
;   ?counter-value: New value for the num-tries slot of a retry node
(deffunction behavior-tree-retry-node-update-counter (?node ?counter-value)
  (modify ?node (retry-num-tries ?counter-value))
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-retry-node-start
  "A retry node directly switches to running when ready"
  (behavior-tree (id ?tree-id) (state RUNNING) (blackboard-scope ?bb-scope)
                 (operation-name ?op-name))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (id ?node-id)
                               (type RETRY) (state READY)
                               (retry-max-tries ?max-tries)
                               (retry-num-tries ?num-tries)
                               (retry-counter-blackboard-key ?key))
 =>
  (if (neq ?key "") then
    (bind ?p (pb-create "google.protobuf.Int64Value"))
    (pb-set-field ?p "value" ?num-tries)
    (assert (blackboard-update (key ?key) (scope ?bb-scope)
                               (operation-name ?op-name) (proto-id ?p)
                               (source-type NODE) (source-tree-id ?tree-id)
                               (source-node-id ?node-id)))
  )
  (modify ?node (state RUNNING) (retry-num-tries (+ ?num-tries 1)))
)

(defrule behavior-tree-retry-node-select-child
  "Select the single child"
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?node-id)
                               (tree-id ?tree-id)
                               (type RETRY) (state RUNNING)
                               (retry-num-tries ?num-tries)
                               (retry-child-id ?retry-child-id))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                                (state ACCEPTED))
 =>
  ; behavior-tree-retry-node-start already increased the counter
  ; Thus we start the iteration span 1 less than the current one
  (bind ?iteration-span
    (tracing-cc-start-node-iteration-span ?tree-id ?node-id (- ?num-tries 1)))
  (modify ?node (iteration-span-reference-id ?iteration-span))
  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-retry-node-child-success
  "A retry node succeeds if its child has succeeded within the max tries."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type RETRY) (state RUNNING|CANCELING)
                               (retry-counter-blackboard-key ?key)
                               (retry-child-id ?retry-child-id)
                               (iteration-span-reference-id ?iteration-span))
  (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                      (state SUCCEEDED))
 =>
  (span-end ?iteration-span)
  (bind ?node
    (modify ?node (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-retry-node-child-failure-no-more-tries
  "Child failed, no more tries left, fail retry node."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type RETRY) (state RUNNING|CANCELING)
                               (retry-max-tries ?max-tries&~0)
                               (retry-num-tries ?num-tries&
                                                :(>= ?num-tries ?max-tries))
                               (retry-counter-blackboard-key ?key)
                               (retry-child-id ?retry-child-id)
                               (retry-recovery-id ?retry-recovery-id)
                               (iteration-span-reference-id ?iteration-span))
  (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id) (state FAILED)
                      (extended-status-proto-id ?context-es))

  ; Ensure that there is either no recovery-child or it is not still somehow
  ; active. This can only happen in recovery context as otherwise when max tries
  ; have been reached no recovery is started.
  (not (behavior-tree-node (tree-id ?tree-id) (id ?retry-recovery-id)
    (state SELECTED|READY|RUNNING|EVALUATING-CONDITION|CANCELING-CONDITION|CANCELING)))
 =>
  (span-end-failure ?iteration-span ?*TRACING-STATUS-ABORTED* "Do child failed")
  (bind ?node (modify ?node
    (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))

  (bind ?es-proto
    (behavior-tree-node-collect-extended-status ?node 31300 ?context-es))
  (bind ?message (str-cat "Retry node exhausted all " ?max-tries " tries"))
  (if (eq (pb-get-field ?es-proto "status_code.component")
          "ai.intrinsic.executive") then
    ; Only set user message when the generated proto is set from the executive
    ; (this could be a user defined emit proto, then leave unchanged)
    (extended-status-set-message ?es-proto USER ?message)
  )

  (behavior-tree-set-node-failed ?node EXECUTION ?message
                                 EXTENDED-STATUS-SET ?es-proto)
)

(defrule behavior-tree-retry-node-child-failure-no-recovery
  "Child failed, reset to SELECTED, reset child"
  (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (condition-id ?condition-id)
                               (type RETRY) (state RUNNING)
                               (retry-max-tries ?max-tries)
                               (retry-num-tries ?num-tries&
                                                :(or (= ?max-tries 0)
                                                     (< ?num-tries ?max-tries)))
                               (retry-child-id ?retry-child-id)
                               (retry-recovery-id 0)
                               (iteration-span-reference-id ?iteration-span))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                                (state FAILED)
                                (extended-status-proto-id ?context-es))
 =>
  (span-end-failure ?iteration-span ?*TRACING-STATUS-ABORTED* "Do child failed")
  (bind ?node
    (modify ?node (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))
  (if (neq ?condition-id nil) then
    (behavior-tree-condition-reset ?condition-id ?op-name FALSE)
  )

  (bind ?es-proto
    (behavior-tree-node-collect-extended-status ?node 31300 ?context-es))
  (bind ?node (modify ?node (extended-status-proto-id ?es-proto)))

  (behavior-tree-select-node ?node)
  (behavior-tree-node-reset ?child ?op-name)
)

(defrule behavior-tree-retry-node-child-failure-with-recovery
  "Child failed, run recovery."
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (condition-id ?condition-id)
                               (type RETRY) (state RUNNING)
                               (retry-max-tries ?max-tries)
                               (retry-num-tries ?num-tries&
                                                :(or (= ?max-tries 0)
                                                     (< ?num-tries ?max-tries)))
                               (retry-child-id ?retry-child-id)
                               (retry-recovery-id ?retry-recovery-id&~0))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                                (state FAILED)
                                (extended-status-proto-id ?context-es))
  ?recovery <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-recovery-id)
                                   (state ACCEPTED))
 =>
  (bind ?es-proto
    (behavior-tree-node-collect-extended-status ?node 31300 ?context-es))
  (modify ?node (extended-status-proto-id ?es-proto))

  (behavior-tree-select-node ?recovery)
)

(defrule behavior-tree-retry-node-recovery-success
  "A retry node continues the next iteration if its recovery has succeeded within the max tries."
  (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type RETRY) (state RUNNING)
                               (condition-id ?condition-id)
                               ; Checking for max-tries is only relevant in a
                               ; recovery context. Normal execution will never
                               ; start a recovery when max-tries are reached.
                               ; Thus add this check, so that a recovery-child
                               ; from a operation recovery does not start a new
                               ; iteration. Instead
                               ; behavior-tree-retry-node-child-failure-no-more-tries
                               ; fails the node.
                               (retry-max-tries ?max-tries)
                               (retry-num-tries ?num-tries&
                                                :(or (= ?max-tries 0)
                                                     (< ?num-tries ?max-tries)))
                               (retry-child-id ?retry-child-id)
                               (retry-recovery-id ?retry-recovery-id)
                               (iteration-span-reference-id ?iteration-span))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                                (state FAILED))
  ?recovery <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-recovery-id)
                                   (state SUCCEEDED))
 =>
  (span-end ?iteration-span)
  (bind ?node (modify ?node
    (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))
  (if (neq ?condition-id nil) then
    (behavior-tree-condition-reset ?condition-id ?op-name FALSE)
  )
  (behavior-tree-select-node ?node)
  (behavior-tree-node-reset ?child ?op-name)
  (behavior-tree-node-reset ?recovery ?op-name)
)

(defrule behavior-tree-retry-node-recovery-failed
  "The recovery has failed, immediately fail the retry node."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type RETRY) (state RUNNING|CANCELING)
                               (retry-counter-blackboard-key ?key)
                               (retry-num-tries ?num-tries)
                               (retry-child-id ?retry-child-id)
                               (retry-recovery-id ?retry-recovery-id)
                               (iteration-span-reference-id ?iteration-span))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                                (state FAILED))
  ?recovery <- (behavior-tree-node (tree-id ?tree-id) (id ?retry-recovery-id)
                                   (state FAILED)
                                   (extended-status-proto-id ?context-es))
 =>
  (span-end ?iteration-span)
  (bind ?node (modify ?node
    (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))

  (bind ?es-proto
    (behavior-tree-node-collect-extended-status ?node 31300 ?context-es))
  (bind ?message (str-cat "Recovery failed after try number " ?num-tries))
  (if (eq (pb-get-field ?es-proto "status_code.component")
          "ai.intrinsic.executive") then
    ; Only set user message when the generated proto is set from the executive
    ; (this could be a user defined emit proto, then leave unchanged)
    (extended-status-set-message ?es-proto USER ?message)
  )

  (behavior-tree-set-node-failed ?node EXECUTION "Recovery failed"
                                 EXTENDED-STATUS-SET ?es-proto)
)

(defrule behavior-tree-retry-node-suspend
  "Suspend node when tree suspends and children are inactive."
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type RETRY) (state ?state&RUNNING)
                               (retry-max-tries ?max-tries)
                               (retry-num-tries ?num-tries)
                               (retry-child-id ?retry-child-id)
                               (retry-recovery-id ?retry-recovery-id))

  ; Retry child must not be succeeded -> The node should succeed instead.
  (not (behavior-tree-node (tree-id ?tree-id)
                           (id ?retry-child-id)
                           (state SUCCEEDED)))
  ; Recovery must not be failed -> The node should fail instead.
  (not (behavior-tree-node (tree-id ?tree-id)
                           (id ?retry-recovery-id)
                           (state FAILED)))
  ; No child can be actively executing. Also no child can be canceling/canceled
  ; - in that case the node should become CANCELED.
  (not (behavior-tree-node (tree-id ?tree-id)
                           (id ?retry-child-id|?retry-recovery-id)
                           (state EVALUATING-CONDITION|RUNNING|
                                  CANCELING|CANCELED)))
  (or
    ; This is not the final iteration
    (test (or (= ?max-tries 0) (< ?num-tries ?max-tries)))

    ; If it is the final iteration the child must not have failed
    (not (behavior-tree-node (tree-id ?tree-id)
                             (id ?retry-child-id)
                             (state FAILED)))
  )
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-retry-node-resume
  "Resume node if tree becomes RUNNING again."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?id) (tree-id ?tree-id)
                               (parent-id ?parent-id)
                               (type RETRY) (state SUSPENDED)
                               (suspended-from-state ?resume-state))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-retry-node-canceled
  "Canceled the node if its child is waiting or failed."
  ?node <- (behavior-tree-node (tree-id ?tree-id) (type RETRY) (state CANCELING)
                               (retry-child-id ?retry-child-id)
                               (retry-recovery-id ?retry-recovery-id)
                               (retry-max-tries ?max-tries)
                               (retry-num-tries ?num-tries))

  (or
   ; The child node was not yet started or already canceled
   (exists (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                               (state ACCEPTED|CANCELED)))

   ; The child has failed - but not in the final iteration and there is no
   ; recovery
   (and (exists (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                                    (state FAILED)))
        (test (or (= ?max-tries 0) (< ?num-tries ?max-tries)))
        (test (= ?retry-recovery-id 0)))

   ; Or the child had failed - but not in the final iteration, and then the
   ; recovery was canceled, not started or succeeded.
   ; The recovery being succeeded means the next iteration has not started yet
   ; and neither recovery not child are active.
   (and (exists (behavior-tree-node (tree-id ?tree-id) (id ?retry-child-id)
                                    (state FAILED)))
        (exists (behavior-tree-node (tree-id ?tree-id)
                                       (id ?retry-recovery-child-id)
                                       (state ACCEPTED|CANCELED|SUCCEEDED)))
        (test (or (= ?max-tries 0) (< ?num-tries ?max-tries))))
  )
 =>
  (behavior-tree-set-node-canceled ?node)
)

(defrule behavior-tree-retry-node-cancel
  "Cancels the node's in-flight child."
  (behavior-tree-node (tree-id ?tree-id) (type RETRY) (state CANCELING)
                      (retry-child-id ?retry-child-id)
                      (retry-recovery-id ?retry-recovery-id))
  ?child <- (behavior-tree-node (tree-id ?tree-id)
                                (id ?retry-child-id|?retry-recovery-id)
                                (state SELECTED|READY|RUNNING|SUSPENDED))
 =>
  (modify ?child (state CANCELING)
          (canceling-span-reference-id (tracing-start-canceling-span ?child)))
)
