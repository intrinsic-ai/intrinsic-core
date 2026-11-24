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

; Breakpoint handling for BehaviorTrees.

; A breakpoint suspends a behavior tree. It is associated with a node and can
; trigger suspending before or after the marked node has been executed.

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate behavior-tree-breakpoint-update
  (slot operation-name (type STRING))
  (slot op (type SYMBOL) (allowed-values ADD REMOVE CLEAR) (default ?NONE))
  (slot tree-id (type SYMBOL) (default ?NONE))
  (slot node-id (type INTEGER) (default ?NONE))
  (slot add-type (type SYMBOL) (allowed-values NONE BEFORE AFTER))
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-breakpoint-trigger-before
  "A node is marked with a breakpoint to trigger BEFORE execution of the node."
  (declare (salience ?*SALIENCE-BREAKPOINT*))
  (behavior-tree (id ?tree-id) (operation-name ?op-name) (state RUNNING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (breakpoint-type BEFORE)
                               (breakpoint-triggered FALSE))
 =>
  (modify ?node (breakpoint-triggered TRUE)
          (state SUSPENDED) (suspended-from-state SELECTED)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
  (assert (operation-update-state (operation-name ?op-name)
                                  (target-state SUSPENDING)))
)

(defrule behavior-tree-breakpoint-trigger-after
  "A node is marked with a breakpoint to trigger AFTER execution of the node."
  (declare (salience ?*SALIENCE-BREAKPOINT*))
  (behavior-tree (id ?tree-id) (operation-name ?op-name) (state RUNNING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (state ?state&SUCCEEDED|FAILED)
                               (breakpoint-type AFTER)
                               (breakpoint-triggered FALSE))
 =>
  (modify ?node (breakpoint-triggered TRUE)
          (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
  (assert (operation-update-state (operation-name ?op-name)
                                  (target-state SUSPENDING)))
)

; The following rules deal with updates received from service calls.

(defrule behavior-tree-breakpoint-update-invalid-operation
  "Update for invalid tree ID"
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (operation-name ?op-name))
  (not (operation-envelope (name ?op-name)))
 =>
  (assert (error (name BEHAVIOR-TREE-BREAKPOINT-UPDATE-INVALID-OPERATION)
                 (type RECOVERABLE)
                 (message (str-cat "Operation " ?op-name " does not exist."))))
  (retract ?uf)
)

(defrule behavior-tree-breakpoint-update-invalid-tree
  "Update for invalid tree ID"
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (operation-name ?op-name)
                                          (op ~CLEAR) (tree-id ?tree-id))
  (operation-envelope (name ?op-name) (operation-tree-id ?operation-tree-id))

  ; No tree for the given ID and operation exists
  (not (behavior-tree (id ?tree-id) (operation-name ?op-name)))
 =>
  (assert (error (name BEHAVIOR-TREE-BREAKPOINT-UPDATE-INVALID-TREE)
                 (type RECOVERABLE)
                 (message (str-cat "Tree " ?tree-id " does not exist or is not "
                                   "part of the operation " ?op-name))))
  (retract ?uf)
)

(defrule behavior-tree-breakpoint-update-invalid-node
  "Update for invalid node ID"
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (op ~CLEAR)
                                          (tree-id ?tree-id) (node-id ?node-id))
  (not (behavior-tree-node (tree-id ?tree-id) (id ?node-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-BREAKPOINT-UPDATE-INVALID-NODE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Tree " ?tree-id " has no node " ?node-id))))
  (retract ?uf)
)

(defrule behavior-tree-breakpoint-add-already-exists
  "Trying to add an already existing breakpoint"
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (op ADD)
                                          (tree-id ?tree-id) (node-id ?node-id))
  (behavior-tree-node (tree-id ?tree-id) (id ?node-id)
                      (breakpoint-type ?type&~NONE))
 =>
  (assert (error (name BEHAVIOR-TREE-BREAKPOINT-ADD-ALREADY-EXISTS)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Node " ?tree-id ":" ?node-id
                                   " already has breakpoint of type " ?type))))
  (retract ?uf)
)

(defrule behavior-tree-breakpoint-add-invalid-type
  "Trying to add a breakpoint with an invalid (unspecified) type"
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (op ADD)
                                          (tree-id ?tree-id) (node-id ?node-id)
                                          (add-type NONE))
 =>
  (assert (error (name BEHAVIOR-TREE-BREAKPOINT-ADD-INVALID-TYPE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Node " ?tree-id ":" ?node-id
                                   " breakpoint update has no type"))))
  (retract ?uf)
)

(defrule behavior-tree-breakpoint-add
  "Add a breakpoint."
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (operation-name ?op-name) (op ADD)
                                          (add-type ?type&BEFORE|AFTER)
                                          (tree-id ?tree-id) (node-id ?node-id))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (id ?node-id)
                               (breakpoint-type NONE))
 =>
  (printout t "Adding " ?type " breakpoint for " ?tree-id ":" ?node-id crlf)
  (modify ?node (breakpoint-type ?type) (breakpoint-triggered FALSE))
  (retract ?uf)
)

(defrule behavior-tree-breakpoint-remove
  "Remove a breakpoint"
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (operation-name ?op-name) (op REMOVE)
                                          (tree-id ?tree-id) (node-id ?node-id))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (id ?node-id))
 =>
  (printout t "Removing breakpoint for " ?tree-id ":" ?node-id crlf)
  (modify ?node (breakpoint-type NONE) (breakpoint-triggered FALSE))
  (retract ?uf)
)

(defrule behavior-tree-breakpoint-clear
  "Clear all breakpoints."
  (declare (salience ?*SALIENCE-BREAKPOINT-UPDATE*))
  ?uf <- (behavior-tree-breakpoint-update (operation-name ?op-name) (op CLEAR))
  (behavior-tree (id ?operation-tree-id) (operation-name ?op-name))
 =>
  (delayed-do-for-all-facts ((?node behavior-tree-node))
      (and (eq ?operation-tree-id
               (behavior-tree-get-top-level-tree-id ?node:tree-id))
           (neq ?node:breakpoint-type NONE))
    (modify ?node (breakpoint-type NONE) (breakpoint-triggered FALSE))
  )
  (retract ?uf)
)
