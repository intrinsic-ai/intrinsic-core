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

; Processing rules for a behavior tree node of type PARALLEL

; A PARALLEL node selects all its children simultaneously such that they run in
; parallel. The node succeeds if all children have succeeded. If a child fails,
; the node fails.

; ----------------------------------- RULES -----------------------------------
(defrule behavior-tree-parallel-node-start
  "A parallel node directly switches to running when ready"
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type PARALLEL) (state READY)
                               (condition-id ?condition-id))
  (or (test (eq ?condition-id nil))
      (exists (behavior-tree-condition (id ?condition-id) (satisfied TRUE))))
 =>
  (modify ?node (state RUNNING))
)

(defrule behavior-tree-parallel-node-select-child
  "Select child if no other child has failed yet"
  (behavior-tree (id ?tree-id) (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type PARALLEL) (state RUNNING))
  ?child <- (behavior-tree-node (id ?child-id) (tree-id ?tree-id)
                                (parent-id ?node-id) (state ACCEPTED))
 =>
  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-parallel-node-success
  "A parallel node succeeds if all children have succeeded"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type PARALLEL) (state RUNNING|CANCELING))
  ; Double negation to check that all children have succeeded
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~SUCCEEDED)))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

; TODO(b/170385511): Preempt children of failed PARALLEL behavior tree node
(defrule behavior-tree-parallel-node-failure
  "A child failed, let the parallel node fail"
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type PARALLEL) (state ?state&RUNNING|CANCELING))
  ; Wait until all children have completed or haven't started
  ; -> no child is still active
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~ACCEPTED&~SUCCEEDED&~FAILED&~CANCELED)))
  ; If the node is running only fail if each child had a chance to start:
  ; Thus make sure that either the node is already CANCELING or that
  ; there is no ACCEPTED child.
  (or
    (test (eq ?state CANCELING))
    (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                             (state ACCEPTED)))
  )
  (exists (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state FAILED)))
 =>
  (bind ?failed-nodes-str (create$))
  (bind ?context-es-protos (create$))
  (do-for-all-facts ((?child behavior-tree-node))
      (and (eq ?child:tree-id ?tree-id)
           (eq ?child:parent-id ?node-id)
           (eq ?child:state FAILED))
    (if (<> ?child:extended-status-proto-id 0) then
      (bind ?context-es-protos
        (append$ ?context-es-protos ?child:extended-status-proto-id))
    )
    (if (neq ?child:name "")
      then
        (bind ?failed-nodes-str
          (append$ ?failed-nodes-str
            (str-cat "'" ?child:name "' (Index: " ?child:index ")")))
      else
        (bind ?failed-nodes-str
          (append$ ?failed-nodes-str (str-cat "Index: " ?child:index)))
    )
  )
  (bind ?message
    (str-cat "The following children failed: "
      (str-join ", " ?failed-nodes-str)))

  (bind ?es-template-proto (extended-status-create 31200 ERROR))
  (extended-status-set-message ?es-template-proto USER ?message)
  (extended-status-set-related-to ?es-template-proto ?tree ?node)

  (bind ?es-proto
    (behavior-tree-node-create-extended-status-from-contexts
      ?node ?es-template-proto ?context-es-protos))

  (behavior-tree-set-node-failed ?node EXECUTION ?message
                                 EXTENDED-STATUS-SET ?es-proto)
)

(defrule behavior-tree-parallel-node-suspend
  "Suspend node when tree suspends and children are inactive."
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type PARALLEL) (state ?state&RUNNING))
  ; No node is still busy, or has failed (then the failure rule will fire)
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state EVALUATING-CONDITION|RUNNING|FAILED)))
  ; If all child nodes succeeded the success rule will fire
  (exists (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state ~SUCCEEDED)))

 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-parallel-node-resume
  "Resume node if tree becomes RUNNING again."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?id) (tree-id ?tree-id)
                               (parent-id ?parent-id)
                               (type PARALLEL) (state SUSPENDED)
                               (suspended-from-state ?resume-state))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-parallel-node-cancel
  "Cancels the node's in-flight children"
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type PARALLEL)
                      (state CANCELING))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                                (state SELECTED|READY|RUNNING|SUSPENDED))
 =>
  (modify ?child (state CANCELING)
          (canceling-span-reference-id (tracing-start-canceling-span ?child)))
)

(defrule behavior-tree-parallel-node-canceled
  "Cancels the node if its children are waiting or finished.
  We could get a mixture of waiting and finished nodes if the parallel node is
  cancelled when some but not all of the child nodes have started."
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type PARALLEL) (state CANCELING))
  ; All children are either accepted, succeeded, or canceled
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~ACCEPTED&~SUCCEEDED&~CANCELED)))
  ; At least one has not succeeded (otherwise the success rule would apply
  (exists (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state ~SUCCEEDED)))
 =>
  (behavior-tree-set-node-canceled ?node)
)
