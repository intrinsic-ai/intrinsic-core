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

; Processing rules for a node of type BRANCH.

; The BRANCH node evaluates an "if" condition and depending on the outcome runs
; either a "then" or an "else" child. The outcome is then determined by the
; outcome of the child run.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-branch-node-start
  "A branch node evaluates a condition to choose between two sub-trees."
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state READY)
                               (branch-if-id ?if-condition-id&~nil))
  ?cond <- (behavior-tree-condition (id ?if-condition-id) (state ACCEPTED)
                                    (node-id ?node-id) (tree-id ?tree-id))
 =>
  (modify ?node (state RUNNING))
  (modify ?cond (state START))
)

(defrule behavior-tree-branch-node-condition-error
  "Condition evaluation returned with an unreported error"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type BRANCH) (state RUNNING|CANCELING)
                               (branch-if-id ?condition-id&~nil))
  (behavior-tree-condition (id ?condition-id) (state ERROR)
                           (extended-status-proto-id ?context-es))
 =>
  (bind ?explanation "Condition did not provide extended status information")
  (if (<> ?context-es 0) then (bind ?explanation (pb-tostring ?context-es)))
  (printout warn "Condition '" ?condition-id "' errored: " ?explanation crlf)

  (behavior-tree-set-node-failed ?node EXECUTION "Branch condition failed."
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
)

(defrule behavior-tree-branch-node-if-satisfied-then
  "The branch node's if-condition is satisfied, select 'then' child."
  (behavior-tree (id ?tree-id) (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type BRANCH) (state RUNNING)
                      (branch-if-id ?if-condition-id)
                      (branch-then-id ?branch-then-id&~0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied TRUE))
  ?child <- (behavior-tree-node (id ?branch-then-id) (tree-id ?tree-id)
                                (state ACCEPTED))
 =>
  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-branch-node-if-satisfied-then-succeeded
  "Then branch has succeeded."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state RUNNING|CANCELING)
                               (branch-if-id ?if-condition-id)
                               (branch-then-id ?branch-then-id&~0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied TRUE))
  (behavior-tree-node (id ?branch-then-id) (tree-id ?tree-id)
                      (state SUCCEEDED))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-branch-node-if-satisfied-then-failed
  "Then branch has failed."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state RUNNING|CANCELING)
                               (branch-if-id ?if-condition-id)
                               (branch-then-id ?branch-then-id&~0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied TRUE))
  (behavior-tree-node (id ?branch-then-id) (tree-id ?tree-id)
                      (state FAILED)
                      (extended-status-proto-id ?context-es))
 =>
  (behavior-tree-set-node-failed ?node EXECUTION "Then child failed"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
)

(defrule behavior-tree-branch-node-if-satisfied-then-empty
  "The branch node's if-condition is satisfied, 'then' is empty"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state RUNNING|CANCELING)
                               (branch-if-id ?if-condition-id)
                               (branch-then-id 0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied TRUE))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-branch-node-if-unsatisfied-else
  "The branch node's if-condition is unsatisfied, select 'else' child."
  (behavior-tree (id ?tree-id) (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type BRANCH) (state RUNNING)
                      (branch-if-id ?if-condition-id)
                      (branch-else-id ?branch-else-id&~0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied FALSE))
  ?child <- (behavior-tree-node (id ?branch-else-id) (tree-id ?tree-id)
                                (state ACCEPTED))
 =>
  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-branch-node-if-unsatisfied-else-succeeded
  "Else branch has succeeded."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state RUNNING|CANCELING)
                               (branch-if-id ?if-condition-id)
                               (branch-else-id ?branch-else-id&~0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied FALSE))
  (behavior-tree-node (id ?branch-else-id) (tree-id ?tree-id)
                      (state SUCCEEDED))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-branch-node-if-unsatisfied-else-failed
  "Else branch has failed."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state RUNNING|CANCELING)
                               (branch-if-id ?if-condition-id)
                               (branch-else-id ?branch-else-id&~0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied FALSE))
  (behavior-tree-node (id ?branch-else-id) (tree-id ?tree-id)
                      (state FAILED)
                      (extended-status-proto-id ?context-es))
 =>
  (behavior-tree-set-node-failed ?node EXECUTION "Else child failed"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
)

(defrule behavior-tree-branch-node-if-unsatisfied-else-empty
  "The branch node's if-condition is unsatisfied, 'else' is empty"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state RUNNING|CANCELING)
                               (branch-if-id ?if-condition-id)
                               (branch-else-id 0))
  (behavior-tree-condition (id ?if-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied FALSE))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-branch-node-suspend
  "Suspend node when tree suspends and children are inactive."
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type BRANCH) (state ?state&RUNNING)
                               (branch-then-id ?branch-then-id)
                               (branch-else-id ?branch-else-id))
  ; At least one of then or else node exists (otherwise would succeed already)
  (test (or (<> ?branch-then-id 0) (<> ?branch-else-id 0)))
  ; No then or else node that is still processing or has already finished
  (not (behavior-tree-node (id ?branch-then-id) (tree-id ?tree-id)
                           (parent-id ?node-id)
                           (state EVALUATING-CONDITION|RUNNING|SUCCEEDED|FAILED)))
  (not (behavior-tree-node (id ?branch-else-id) (tree-id ?tree-id)
                           (parent-id ?node-id)
                           (state EVALUATING-CONDITION|RUNNING|SUCCEEDED|FAILED)))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-branch-node-resume
  "Resume node if tree becomes RUNNING again."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?id) (tree-id ?tree-id)
                               (parent-id ?parent-id)
                               (type BRANCH) (state SUSPENDED)
                               (suspended-from-state ?resume-state))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-branch-node-cancel
  "Cancels the node's in-flight child."
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type BRANCH)
                      (state CANCELING))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                                (state SELECTED|READY|RUNNING|SUSPENDED))
 =>
  (modify ?child (state CANCELING)
          (canceling-span-reference-id (tracing-start-canceling-span ?child)))
)

(defrule behavior-tree-branch-node-cancel-in-condition
  "Cancels the node while still evaluating the if condition."
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type BRANCH)
                      (branch-if-id ?if-condition-id)
                      (state CANCELING))
  ?cond <- (behavior-tree-condition (id ?if-condition-id)
                                    (state ~ACCEPTED&~FINISHED&
                                           ~CANCELING&~CANCELED)
                                    (node-id ?node-id) (tree-id ?tree-id))
 =>
  (modify ?cond (state CANCELING))
)

(defrule behavior-tree-branch-node-canceled
  "Cancels the node if its condition is not started or is fully evaluated and
  all children are waiting."
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type BRANCH)
                               (branch-if-id ?if-condition-id)
                               (state CANCELING))
  (behavior-tree-condition (id ?if-condition-id)
                           (state ACCEPTED|FINISHED|CANCELED)
                           (node-id ?node-id) (tree-id ?tree-id))
  ; All children are either accepted, or canceled.
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~ACCEPTED&~CANCELED)))
 =>
  (behavior-tree-set-node-canceled ?node)
)
