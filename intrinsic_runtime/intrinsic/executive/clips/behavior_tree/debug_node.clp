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

; Processing rules for a node of type DEBUG

; The DEBUG node is used for debugging operations such as:
; - suspending the enclosing operation

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-debug-node-start
  "A DEBUG node switches to RUNNING immediately when READY"
  (behavior-tree (state RUNNING) (id ?tree-id))
  ?node <- (behavior-tree-node (type DEBUG) (state READY) (tree-id ?tree-id)
                               (debug-suspend ?debug-suspend))
  (operation-envelope (name ?op-name)
    (operation-tree-id ?operation-tree-id&
                       :(eq ?operation-tree-id
                            (behavior-tree-get-top-level-tree-id ?tree-id))))
 =>
  (if ?debug-suspend then
    (assert (operation-update-state (operation-name ?op-name)
                                    (target-state SUSPENDING)))
  )
  (modify ?node (state RUNNING))
)

(defrule behavior-tree-debug-node-suspend
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type DEBUG) (state ?state&RUNNING))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-debug-node-resume
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (parent-id ?parent-id)
                               (type DEBUG) (state SUSPENDED)
                               (suspended-from-state RUNNING)
                               (debug-resume-state ?resume-state))
 =>
  (switch ?resume-state
    (case SUCCEEDED then (behavior-tree-set-node-succeeded ?node))
    (case FAILED then
      (bind ?es-proto (extended-status-create 31700))
      (extended-status-set-message ?es-proto USER "Failing as configured")
      (extended-status-set-related-to ?es-proto ?tree ?node)
      (behavior-tree-set-node-failed ?node EXECUTION "Failing as configured"
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?es-proto)
    )
  )
)

(defrule behavior-tree-debug-node-resume-not-started-yet
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (parent-id ?parent-id)
                               (type DEBUG) (state SUSPENDED)
                               (suspended-from-state ?resume-state&~RUNNING))
 =>
  (modify ?node (state ?resume-state))
)

(defrule behavior-tree-debug-node-canceled
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type DEBUG)
                               (state CANCELING))
 =>
  (behavior-tree-set-node-canceled ?node)
)
