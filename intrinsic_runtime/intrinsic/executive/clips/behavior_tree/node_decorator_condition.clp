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

; Processing rules for decorator conditions of behavior tree nodes

; Any node may have a condition, which must be satisfied before the node is
; executed. If a node with an unsatisfied condition is selected, it directly
; fails. Otherwise, it continues to be processed depending on the type of the
; node.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-node-decorator-condition-evaluation-start
  "A selected node with a condition must transition to EVALUATING-CONDITION."
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (condition-id ?condition-id&~nil))
  ?cond <- (behavior-tree-condition (id ?condition-id) (state ACCEPTED))
 =>
  (bind ?condition-span (tracing-start-evaluating-condition-span ?node))
  (modify ?node (state EVALUATING-CONDITION)
    (condition-span-reference-id ?condition-span))
  (modify ?cond (state START))
)

(defrule behavior-tree-node-decorator-condition-cancel
  "A selected node with a condition must transition to EVALUATING-CONDITION."
  (behavior-tree (id ?tree-id) (state CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state EVALUATING-CONDITION)
                               (condition-id ?cond-id))
  ?cond <- (behavior-tree-condition (id ?cond-id)
                                    (state ~START&~FINISHED&~CANCELING&~CANCELED))
 =>
  (modify ?node (state CANCELING-CONDITION))
  (modify ?cond (state CANCELING))
)

(defrule behavior-tree-node-decorator-condition-canceled
  "A selected node with a condition must transition to EVALUATING-CONDITION."
  (behavior-tree (id ?tree-id) (state CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state CANCELING-CONDITION)
                               (condition-id ?cond-id))
  (behavior-tree-condition (id ?cond-id) (state START|CANCELED|FINISHED))
 =>
  (behavior-tree-set-node-canceled ?node)
)

(defrule behavior-tree-node-decorator-condition-evaluation-skip
  "A selected node with no condition can immediatly transition to READY."
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (condition-id nil))
 =>
  (modify ?node (state READY))
)

(defrule behavior-tree-node-decorator-condition-satisfied
  "Node can advance to READY state with satisfied condition"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (state EVALUATING-CONDITION)
                               (condition-id ?condition-id&~nil))
  (behavior-tree-condition (id ?condition-id) (state FINISHED)
                           (satisfied TRUE))
 =>
  (tracing-end-evaluating-condition-span ?node)
  (modify ?node (state READY)
    (condition-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-node-decorator-condition-unsatisfied
  "Node failed state with unsatisfied condition"
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (state EVALUATING-CONDITION)
                               (condition-id ?condition-id&~nil))
  (behavior-tree-condition (id ?condition-id) (state FINISHED)
                           (satisfied FALSE))
 =>
  (tracing-end-evaluating-condition-span-failure ?node
    ?*TRACING-STATUS-ABORTED* "Condition unsatisfied")
  (bind ?node (modify ?node
    (condition-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))

  (bind ?es-proto (extended-status-create 31001))
  (extended-status-set-message ?es-proto USER
    "Failing as the decorator condition was unsatisfied")
  (extended-status-set-related-to ?es-proto ?tree ?node)
  (behavior-tree-set-node-failed ?node CONDITION "Condition unsatisfied"
    EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
  (pb-remove ?es-proto)
)

(defrule behavior-tree-node-decorator-condition-error
  "Condition evaluation returned with an unreported error"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (state EVALUATING-CONDITION)
                               (condition-id ?condition-id&~nil))
  (behavior-tree-condition (id ?condition-id) (state ERROR)
                           (extended-status-proto-id ?context-es))
 =>
  (bind ?error-msg (str-cat "Condition '" ?condition-id "' errored"))
  (tracing-end-evaluating-condition-span-failure ?node
    ?*TRACING-STATUS-INTERNAL* ?error-msg)
  (bind ?node (modify ?node
    (condition-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))
  (behavior-tree-set-node-failed ?node CONDITION "Condition evaluation failed."
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
)

(defrule behavior-tree-node-decorator-condition-suspended
  "Mark node suspended when decorator condition is suspended"
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (state EVALUATING-CONDITION)
                               (condition-id ?condition-id&~nil))
  (behavior-tree-condition (id ?condition-id) (state SUSPENDED))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state EVALUATING-CONDITION))
)
