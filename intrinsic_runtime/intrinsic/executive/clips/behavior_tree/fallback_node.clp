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

; Processing rules for a behavior tree node of type FALLBACK

; A FALLBACK node selects its children one by one until the first child
; succeeded. If a child fails, it continues with the next child. The node
; succeeds if a child succeeds. It fails if all children fail.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-fallback-node-start
  "A fallback node directly switches to running when ready"
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type FALLBACK) (state READY)
                               (condition-id ?condition-id))
  (or (test (eq ?condition-id nil))
      (exists (behavior-tree-condition (id ?condition-id) (satisfied TRUE))))
 =>
  (modify ?node (state RUNNING))
)

(defrule behavior-tree-fallback-node-select-child
  "Select the first child that has not failed yet"
  (behavior-tree (id ?tree-id) (state RUNNING))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type FALLBACK) (state RUNNING))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (index ?child-index)
                                (parent-id ?node-id) (state ACCEPTED))
  (not (behavior-tree-node (index ?other-child-index&:(< ?other-child-index
                                                         ?child-index))
                           (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~FAILED)))
 =>
  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-fallback-node-success
  "A fallback node succeeds if a child has succeeded"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type FALLBACK) (state RUNNING|CANCELING))
  (exists (behavior-tree-node (id ?child-id) (tree-id ?tree-id)
                              (parent-id ?node-id) (state SUCCEEDED)))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-fallback-node-fail
  "A fallback node fails if all children have failed"
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type FALLBACK) (state RUNNING|CANCELING))
  ; Check that all children have failed by checking that there is no child
  ; that has not failed
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~FAILED)))
 =>
  (bind ?es-template-proto (extended-status-create 31100 ERROR))
  (extended-status-set-message ?es-template-proto USER "All children failed")
  (extended-status-set-related-to ?es-template-proto ?tree ?node)

  (bind ?context-es-protos (create$))
  (do-for-all-facts ((?child behavior-tree-node))
      (and (eq ?child:tree-id ?tree-id)
           (eq ?child:parent-id ?node-id)
           (eq ?child:state FAILED)
           (neq ?child:extended-status-proto-id 0))
    (bind ?context-es-protos
      (append$ ?context-es-protos ?child:extended-status-proto-id))
  )

  (bind ?es-proto
    (behavior-tree-node-create-extended-status-from-contexts
      ?node ?es-template-proto ?context-es-protos))

  (behavior-tree-set-node-failed ?node EXECUTION "All children failed"
                                 EXTENDED-STATUS-SET ?es-proto)
)

(defrule behavior-tree-fallback-node-suspend
  "Suspend node when tree suspends and children are inactive."
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type FALLBACK) (state ?state&RUNNING))
  ; No node is still busy, or succeeded
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state EVALUATING-CONDITION|RUNNING|SUCCEEDED)))
  ; There are no children or all child nodes failed
  (exists (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state ~FAILED)))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-fallback-node-resume
  "Resume node if tree becomes RUNNING again."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?id) (tree-id ?tree-id)
                               (parent-id ?parent-id)
                               (type FALLBACK) (state SUSPENDED)
                               (suspended-from-state ?resume-state))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-fallback-node-cancel
  "Cancels the node's in-flight child."
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type FALLBACK)
                      (state CANCELING))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                                (state SELECTED|READY|RUNNING|SUSPENDED))
 =>
  (modify ?child (state CANCELING)
          (canceling-span-reference-id (tracing-start-canceling-span ?child)))
)

(defrule behavior-tree-fallback-node-canceled
  "Cancels the node if all children are either waiting or failed."
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type FALLBACK)
                               (state CANCELING))
  ; All children are either accepted, failed, or canceled.
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~ACCEPTED&~FAILED&~CANCELED)))

  ; The condition above ensures expected child states, they are all in a resting
  ; state. Transitioning to FAILED is handled elsewhere - when all children have
  ; failed. Thus Cancellation succeeded if there are ACCEPTED or CANCELED
  ; children (or both).
  (exists (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                              (state ACCEPTED|CANCELED)))
 =>
  (behavior-tree-set-node-canceled ?node)
)
