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

; Step-wise execution handling for BehaviorTrees.

; During step-wise execution, the execution is automatically suspended when
; some node is selected (other than the root node). Only applies to the process
; tree.

; --------------------------------- FUNCTIONS ---------------------------------
(deffunction behavior-tree-stepwise-step-through-recursively (?tree-id ?node-id)

  (do-for-fact ((?node behavior-tree-node))
      (and (eq ?node:tree-id ?tree-id) (eq ?node:id ?node-id))

    (if (eq ?node:type SUB-TREE)
     then ; a sub-tree node has a different tree-id, call for tree root
       (do-for-fact ((?sub-tree behavior-tree))
           (eq ?sub-tree:id ?node:sub-tree-id)

         (behavior-tree-stepwise-step-through-recursively
           ?sub-tree:id ?sub-tree:root)
       )
     else ; for all other nodes simply mark children
      (delayed-do-for-all-facts ((?child behavior-tree-node))
          (and (eq ?child:tree-id ?node:tree-id) (eq ?child:parent-id ?node:id))

        (behavior-tree-stepwise-step-through-recursively
          ?child:tree-id ?child:id)
      )
    )

    (modify ?node (stepwise-state STEP-THROUGH))
  )
  (return TRUE)
)

(deffunction behavior-tree-stepwise-step-through-tree (?top-tree-id)
  "This will take the node in the tree that was suspended when selected, i.e.,
   the one where the behavior-tree-stepwise-suspend rule fired for last, and
   mark that and its sub-tree(s) for step through recursively."
  (delayed-do-for-all-facts ((?node behavior-tree-node))
    (and (eq ?top-tree-id (behavior-tree-get-top-level-tree-id ?node:tree-id))
         (eq ?node:state SUSPENDED)
         (eq ?node:suspended-from-state SELECTED)
         (not (any-factp ((?child behavior-tree-node))
                         (and (eq ?child:parent-id ?node:id)
                              (eq ?child:state SUSPENDED)))))

    (behavior-tree-stepwise-step-through-recursively ?node:tree-id ?node:id)
  )
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-stepwise-suspend
  "Suspend upon selection of a non-root node (process tree only)"
  (declare (salience ?*SALIENCE-BREAKPOINT*))
  (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name)
                 (start-node-id ?start-node-id))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (id ?node-id) (state SELECTED)
                               (stepwise-state NONE))
  ?op <- (operation-envelope (name ?op-name) (execution-mode STEP-WISE)
                             (start-tree-id ?start-tree-id))
  ; Do not suspend for the start node, we want to take at least one step when
  ; the execution is started, the user could otherwise simply have not started
  ; the tree. If they really want to stop here, they can set a BEFORE
  ; breakpoint.
  (test (or (neq ?start-tree-id ?tree-id) (neq ?node-id ?start-node-id)))
 =>
  (modify ?node (stepwise-state PROCESSED)
          (state SUSPENDED) (suspended-from-state SELECTED)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
  (assert (operation-update-state (operation-name ?op-name)
                                  (target-state SUSPENDING)))
)
