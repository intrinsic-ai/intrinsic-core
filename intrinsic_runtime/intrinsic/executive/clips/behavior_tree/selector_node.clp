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

; Processing rules for a behavior tree node of type SELECTOR

; A SELECTOR node selects its children one by one until the first child
; succeeded. If a child CONDITION fails, it continues with the next child. If
; a selected child (for which the decorator condition was satisfied) causes an
; execution failure the selector node will fail. The node succeeds if a child
; succeeds. It also fails if all child conditions fail.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-selector-node-start
  "A selector node directly switches to running when ready"
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type SELECTOR) (state READY)
                               (condition-id ?condition-id))
  (or (test (eq ?condition-id nil))
      (exists (behavior-tree-condition (id ?condition-id) (satisfied TRUE))))
 =>
  (modify ?node (state RUNNING))
)

(defrule behavior-tree-selector-node-select-child
  "Select the first child that has not failed yet"
  (behavior-tree (id ?tree-id) (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type SELECTOR) (state RUNNING))
  ?child <- (behavior-tree-node (index ?child-index) (tree-id ?tree-id)
                                (parent-id ?node-id) (state ACCEPTED))
  (not (behavior-tree-node (index ?other-child-index&:(< ?other-child-index
                                                         ?child-index))
                           (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~FAILED)))
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state FAILED) (failure-reason EXECUTION)))
 =>
  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-selector-node-success
  "A selector node succeeds if a child has succeeded"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type SELECTOR) (state RUNNING|CANCELING))
  (exists (behavior-tree-node (id ?child-id) (tree-id ?tree-id)
                              (parent-id ?node-id) (state SUCCEEDED)))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-selector-node-fails-execute
  "A selector node fails if any child fails in execution"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type SELECTOR) (state RUNNING|CANCELING))
  (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                      (state FAILED) (failure-reason EXECUTION|UNKNOWN)
                      (extended-status-proto-id ?context-es))
 =>
  (behavior-tree-set-node-failed ?node EXECUTION "Child failed in execution"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
)

(defrule behavior-tree-selector-node-fail-all-conditions
  "A selector node fails if all children have failed to be selected"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type SELECTOR) (state RUNNING)
                               (on-failure-emit-extended-status-proto-id
                                 ?emit-proto))
  ; Check that all children have failed by checking that there is no child
  ; that has not failed and that no child is not in condition failure
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~FAILED)))
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (failure-reason ~CONDITION)))
 =>
  (bind ?es-proto 0)
  (if (= ?emit-proto 0) then
    (bind ?es-proto (extended-status-create 31400 ERROR))
    (extended-status-set-message ?es-proto USER "No child was selected")
  )

  (behavior-tree-set-node-failed ?node EXECUTION "No child was selected"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
)

(defrule behavior-tree-selector-node-suspend
  "Suspend node when tree suspends and children are inactive."
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type SELECTOR) (state ?state&RUNNING))
  ; No running (will enter resting state) or succeeded node (will trigger
  ; success rule)
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state EVALUATING-CONDITION|RUNNING|SUCCEEDED)))
  ; No node with execution failure (will trigger fails-execute)
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state FAILED) (failure-reason EXECUTION)))
  ; There is at least one child that hasn't failed (otherwise fail-all triggers)
  (exists (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state ~FAILED)))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-selector-node-resume
  "Resume node if tree becomes RUNNING again."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?id) (tree-id ?tree-id)
                               (parent-id ?parent-id)
                               (type SELECTOR) (state SUSPENDED)
                               (suspended-from-state ?resume-state))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-selector-node-cancel
  "Cancels the node's in-flight child."
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type SELECTOR)
                      (state CANCELING))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                                (state SELECTED|READY|RUNNING|SUSPENDED))
 =>
  (modify ?child (state CANCELING)
          (canceling-span-reference-id (tracing-start-canceling-span ?child)))
)

(defrule behavior-tree-selector-node-canceled
  "Cancels the node if all children are either waiting or failed."
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type SELECTOR)
                               (state CANCELING))
  ; All children are either accepted, canceled, or failed
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~ACCEPTED&~CANCELED&~FAILED)))

  ; All children that have failed must have failed with reason CONDITION
  (forall (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state FAILED))
    (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                        (state FAILED) (failure-reason CONDITION)))

  ; At least one child is not finished or has been cancelled
  ; (If all children have failed even with reason CONDITION, the node should
  ; fail)
  (exists (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state ACCEPTED|CANCELED)))
 =>
  (behavior-tree-set-node-canceled ?node)
)
