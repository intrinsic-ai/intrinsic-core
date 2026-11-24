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

; Consistency checks for behavior trees

; The behavior tree implementation assumes that the imported tree has a correct
; structure, e.g., all referenced nodes must exist.
; This file checks whether those assumptions are satisfied.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-check-root-does-not-exist
  "Check whether the root node exists"
  (behavior-tree (id ?tree-id) (root ?root-id))
  (not (behavior-tree-node (tree-id ?tree-id) (id ?root-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-ROOT-DOES-NOT-EXIST)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Root node with ID " ?root-id " of tree "
                                   ?tree-id " does not exist"))))
)

(defrule behavior-tree-check-start-node-does-not-exist
  "Check whether the start node exists"
  (behavior-tree (id ?tree-id) (start-node-id ?node-id))
  (not (behavior-tree-node (tree-id ?tree-id) (id ?node-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-START-NODE-DOES-NOT-EXIST)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Start node with ID " ?node-id " of tree "
                                   ?tree-id " does not exist"))))
)

(defrule behavior-tree-check-id-not-unique
  "Check that there are not multiple trees with the same id"
  ?b1 <- (behavior-tree (id ?tree-id))
  ?b2 <- (behavior-tree (id ?tree-id))
  (test (neq ?b1 ?b2))
  (not (error (name BEHAVIOR-TREE-CHECK-ID-NOT-UNIQUE) (data ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-ID-NOT-UNIQUE)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?tree-id)
                 (message (str-cat "There exist at least two trees with ID "
                                   ?tree-id))))
)

(defrule behavior-tree-check-parent-does-not-exist
  "A node has declared a parent that does not exist"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (parent-id ?parent-id&~0))
  (not (behavior-tree-node (id ?parent-id) (tree-id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-PARENT-DOES-NOT-EXIST)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Parent node " ?parent-id " of node " ?id
                                   " does not exist"))))
)

(defrule behavior-tree-check-root-must-have-no-parent
  "The root node of the tree must not have a parent"
  (behavior-tree (id ?tree-id) (root ?root-id))
  (behavior-tree-node (tree-id ?tree-id) (id ?root-id) (parent-id ?parent&~0))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-ROOT-MUST-HAVE-NO-PARENT)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Root node " ?root-id
                                   " must not have a parent, has parent "
                                   ?parent))))
)

(defrule behavior-tree-check-zero-is-invalid-node-id
  "Do not use 0 as node id"
  (behavior-tree-node (id 0) (tree-id ?tree-id))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-ZERO-IS-INVALID-NODE-ID)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message "A node with invalid ID 0 exists")))
)

(defrule behavior-tree-check-node-with-nonexistent-tree
  "A node refers to a nonexistent behavior-tree"
  (behavior-tree-node (id ?id) (tree-id ?tree-id))
  (not (behavior-tree (id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-NODE-WITH-NONEXISTENT-TREE)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Tree " ?tree-id " of node " ?id
                                   " does not exist"))))
)

(defrule behavior-tree-check-plan-does-not-exist
  "The plan referred to by a behavior tree does not exist"
  (behavior-tree (id ?id) (plan-id ?plan-id))
  (not (plan (id ?plan-id) (type BEHAVIOR-TREE)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-PLAN-DOES-NOT-EXIST)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?id)
                 (message (str-cat "Plan " ?plan-id " of behavior-tree " ?id
                                   " does not exist or has the wrong type"))))
)

(defrule behavior-tree-check-two-nodes-with-the-same-id
  "Node IDs must be unique"
  ?n1 <- (behavior-tree-node (tree-id ?tree-id) (id ?id))
  ?n2 <- (behavior-tree-node (tree-id ?tree-id) (id ?id))
  (test (neq ?n1 ?n2))
  (not (error (name BEHAVIOR-TREE-CHECK-TWO-NODES-WITH-THE-SAME-ID) (data ?id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-TWO-NODES-WITH-THE-SAME-ID)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?id)
                 (message (str-cat "Node IDs must be unique, but multiple nodes"
                                   " with id " ?id " exist"))))
)

(defrule behavior-tree-check-task-with-nonexistent-task
  "A TASK node must reference behavior-call, plan-action or code-execution"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type TASK)
                      (task-action-prototype-uid nil)
                      (behavior-call-instance-uid nil))
  (behavior-tree (id ?tree-id) (operation-name ?op-name))
  (not (code-execution-instance (operation-name ?op-name)
                                (tree-id ?tree-id) (node-id ?id)))
 =>
  (assert (error (name
            BEHAVIOR-TREE-CHECK-TASK-WITH-NONEXISTENT-TASK)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "TASK node " ?id " in " ?tree-id
                                   " refers neither to a prototype"
                                   " plan-action nor a behavior call"))))
)

(defrule behavior-tree-check-task-with-action-and-behavior-call
  "A TASK node references either a behavior-call or a plan-action"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type TASK)
                      (task-action-prototype-uid ?action-prototype-uid&~nil)
                      (behavior-call-instance-uid ?bc-uid&~nil))
 =>
  (assert (error (name
            BEHAVIOR-TREE-CHECK-TASK-WITH-ACTION-AND-BEHAVIOR-CALL)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "TASK node " ?id " in " ?tree-id
                                   " refers to a prototype plan-action "
                                   ?action-prototype-uid
                                   " and to behavior call " ?bc-uid
                                   ". Only one slot can be set."))))
)

(defrule behavior-tree-check-task-with-nonexistent-prototype-action
  "The prototype plan-action referenced by a TASK node does not exist"
  (behavior-tree-node (id ?id) (tree-id ?tree-id)
                      (type TASK) (task-action-prototype-uid ?action-uid&~nil))
  (not (plan-action (uid ?action-uid)))
 =>
  (assert (error (name
                   BEHAVIOR-TREE-CHECK-TASK-WITH-NONEXISTENT-PROTOTYPE-ACTION)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "TASK node " ?id " in " ?tree-id
                                   " refers to non-existent prototype"
                                   " plan-action " ?action-uid))))
)

(defrule behavior-tree-check-task-with-nonexistent-action
  "The plan-action referenced by a TASK node does not exist"
  (behavior-tree-node (id ?id) (tree-id ?tree-id)
                      (type TASK) (task-action-uid ?action-uid&~nil))
  (not (plan-action (uid ?action-uid)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-TASK-WITH-NONEXISTENT-ACTION)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "TASK node " ?id " in " ?tree-id
                                   " refers to non-existent plan-action "
                                   ?action-uid))))
)

(defrule behavior-tree-check-task-with-nonexistent-behavior-call
  "The behavior-call-instance referenced by a TASK node does not exist"
  (behavior-tree-node (id ?id) (tree-id ?tree-id)
                      (type TASK) (behavior-call-instance-uid ?bci-uid&~nil))
  (not (behavior-call-instance (uid ?bci-uid)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-TASK-WITH-NONEXISTENT-BEHAVIOR-CALL)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "TASK node " ?id " in " ?tree-id
                                   " refers to non-existent"
                                   " behavior-call-instance " ?bci-uid))))
)

(defrule behavior-tree-check-node-with-nonexistent-condition
  "A node refers to a non-existent condition"
  (behavior-tree-node (id ?id) (tree-id ?tree-id)
                      (condition-id ?condition-id&~nil))
  (not (behavior-tree-condition (id ?condition-id) (tree-id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-NODE-WITH-NONEXISTENT-CONDITION)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Node " ?id
                                   " refers to non-existent condition "
                                   ?condition-id))))
)

(defrule behavior-tree-check-max-tries-set-for-non-retry-node
  "The slot max-tries is reserved for RETRY nodes, value must be 0 otherwise"
  (behavior-tree-node (id ?id) (tree-id ?tree-id)
                      (type ?type&~RETRY) (retry-max-tries ?max-tries&~0))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-MAX-TRIES-SET-FOR-NON-RETRY-NODE)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "Node " ?id " with type " ?type
                                   " has max-tries set to " ?max-tries ","
                                   " which is only allowed for nodes of type"
                                   " RETRY"))))
)

(defrule behavior-tree-check-retry-node-without-child
  "A RETRY node must have one child"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type RETRY)
                               (retry-child-id ?retry-child-id))
  (not (behavior-tree-node (id ?retry-child-id) (tree-id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-RETRY-NODE-WITHOUT-CHILD)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "RETRY node " ?id " does not have child"))))
)

(defrule behavior-tree-check-task-node-with-child
  "A TASK node must not have any children"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type TASK))
  (behavior-tree-node (id ?child-id) (tree-id ?tree-id) (parent-id ?id))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-TASK-NODE-WITH-CHILD)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "TASK node " ?id " has child " ?child-id
                                   ", but a TASK node must have no children"))))
)


(defrule behavior-tree-check-branch-nil-if-condition
  "A BRANCH node requires an if condition."
  (behavior-tree-node (type BRANCH) (id ?id) (tree-id ?tree-id)
                      (branch-if-id nil))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-BRANCH-NIL-IF-CONDITION)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "BRANCH node with id " ?id " (tree " ?tree-id
                                   ") has no if condition."))))
)

(defrule behavior-tree-check-branch-missing-if-condition
  "A BRANCH node has condition set but it's missing."
  (behavior-tree-node (type BRANCH) (id ?id) (tree-id ?tree-id)
                      (branch-if-id ?branch-if-id&~nil))
   (not (behavior-tree-condition (id ?branch-if-id) (tree-id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-BRANCH-MISSING-IF-CONDITION)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "BRANCH node with id " ?id " (tree " ?tree-id
                                   ") has set condition " ?branch-if-id
                                   " but the condition does not exist"))))
)

(defrule behavior-tree-check-branch-neither-then-nor-else-child
  "A BRANCH requires at least one of THEN or ELSE children"
  (behavior-tree-node (type BRANCH)
                      (id ?branch-node-id) (tree-id ?tree-id)
                      (branch-then-id 0) (branch-else-id 0))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-BRANCH-NEITHER-THEN-NOR-ELSE-CHILD)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "BRANCH node with id " ?branch-node-id
                                   " (tree " ?tree-id ") has neither a then nor"
                                   " an else child"))))
)

(defrule behavior-tree-check-branch-missing-then-child
  "A BRANCH has a 'then' child set but that is not to be found"
  (behavior-tree-node (type BRANCH)
                      (id ?branch-node-id) (tree-id ?tree-id)
                      (branch-then-id ?branch-then-id&~0))
  (not (behavior-tree-node (id ?branch-then-id) (tree-id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-BRANCH-MISSING-THEN-CHILD)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "BRANCH node with id " ?branch-node-id
                                   " (tree " ?tree-id ") has an then child set"
                                   " that cannot be found"))))
)

(defrule behavior-tree-check-branch-missing-else-child
  "A BRANCH has a 'else' child set but that is not to be found"
  (behavior-tree-node (type BRANCH)
                      (id ?branch-node-id) (tree-id ?tree-id)
                      (branch-else-id ?branch-else-id&~0))
  (not (behavior-tree-node (id ?branch-else-id) (tree-id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-BRANCH-MISSING-ELSE-CHILD)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "BRANCH node with id " ?branch-node-id
                                   " (tree " ?tree-id ") has an else child set"
                                   " that cannot be found"))))
)

(defrule behavior-tree-check-loop-no-do-child
  "A LOOP requires a 'do' child"
  (behavior-tree-node (type LOOP) (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-do-id 0))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-LOOP-NO-DO-CHILD)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") has no 'do' child"))))
)

(defrule behavior-tree-check-loop-missing-do-child
  "A LOOP node has a 'do' child set, but the node is missing"
  (behavior-tree-node (type LOOP)
                      (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-do-id ?loop-do-id&~0))
  (not (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-LOOP-MISSING-DO-CHILD)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") has node " ?loop-do-id
                                   " set as 'do' child,"
                                   " but node cannot be found"))))
)

(defrule behavior-tree-check-loop-while-with-for-each-input-protos
  "A LOOP node in WHILE mode must not specify protos to for-each over"
  (behavior-tree-node (type LOOP) (loop-mode WHILE)
                      (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-for-each-input-protos
                        $?protos&:(non-empty$ ?protos)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-LOOP-WHILE-WITH-FOR-EACH-INPUT-PROTOS)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") is a WHILE node,"
                                   " but defines for-each input protos"))))
)

(defrule behavior-tree-check-loop-while-with-for-each-generator
  "A LOOP node in WHILE mode must not specify a generator expression to loop"
  (behavior-tree-node (type LOOP) (loop-mode WHILE)
                      (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-for-each-generator-expression ?for-each-gen&~""))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-LOOP-WHILE-WITH-FOR-EACH-GENERATOR)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") is a WHILE node,"
                                   " but defines a for-each generator"
                                   " expression"))))
)

(defrule behavior-tree-check-loop-while-with-for-each-blackboard-key
  "A LOOP node in WHILE mode must not specify a blackboard key for for-each"
  (behavior-tree-node (type LOOP) (loop-mode WHILE)
                      (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-for-each-value-blackboard-key ?for-each-key&~""))
 =>
  (assert (error (name
                   BEHAVIOR-TREE-CHECK-LOOP-WHILE-WITH-FOR-EACH-BLACKBOARD-KEY)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") is a WHILE node,"
                                   " but defines a for-each blackboard key"))))
)

(defrule behavior-tree-check-loop-for-each-with-while-condition
  "A LOOP node in FOR-EACH mode must not specify a while condition"
  (behavior-tree-node (type LOOP) (loop-mode FOR-EACH)
                      (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-while-id ?while-id&~nil))
 =>
  (assert (error (name
                   BEHAVIOR-TREE-CHECK-LOOP-FOR-EACH-WITH-WHILE-CONDITION)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") is a FOR-EACH node,"
                                   " but defines a loop while condition"))))
)

(defrule behavior-tree-check-loop-for-each-input-protos-and-generator
  "A LOOP node in FOR-EACH mode cannot have input protos and a generator"
  (behavior-tree-node (type LOOP) (loop-mode FOR-EACH)
                      (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-for-each-input-protos
                        $?protos&:(non-empty$ ?protos))
                      (loop-for-each-generator-expression ?gen-expr&~""))
 =>
  (assert (error (name
                   BEHAVIOR-TREE-CHECK-LOOP-FOR-EACH-INPUT-PROTOS-AND-GENERATOR)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") is a FOR-EACH node,"
                                   " but defines input protos and a generator"
                                   " expression. Exactly one of these needs to"
                                   " be defined."))))
)

(defrule behavior-tree-check-loop-for-each-no-input-nor-generator
  "A LOOP node in FOR-EACH mode must specify input protos or a generator"
  (behavior-tree-node (type LOOP) (loop-mode FOR-EACH)
                      (id ?loop-node-id) (tree-id ?tree-id)
                      (loop-for-each-input-protos $?protos&:(empty$ ?protos))
                      (loop-for-each-generator-expression ?gen-expr&""))
 =>
  (assert (error (name
                   BEHAVIOR-TREE-CHECK-LOOP-FOR-EACH-NO-INPUT-NOR-GENERATOR)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "LOOP node with id " ?loop-node-id
                                   " (tree " ?tree-id ") is a FOR-EACH node,"
                                   " but defines neither input protos nor a"
                                   " generator expression. Exactly one of these"
                                   " needs to be defined."))))
)

(defrule behavior-tree-check-sub-tree-node-without-tree
  "For a SUB-TREE node a behavior-tree must exist with the correct id."
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type SUB-TREE)
                      (sub-tree-id ?sub-tree-id))
  (not (behavior-tree (id ?sub-tree-id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-SUB-TREE-NODE-WITHOUT-TREE)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "SUB-TREE node " ?id " does not have a"
                                   " corresponding tree, but must have one"))))
)

(defrule behavior-tree-check-sub-tree-node-with-children
  "A SUB-TREE node may not have any children"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type SUB-TREE))
  ?child <- (behavior-tree-node (id ?child-id) (tree-id ?tree-id)
                                (parent-id ?id))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-SUB-TREE-NODE-WITH-CHILDREN)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "SUB-TREE node " ?id " has child" ?child-id
                                   ", but may not have any children"))))
)

(defrule behavior-tree-check-log-id-zero
  "The top-level tree has to have a log-id set when being executed."
  (declare (salience ?*SALIENCE-HIGH*))
  (operation-envelope (operation-tree-id ?bt-id))
  (behavior-tree (id ?bt-id) (state RUNNING) (log-id 0))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-LOG-ID-ZERO)
               (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?bt-id)
               (message (str-cat "Tree node " ?bt-id " has no log id set while"
               " being executed."))))
)

(defrule behavior-tree-check-condition-without-parent
  "A parent condition must exist."
  (behavior-tree-condition (id ?id) (parent-id ?pid&~nil)
                                 (tree-id ?tree-id))
  (not (behavior-tree-condition (id ?pid)))

  (not (error (name BEHAVIOR-TREE-CHECK-CONDITION-WITHOUT-PARENT)
              (data ?pid)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-CONDITION-WITHOUT-PARENT)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?pid)
                 (message (str-cat "For condition " ?id ": The parent condition"
                                   ?pid " does not exist."))))
)

(defrule behavior-tree-check-condition-without-node
  "A condition must be associated to a node."
  (behavior-tree-condition (id ?id) (node-id ?node-id) (tree-id ?tree-id))
  (not (behavior-tree-node (id ?node-id) (tree-id ?tree-id)))

  (not (error (name BEHAVIOR-TREE-CHECK-CONDITION-WITHOUT-NODE)
              (data ?id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-CONDITION-WITHOUT-NODE)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?id)
                 (message (str-cat "For condition " ?id ": The node " ?node-id
                                   " in tree " ?tree-id " does not exist."))))
)

(defrule behavior-tree-check-two-conditions-with-same-index
  "Condition index must be unique"
  ?c1 <- (behavior-tree-condition (parent-id ?pid&~nil) (tree-id ?tree-id)
                                  (compound-condition-index ?index) (id ?id1))
  ?c2 <- (behavior-tree-condition (parent-id ?pid&~nil) (tree-id ?tree-id)
                                  (compound-condition-index ?index) (id ?id2))
  (test (neq ?c1 ?c2))
  (not (error (name BEHAVIOR-TREE-CHECK-TWO-CONDITIONS-WITH-SAME-INDEX)
              (data ?index)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-TWO-CONDITIONS-WITH-SAME-INDEX)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?index)
                 (message (str-cat "Condition index must be unique within "
                                   "parent, but multiple conditions with index "
                                   ?index " exist. Ids: " ?id1 " " ?id2))))
)

(defrule behavior-tree-check-two-nodes-with-same-index
  "Node index must be unique within parent"
  ?n1 <- (behavior-tree-node (parent-id ?pid&~0) (tree-id ?tree-id)
                             (index ?index&~0) (id ?id1))
  ?n2 <- (behavior-tree-node (parent-id ?pid&~0) (tree-id ?tree-id)
                             (index ?index&~0) (id ?id2))
  (test (neq ?n1 ?n2))
  (not (error (name BEHAVIOR-TREE-CHECK-TWO-NODES-WITH-SAME-INDEX)
              (data ?index)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-TWO-NODES-WITH-SAME-INDEX)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?index)
                 (message (str-cat "Node index must be unique within "
                                   "parent, but multiple nodes with index "
                                   ?index " exist. Ids: " ?id1 " " ?id2))))
)

(defrule behavior-tree-check-node-with-children-zero-index
  "If a node type may have multiple children their index needs to be set"
  (behavior-tree-node (parent-id ?pid&~0) (tree-id ?tree-id) (index 0) (id ?id))
  (behavior-tree-node (tree-id ?tree-id) (id ?pid)
                      (type SEQUENCE|PARALLEL|SELECTOR|FALLBACK))
  (not (error (name BEHAVIOR-TREE-CHECK-NODE-WITH-CHILDREN-ZERO-INDEX)
              (data ?id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-NODE-WITH-CHILDREN-ZERO-INDEX)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?id)
                 (message (str-cat "Node index cannot be zero if parent is a "
                                   "sequence, parallel, selector or fallback "
                                   "node. Id: " ?id))))
)

(defrule behavior-tree-check-data-node-missing-blackboard-key
  "If a data node is missing the blackboard key"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type DATA)
                      (data-blackboard-key ""))
  (not (error (name BEHAVIOR-TREE-CHECK-DATA-NODE-MISSING-BLACKBOARD-KEY)
              (data ?id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-DATA-NODE-MISSING-BLACKBOARD-KEY)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?id)
                 (message (str-cat "Data node is missing blackboard key"))))
)

(defrule behavior-tree-check-data-node-create-or-update-without-proto
  "If a CREATE-OR-UPDATE data node is missing the proto"
  (behavior-tree-node (id ?id) (tree-id ?tree-id) (type DATA)
                      (data-operation CREATE-OR-UPDATE)
                      (data-create-update-proto 0))
  (not (error (name BEHAVIOR-TREE-CHECK-DATA-NODE-CREATE-OR-UPDATE-WITHOUT-PROTO)
              (data ?id)))
 =>
  (assert (error (name BEHAVIOR-TREE-CHECK-DATA-NODE-CREATE-OR-UPDATE-WITHOUT-PROTO)
                 (trigger-full-report TRUE)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (data ?id)
                 (message (str-cat "Data node to create or update data is "
                                   "missing the create or update proto"))))
)
