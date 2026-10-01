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

; Processing rules for a node of type SUB-TREE

; The SUB-TREE node has only one child, if it SUCCEEDs, the node SUCCEEDs,
; if the child FAILs, the node FAILs.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-sub-tree-node-start
  "A SUB-TREE node switches to RUNNING immediately when READY"
  (behavior-tree (state RUNNING) (id ?tree-id))
  ?node <- (behavior-tree-node (type SUB-TREE) (state READY)
                               (tree-id ?tree-id) (condition-id ?condition-id))
  (or (test (eq ?condition-id nil))
      (exists (behavior-tree-condition (id ?condition-id) (satisfied TRUE))))
 =>
  (modify ?node (state RUNNING))
)

(defrule behavior-tree-sub-tree-node-select-child
  "When the SUB-TREE node is set to RUNNING, its child is selected."
  ?tree <- (behavior-tree (state RUNNING) (id ?tree-id))
  (behavior-tree-node (type SUB-TREE) (state RUNNING)
                      (sub-tree-id ?sub-tree-id)
                      (span-reference-id ?parent-span-id))
  ?sub-tree <- (behavior-tree (id ?sub-tree-id) (state ACCEPTED))
 =>
  (bind ?sub-tree-span
    (tracing-cc-start-tree-span ?sub-tree-id ?parent-span-id))
  (modify ?sub-tree (state RUNNING) (span-reference-id ?sub-tree-span))
)

(defrule behavior-tree-sub-tree-node-succeeded
  "Succeed if root node of sub-tree succeeds"
  (behavior-tree (state RUNNING|CANCELING|SUSPENDING) (id ?tree-id))
  ?node <- (behavior-tree-node (type SUB-TREE) (state RUNNING|CANCELING)
                               (sub-tree-id ?sub-tree-id))
  (behavior-tree (state SUCCEEDED) (id ?sub-tree-id))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-sub-tree-node-failed
  "Succeed if root node of sub-tree fails"
  (behavior-tree (state RUNNING|CANCELING|SUSPENDING) (id ?tree-id)
                 (operation-name ?operation-name))
  ?node <- (behavior-tree-node (type SUB-TREE)
                               (state ?tree-state&RUNNING|CANCELING)
                               (tree-id ?tree-id)
                               (sub-tree-id ?sub-tree-id))
  (behavior-tree (state FAILED) (id ?sub-tree-id) (root ?sub-root-id)
                 (operation-name ?operation-name))
  (behavior-tree-node (tree-id ?sub-tree-id) (id ?sub-root-id)
                      (extended-status-proto-id ?context-es))
 =>
  (behavior-tree-set-node-failed ?node EXECUTION "Sub-tree failed"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
)

(defrule behavior-tree-sub-tree-node-suspend
  "Initiate suspending the sub-tree."
  (declare (salience ?*SALIENCE-SUSPENDING*))
  ?tree <- (behavior-tree (state SUSPENDING) (id ?tree-id))
  ?node <- (behavior-tree-node (type SUB-TREE) (state RUNNING)
                               (tree-id ?tree-id) (sub-tree-id ?sub-tree-id))
  ?sub-tree <- (behavior-tree (state RUNNING) (id ?sub-tree-id))
 =>
  (if (not (behavior-tree-suspend ?sub-tree-id)) then
    (bind ?es-proto (extended-status-create 31900))
    (extended-status-set-message ?es-proto USER "Could not suspend sub-tree")
    (extended-status-set-related-to ?es-proto ?tree ?node)
    (behavior-tree-set-node-failed ?node EXECUTION "Could not suspend sub-tree"
      EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
    (pb-remove ?es-proto)
  )
)

(defrule behavior-tree-sub-tree-node-suspended
  "Transition node to SUSPENDED when the sub-tree finished suspending."
  (behavior-tree (state SUSPENDING) (id ?tree-id))
  ?node <- (behavior-tree-node (type SUB-TREE) (state ?state&RUNNING)
                               (tree-id ?tree-id)
                               (sub-tree-id ?sub-tree-id))
  (behavior-tree (state SUSPENDED) (id ?sub-tree-id))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-sub-tree-node-resume
  "Resume the sub-tree when the enclosing tree becomes RUNNING again."
  (behavior-tree (state RUNNING) (id ?tree-id) (start-node-id ?start-node-id))
  ?node <- (behavior-tree-node (id ?id) (type SUB-TREE) (state SUSPENDED)
                               (parent-id ?parent-id)
                               (tree-id ?tree-id) (sub-tree-id ?sub-tree-id)
                               (suspended-from-state ?resume-state))
  ?sub-tree <- (behavior-tree (id ?sub-tree-id)
                              (state ?sub-tree-state&ACCEPTED|SUSPENDED))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (if (eq ?sub-tree-state SUSPENDED) then (modify ?sub-tree (state RUNNING)))
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-sub-tree-node-cancel
  "Cancels the node's in-flight sub-tree."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?tree <- (behavior-tree (id ?tree-id))
  ?node <- (behavior-tree-node (type SUB-TREE) (tree-id ?tree-id)
                               (sub-tree-id ?sub-tree-id)
                               (state CANCELING))
  (behavior-tree (id ?sub-tree-id) (state RUNNING|SUSPENDING|SUSPENDED))
 =>
  (if (not (behavior-tree-cancel ?sub-tree-id)) then
    (bind ?es-proto (extended-status-create 31900))
    (extended-status-set-message ?es-proto USER "Could not cancel sub-tree")
    (extended-status-set-related-to ?es-proto ?tree ?node)
    (behavior-tree-set-node-failed ?node EXECUTION "Could not cancel sub-tree"
      EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
    (pb-remove ?es-proto)
  )
)

(defrule behavior-tree-sub-tree-node-canceled
  "Cancels the node if the sub-tree is waiting."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type SUB-TREE)
           (sub-tree-id ?sub-tree-id) (state CANCELING))
  (behavior-tree (id ?sub-tree-id) (state ACCEPTED|CANCELED))
 =>
  (behavior-tree-set-node-canceled ?node)
)
