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

; Selective execution of behavior tree nodes.
;
; Depending on the execution settings of a node, it might not be intended to be
; executed with the normal node semantics. In this case, the rules in this file
; take over.
;
; A node can be disabled. It is then automatically SUCCEEDED or FAILED and no
; node semantics are executed. The outcome state of a node is either
; pre-determined by a user or automatically determined based on the node's
; parent.

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate behavior-tree-selective-execution-settings-update
  (slot operation-name (type STRING))
  (slot tree-id (type SYMBOL) (default ?NONE))
  (slot node-id (type INTEGER) (default ?NONE))
  (slot execution-mode (type SYMBOL) (allowed-values NORMAL DISABLED)
                       (default ?NONE))
  (slot execution-mode-result-state (type SYMBOL)
                                    (allowed-values AUTO SUCCEEDED FAILED)
                                    (default ?NONE))
)

; ------------------------------- FUNCTIONS -----------------------------------

(deffunction behavior-tree-node-execution-add-disabled-span-attribute (?node)
  (bind ?span-id (fact-slot-value ?node span-reference-id))
  (if (<> ?span-id 0) then
    (span-add-attribute ?span-id "bt_execution_settings" "disabled")
  )
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-node-execution-skip-disabled-with-result
  "A DISABLED node with a result state transitions to the result state."
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION*))
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (execution-mode DISABLED)
                               (execution-mode-result-state ?result-state&~AUTO))
 =>
  (behavior-tree-node-execution-add-disabled-span-attribute ?node)
  (if (eq ?result-state SUCCEEDED)
    then
      (behavior-tree-set-node-succeeded ?node)
    else
      (bind ?message
        "Disabled node failed intentionally as it is configured to fail")
      (bind ?es-proto (extended-status-create 31000))
      (extended-status-set-message ?es-proto USER ?message)
      (extended-status-set-related-to ?es-proto ?tree ?node)
      (behavior-tree-set-node-failed ?node EXECUTION ?message
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?es-proto)
  )
)

(defrule behavior-tree-node-execution-skip-disabled-in-fallback
  "A DISABLED node in a FALLBACK fails."
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION*))
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (parent-id ?parent-id)
                               (execution-mode DISABLED)
                               (execution-mode-result-state AUTO))
  (behavior-tree-node (type FALLBACK) (id ?parent-id) (tree-id ?tree-id))
 =>
  (behavior-tree-node-execution-add-disabled-span-attribute ?node)

  (bind ?message
    "Disabled node failed intentionally to skip forward in fallback")
  (bind ?es-proto (extended-status-create 31000))
  (extended-status-set-message ?es-proto USER ?message)
  (extended-status-set-related-to ?es-proto ?tree ?node)
  (behavior-tree-set-node-failed ?node EXECUTION ?message
    EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
  (pb-remove ?es-proto)
)

(defrule behavior-tree-node-execution-skip-disabled-in-selector
  "A DISABLED node in a SELECTOR fails with failure-reason CONDITION."
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION*))
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (parent-id ?parent-id)
                               (execution-mode DISABLED)
                               (execution-mode-result-state AUTO))
  (behavior-tree-node (type SELECTOR) (id ?parent-id) (tree-id ?tree-id))
 =>
  (behavior-tree-node-execution-add-disabled-span-attribute ?node)

  (bind ?message
    "Disabled node failed intentionally to skip condition evaluation in selector")
  (bind ?es-proto (extended-status-create 31000))
  (extended-status-set-message ?es-proto USER ?message)
  (extended-status-set-related-to ?es-proto ?tree ?node)
  (behavior-tree-set-node-failed ?node CONDITION ?message
    EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
  (pb-remove ?es-proto)
)

(defrule behavior-tree-node-execution-skip-disabled-start
  "A DISABLED start node of a tree succeeds."
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION*))
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (id ?start-node-id)
                               (execution-mode DISABLED)
                               (execution-mode-result-state AUTO))
 =>
  (behavior-tree-node-execution-add-disabled-span-attribute ?node)
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-node-execution-skip-disabled
  "A DISABLED node not handled by other specific rules succeeds."
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION*))
  (behavior-tree (id ?tree-id) (state RUNNING|SUSPENDING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (state SELECTED)
                               (parent-id ?parent-id&~0)
                               (execution-mode DISABLED)
                               (execution-mode-result-state AUTO))
  (behavior-tree-node (type ~SELECTOR&~FALLBACK)
                      (id ?parent-id) (tree-id ?tree-id))
 =>
  (behavior-tree-node-execution-add-disabled-span-attribute ?node)
  (behavior-tree-set-node-succeeded ?node)
)

; The following rules deal with updates received from service calls.

(defrule behavior-tree-selective-execution-settings-update-invalid-operation
  "Update for invalid tree ID"
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION-UPDATE*))
  ?uf <- (behavior-tree-selective-execution-settings-update
           (operation-name ?op-name))
  (not (operation-envelope (name ?op-name)))
 =>
  (assert (error (name
            BEHAVIOR-TREE-SELECTIVE-EXECUTION-SETTINGS-UPDATE-INVALID-OPERATION)
                 (type RECOVERABLE)
                 (message (str-cat "Operation " ?op-name " does not exist."))))
  (retract ?uf)
)

(defrule behavior-tree-selective-execution-settings-update-invalid-tree
  "Update for invalid tree ID"
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION-UPDATE*))
  ?uf <- (behavior-tree-selective-execution-settings-update
           (operation-name ?op-name) (tree-id ?tree-id))
  (operation-envelope (name ?op-name) (operation-tree-id ?operation-tree-id))

  ; No tree for the given ID and operation name exists
  (not (behavior-tree (id ?tree-id) (operation-name ?op-name)))
 =>
  (assert (error (name
            BEHAVIOR-TREE-SELECTIVE-EXECUTION-SETTINGS-UPDATE-INVALID-TREE)
                 (type RECOVERABLE)
                 (message (str-cat "Tree " ?tree-id " does not exist or is not "
                                   "in the process tree of the operation."))))
  (retract ?uf)
)

(defrule behavior-tree-selective-execution-settings-update-invalid-node
  "Update for invalid node ID"
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION-UPDATE*))
  ?uf <- (behavior-tree-selective-execution-settings-update
           (tree-id ?tree-id) (node-id ?node-id))
  (not (behavior-tree-node (tree-id ?tree-id) (id ?node-id)))
 =>
  (assert (error (name
            BEHAVIOR-TREE-SELECTIVE-EXECUTION-SETTINGS-UPDATE-INVALID-NODE)
                 (type RECOVERABLE)
                 (message (str-cat "Tree " ?tree-id " has no node " ?node-id))))
  (retract ?uf)
)

(defrule behavior-tree-selective-execution-settings-update
  "Update selective execution settings for a node."
  (declare (salience ?*SALIENCE-SELECTIVE-EXECUTION-UPDATE*))
  ?uf <- (behavior-tree-selective-execution-settings-update
           (operation-name ?op-name)
           (tree-id ?tree-id) (node-id ?node-id)
           (execution-mode ?execution-mode)
           (execution-mode-result-state ?execution-mode-result-state))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (id ?node-id))
 =>
  (printout t "Updating selective-execution-settings for "
              ?tree-id ":" ?node-id crlf)
  (modify ?node (execution-mode ?execution-mode)
                (execution-mode-result-state ?execution-mode-result-state))
  (retract ?uf)
)
