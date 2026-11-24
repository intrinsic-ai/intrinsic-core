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

; Consistency checks for operation-envelope

; ----------------------------------- RULES -----------------------------------

(defrule operation-check-name-not-set
  "Check that the name is set."
  (operation-envelope (name ""))
 =>
  (assert (error (name OPERATION-CHECK-NAME-NOT-SET)
                 (type RECOVERABLE)
                 (message (str-cat "Operation without a name found. "
                   "An operation must have a unique name."))))
)

(defrule operation-check-process-tree-nil
  "Check that the operation-tree-id is set."
  (operation-envelope (name ?name) (operation-tree-id nil))
 =>
  (assert (error (name OPERATION-CHECK-PROCESS-TREE-NIL)
                 (type RECOVERABLE)
                 (message (str-cat "Operation " ?name
                   " has a process tree with ID nil"))))
)

(defrule operation-check-process-tree-exists
  "Check whether the process tree exists"
  (operation-envelope (name ?name) (operation-tree-id ?bt&~nil))
  (not (behavior-tree (id ?bt)))
 =>
  (assert (error (name OPERATION-CHECK-PROCESS-TREE-EXISTS)
                 (type RECOVERABLE) (behavior-tree-id ?bt)
                 (message (str-cat "Operation " ?name
                   " has a process tree with ID " ?bt " that does not exist"))))
)

(defrule operation-check-start-tree-nil
  "Check that the start-tree-id is set."
  (operation-envelope (name ?name) (start-tree-id nil))
 =>
  (assert (error (name OPERATION-CHECK-START-TREE-NIL)
                 (type RECOVERABLE)
                 (message (str-cat "Operation " ?name
                   " has a start tree with ID nil"))))
)

(defrule operation-check-start-tree-missing
  "Check whether the start tree is missing"
  (operation-envelope (name ?name) (start-tree-id ?bt&~nil))
  (not (behavior-tree (id ?bt)))
 =>
  (assert (error (name OPERATION-CHECK-START-TREE-MISSING)
                 (type RECOVERABLE) (behavior-tree-id ?bt)
                 (message (str-cat "Operation " ?name
                   " has a start tree with ID " ?bt " that does not exist"))))
)

(defrule operation-check-start-tree-not-in-operation
  "Check that the start tree is part of the operation tree"
  (operation-envelope (name ?name) (operation-tree-id ?op-tree-id&~nil)
                                   (start-tree-id ?start-tree-id&~nil))
  (behavior-tree (id ?op-tree-id))
  (behavior-tree (id ?start-tree-id&
                   :(neq ?op-tree-id
                     (behavior-tree-get-top-level-tree-id ?start-tree-id))))
 =>
  (assert (error (name OPERATION-CHECK-START-TREE-NOT-IN-OPERATION)
                 (type RECOVERABLE) (behavior-tree-id ?start-tree-id)
                 (message (str-cat "Operation " ?name
                   " has a start tree with ID " ?start-tree-id " that is not "
                   " in the operation tree with ID " ?op-tree-id))))
)

(defrule operation-check-name-not-unique
  "Check that there are no multiple operations with the same name"
  ?o1 <- (operation-envelope (name ?name))
  ?o2 <- (operation-envelope (name ?name))
  (test (neq ?o1 ?o2))
  (not (error (name OPERATION-CHECK-NAME-NOT-UNIQUE) (data ?name)))
 =>
  (assert (error (name OPERATION-CHECK-NAME-NOT-UNIQUE)
                 (type RECOVERABLE) (data ?name)
                 (message (str-cat "There exist at least two "
                   "operation-envelopes with name " ?name))))
)

(defrule operation-check-process-tree-in-multiple-operations
  "Check that there are no multiple operations with the same name"
  ?o1 <- (operation-envelope (operation-tree-id ?tree-id))
  ?o2 <- (operation-envelope (operation-tree-id ?tree-id))
  (test (neq ?o1 ?o2))
  (not (error (name OPERATION-CHECK-PROCESS-TREE-IN-MULTIPLE-OPERATIONS)
              (data ?tree-id)))
 =>
  (assert (error (name OPERATION-CHECK-PROCESS-TREE-IN-MULTIPLE-OPERATIONS)
                 (type RECOVERABLE) (data ?tree-id)
                 (message (str-cat "There exist at least two "
                   "operation-envelopes that have the same process tree, id: "
                   ?tree-id))))
)
