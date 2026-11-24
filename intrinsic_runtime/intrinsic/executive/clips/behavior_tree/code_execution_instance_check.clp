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

; Code execution instance conformance checks.
;
; Requires:
; - code_execution_instance.clp

; ----------------------------------- RULES -----------------------------------

(defrule code-execution-instance-check-ambiguous-ids
  "Check if there is more than 1 code-execution-instance associated with a node."
  ?cei1 <- (code-execution-instance (operation-name ?op-name)
                                    (tree-id ?tree-id) (node-id ?node-id))
  ?cei2 <- (code-execution-instance (operation-name ?op-name)
                                    (tree-id ?tree-id) (node-id ?node-id))
  (test (< (fact-index ?cei1) (fact-index ?cei2)))  ; fire once for 2 facts
  (test (neq ?cei1 ?cei2))
 =>
  (assert
    (error (name CODE-EXECUTION-INSTANCE-CHECK-AMBIGUOUS-IDS) (type RECOVERABLE)
           (message (str-cat "code-execution-instance facts " (fact-index ?cei1)
                             " and " (fact-index ?cei2)
                             " pertain to the same node "
                             (code-execution-instance-to-string ?cei1)))))
)

(defrule code-execution-instance-not-associated-to-node
  "Check that a code-execution-instance is associated to a node"
  ?cei <- (code-execution-instance (tree-id ?tree-id) (node-id ?node-id))
  (not (behavior-tree-node (tree-id ?tree-id) (id ?node-id)))
 =>
  (assert
    (error (name CODE-EXECUTION-INSTANCE-NOT-ASSOCIATED-TO-NODE)
           (type RECOVERABLE)
           (message (str-cat "code-execution-instance "
                             (code-execution-instance-to-string ?cei)
                             " is not associated with any node."))))
)

(defrule code-execution-instance-not-associated-to-tree
  "Check that a code-execution-instance is associated to a tree"
  ?cei <- (code-execution-instance (operation-name ?op-name)
                                   (tree-id ?tree-id))
  (not (behavior-tree (id ?tree-id) (operation-name ?op-name)))
 =>
  (assert
    (error (name CODE-EXECUTION-INSTANCE-NOT-ASSOCIATED-TO-TREE)
           (type RECOVERABLE)
           (message (str-cat "code-execution-instance "
                             (code-execution-instance-to-string ?cei)
                             " is not associated with any tree."))))
)

(defrule code-execution-instance-not-associated-to-operation
  "Check that a code-execution-instance is associated to an operation"
  ?cei <- (code-execution-instance (operation-name ?op-name))
  (not (operation-envelope (name ?op-name)))
 =>
  (assert
    (error (name CODE-EXECUTION-INSTANCE-NOT-ASSOCIATED-TO-OPERATION)
           (type RECOVERABLE)
           (message (str-cat "code-execution-instance "
                             (code-execution-instance-to-string ?cei)
                             " is not associated with any operation."))))
)

(defrule code-execution-instance-requires-code
  "Check that a code-execution-instance has code"
  ?cei <- (code-execution-instance (code-proto 0))
 =>
  (assert
    (error (name CODE-EXECUTION-INSTANCE-REQUIRES-CODE)
           (type RECOVERABLE)
           (message (str-cat "code-execution-instance "
                             (code-execution-instance-to-string ?cei)
                             " doesn't have a code proto."))))
)

(defrule code-execution-instance-requires-parameters
  "Check that a code-execution-instance has parameters"
  ?cei <- (code-execution-instance (parameters-prototype-proto 0)
                                   (parameter-message-full-name ?parameter-message-full-name))
  (test (neq ?parameter-message-full-name ""))
 =>
  (assert
    (error (name CODE-EXECUTION-INSTANCE-REQUIRES-PARAMETERS)
           (type RECOVERABLE)
           (message (str-cat "code-execution-instance "
                             (code-execution-instance-to-string ?cei)
                             " has a parameter message but no parameters proto."))))
)

(defrule code-execution-instance-requires-file-descriptor-set
  "Check that a code-execution-instance has file descriptor set"
  ?cei <- (code-execution-instance (file-descriptor-set-proto 0)
                                   (parameter-message-full-name ?parameter-message-full-name)
                                   (return-value-message-full-name ?return-value-message-full-name))
  (test (or (neq ?parameter-message-full-name "")
            (neq ?return-value-message-full-name "")))
 =>
  (assert
    (error (name CODE-EXECUTION-INSTANCE-REQUIRES-FILE-DESCRIPTOR-SET)
           (type RECOVERABLE)
           (message (str-cat "code-execution-instance "
                             (code-execution-instance-to-string ?cei)
                             " has a parameter and/or return value message"
                             " but no file descriptor set proto."))))
)
