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

; Behavior Tree import from BehaviorTree proto


; ---------------------------- FORWARD DECLARATIONS ----------------------------

(deffunction behavior-tree-import-node-proto-result-value-get-asserted-fact
  (?result-value))
(deffunction behavior-tree-import-node-proto (?tree-id ?parent-id
                                              ?blackboard-scope ?operation-name
                                              ?run-metadata-proto ?run-metadata-proto-path
                                              ?child-index
                                              ?node-proto))

(deffunction behavior-tree-import-node-protos (?tree-id ?parent-id
                                               ?blackboard-scope ?operation-name
                                               ?run-metadata-proto ?run-metadata-proto-path-prefix
                                               ?node-protos))

(deffunction behavior-tree-import-proto-result-value-get-tree-id
  (?result-value))
(deffunction behavior-tree-import-proto (?bt-proto ?parent-tree-id
                                         ?blackboard-scope ?operation-name
                                         ?run-metadata-proto ?run-metadata-proto-path))

; --------------------------------- FUNCTIONS ---------------------------------

; Generates a state proto path (if state proto is given).
;
; Args:
;   ?run-metadata-proto: proto ID of associated state proto, may be 0
;   ?prefix: state proto path prefix
;   ?suffix: state proto path suffix
;
; Returns:
;   If ?run-metadata-proto is 0 returns empty string, otherwise returns concatenation
;   of ?prefix and ?suffix.
(deffunction gen-run-metadata-proto-path (?run-metadata-proto ?prefix ?suffix)
  (if (= ?run-metadata-proto 0) then (return ""))
  (return (proto-path-join ?prefix ?suffix))
)

; Imports a condition (recursively).
; Imports the given ?condition-proto and creates a fact for ?tree-id with a
; condition ID ?condition-id and ?parent-id (?parent-id may be nil).
; This works recursively for compound conditions.
;
; Args:
;   ?condition-proto: input BehaviorTree.Condition proto to import
;   ?tree-id: Behavior Tree ID condition belongs to
;   ?node-id: Behavior Tree Node ID condition belongs to
;   ?run-metadata-proto: RunMetadata state proto to pre-fill
;   ?run-metadata-proto-path: proto path to store with condition for state updates
;   ?condition-id: ID to use for new condition
;   ?parent-id: parent condition ID
;   ?blackboard-scope: The blackboard-scope to use for this condition.
;   ?operation-name: Name of the operation to associate this tree to.
;   ?condition-index: index in context of parent condition, e.g., in conjunction
;
; Returns:
;   A multifield with the first entry TRUE/FALSE and
;   on success: ""
;   on failure: a message describing why the import failed
(deffunction behavior-tree-import-condition (?condition-proto ?tree-id ?node-id
                                             ?run-metadata-proto ?run-metadata-proto-path
                                             ?condition-id ?parent-id
                                             ?blackboard-scope ?operation-name
                                             ?condition-index)
  (if (<> ?run-metadata-proto 0) then
    (pb-set-field ?run-metadata-proto (proto-path-join ?run-metadata-proto-path "state") ACCEPTED)
  )
  (switch (pb-which-oneof ?condition-proto "condition_type")

    ; Subtree condition
    (case behavior_tree then
      (bind ?sub-tree-condition-proto
        (pb-get-field ?condition-proto "behavior_tree"))
      (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "behavior_tree"))

      (bind ?sub-tree-import-result
        (behavior-tree-import-proto ?sub-tree-condition-proto
          ?tree-id ?blackboard-scope ?operation-name
          ?run-metadata-proto ?s-path))
      (pb-remove ?sub-tree-condition-proto)

      (if (not (result-ok ?sub-tree-import-result)) then
        (return ?sub-tree-import-result)
      )

      (bind ?sub-tree-id
        (behavior-tree-import-proto-result-value-get-tree-id
          ?sub-tree-import-result))

      (assert (behavior-tree-condition
               (type SUB-TREE) (id ?condition-id) (parent-id ?parent-id)
               (tree-id ?tree-id) (node-id ?node-id)
               (sub-tree-id ?sub-tree-id) (run-metadata-proto-path ?run-metadata-proto-path)
               (compound-condition-index ?condition-index)))
    )

    ; Blackboard CEL expression
    (case blackboard then
      (bind ?bb-proto (pb-get-field ?condition-proto "blackboard"))

      (switch (pb-which-oneof ?bb-proto "expression_type")
        (case cel_expression_proto then
          (bind ?cel-expr-proto (pb-get-field ?bb-proto "cel_expression_proto"))
          (bind ?cel-expr-id (cel-create-from-proto ?cel-expr-proto
                               ?*PROTO-DESCRIPTOR-POOL-STANDARD-MESSAGES*))
          (if (= ?cel-expr-id 0) then
            (bind ?result (result-create FALSE
                           (str-cat "Cannot import cel_expression_proto: "
                                    (pb-tostring ?cel-expr-proto))))
            (pb-remove ?cel-expr-proto)
            (return ?result)
          )
          (pb-remove ?cel-expr-proto)
          (bind ?expression-or-error (cel-get-expression ?cel-expr-id))
          (if (neq (nth$ 1 ?expression-or-error) ERROR)
            then
              (bind ?expression (nth$ 1 ?expression-or-error))
            else
              (return (result-create FALSE
                        (str-cat "Cannot import cel_expression_proto. Error: "
                                 (nth$ 2 ?expression-or-error))))
          )
        )
        (case cel_expression then
          (bind ?expression (pb-get-field ?bb-proto "cel_expression"))
          (bind ?cel-expr-id (cel-create ?expression
                               ?*PROTO-DESCRIPTOR-POOL-STANDARD-MESSAGES*))
          (if (= ?cel-expr-id 0) then
            (return (result-create FALSE
                           (str-cat "Cannot import cel_expression: "
                                    ?expression)))
          )
        )
        (default
          (return (result-create FALSE
                    (str-cat "Cannot import expression of type: "
                             (pb-which-oneof ?bb-proto "expression_type"))))
        )
      )

      (assert
        (behavior-tree-condition (type BLACKBOARD-CEL-EXPRESSION)
                                 (id ?condition-id) (parent-id ?parent-id)
                                 (tree-id ?tree-id) (node-id ?node-id)
                                 (run-metadata-proto-path ?run-metadata-proto-path)
                                 (blackboard-cel-expression ?expression)
                                 (blackboard-cel-expression-id ?cel-expr-id)
                                 (compound-condition-index ?condition-index)))
      (pb-remove ?bb-proto)
    )

    ; ExtendedStatusMatch
    (case status_match then
      (bind ?status-match-proto (pb-get-field ?condition-proto "status_match"))

      (if (pb-has-field ?status-match-proto "status_code")
       then
         (bind ?status-code-proto
           (pb-get-field ?status-match-proto "status_code"))
         (bind ?es-bb-key (pb-get-field ?status-match-proto "blackboard_key"))
         (bind ?es-component (pb-get-field ?status-code-proto "component"))
         (bind ?es-code (pb-get-field ?status-code-proto "code"))

         (assert
           (behavior-tree-condition
            (type EXTENDED-STATUS-MATCH)
            (id ?condition-id) (parent-id ?parent-id)
            (tree-id ?tree-id) (node-id ?node-id)
            (run-metadata-proto-path ?run-metadata-proto-path)
            (compound-condition-index ?condition-index)
            (extended-status-match-blackboard-key ?es-bb-key)
            (extended-status-match-component ?es-component)
            (extended-status-match-code ?es-code)
           )
         )
         (pb-remove ?status-code-proto)
       else
         (pb-remove ?status-match-proto)
         (return
           (result-create FALSE
                          (str-cat "ExtendedStatusMatch condition "
                                   "is missing status_code field.")))
      )

      (pb-remove ?status-match-proto)
    )

    ; AllOf, aka AND
    (case all_of then
      (bind ?compound-proto (pb-get-field ?condition-proto "all_of"))
      (bind ?sub-condition-protos
        (pb-get-repeated-values ?compound-proto "conditions"))
      (bind ?sub-condition-index 1)
      (foreach ?sub-condition-proto ?sub-condition-protos
        (bind ?sub-condition-id
          (sym-cat ?condition-id "-" ?sub-condition-proto-index))
        (bind ?s-path (if (<> ?run-metadata-proto 0)
                       then (format nil "%s.all_of.conditions[%d]"
                                    ?run-metadata-proto-path (- ?sub-condition-index 1))
                       else ""))

        (bind ?import-cond-result
          (behavior-tree-import-condition ?sub-condition-proto ?tree-id ?node-id
                                          ?run-metadata-proto ?s-path
                                          ?sub-condition-id ?condition-id
                                          ?blackboard-scope ?operation-name
                                          ?sub-condition-index))
        (pb-remove ?sub-condition-proto)
        (if (not (result-ok ?import-cond-result))
         then
           (pb-remove ?compound-proto)
           (return ?import-cond-result)
        )
        (bind ?sub-condition-index (+ ?sub-condition-index 1))
      )
      (pb-remove ?compound-proto)
      (assert (behavior-tree-condition
               (type AND) (id ?condition-id) (parent-id ?parent-id)
               (tree-id ?tree-id) (node-id ?node-id)
               (run-metadata-proto-path ?run-metadata-proto-path)
               (compound-condition-index ?condition-index)))
    )

    ; AnyOf, aka OR
    (case any_of then
      (bind ?compound-proto (pb-get-field ?condition-proto "any_of"))
      (bind ?sub-condition-protos
        (pb-get-repeated-values ?compound-proto "conditions"))
      (bind ?sub-condition-index 1)
      (foreach ?sub-condition-proto ?sub-condition-protos
        (bind ?sub-condition-id
          (sym-cat ?condition-id "-" ?sub-condition-proto-index))
        (bind ?s-path (if (<> ?run-metadata-proto 0)
                       then (format nil "%s.any_of.conditions[%d]"
                                    ?run-metadata-proto-path (- ?sub-condition-index 1))
                       else ""))
        (bind ?import-cond-result
          (behavior-tree-import-condition ?sub-condition-proto ?tree-id ?node-id
                                          ?run-metadata-proto ?s-path
                                          ?sub-condition-id ?condition-id
                                          ?blackboard-scope ?operation-name
                                          ?sub-condition-index))
        (pb-remove ?sub-condition-proto)
        (if (not (result-ok ?import-cond-result))
         then
           (pb-remove ?compound-proto)
           (return ?import-cond-result)
        )
        (bind ?sub-condition-index (+ ?sub-condition-index 1))
      )
      (pb-remove ?compound-proto)
      (assert (behavior-tree-condition
               (type OR) (id ?condition-id) (parent-id ?parent-id)
               (tree-id ?tree-id) (node-id ?node-id)
               (run-metadata-proto-path ?run-metadata-proto-path)
               (compound-condition-index ?condition-index)))
    )

    ; Not/negation
    (case not then
      (bind ?sub-condition-proto (pb-get-field ?condition-proto "not"))
      (bind ?sub-condition-id (sym-cat ?condition-id "-not"))
      (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "not"))
      (bind ?import-cond-result
        (behavior-tree-import-condition ?sub-condition-proto ?tree-id ?node-id
                                        ?run-metadata-proto ?s-path
                                        ?sub-condition-id ?condition-id
                                        ?blackboard-scope ?operation-name
                                        1))
      (pb-remove ?sub-condition-proto)
      (if (not (result-ok ?import-cond-result))
       then (return ?import-cond-result))
      (assert
        (behavior-tree-condition (type NOT)
                                 (id ?condition-id) (parent-id ?parent-id)
                                 (tree-id ?tree-id) (node-id ?node-id)
                                 (run-metadata-proto-path ?run-metadata-proto-path)
                                 (compound-condition-index ?condition-index)))
    )

    (default
      (return (result-create FALSE (str-cat "Cannot import condition with "
        "unknown type: " (pb-which-oneof ?condition-proto "condition_type"))))
    )
  )
  (return (create$ TRUE ""))
)

; Recursively imports a single branch (Selector.Branch) of a selector.
;
; This will create a child node of the selector node based on the 'node' field.
; The 'condition' field will be a decorator condition of the created node.
; The specified 'node' must not have a decorator condition.
;
; Args:
;   ?branch-proto: the proto to import (recursively). This must be a
;     SelectorNode.Branch.
;   ?tree-id: the behavior tree ID the node will belong to
;   ?parent-id: parent node ID for the new node
;   ?blackboard-scope: The blackboard scope to use for the nodes, if relevant.
;   ?operation-name: Name of the operation to associate this tree to.
;   ?run-metadata-proto: proto ID for a RunMetadata proto to which state information
;                 will be written during execution.
;   ?run-metadata-proto-path: proto path for the node proto within enclosing behavior
;                tree state proto
;   ?child-index: index of a node in the context of its parent, e.g., if the
;                 parent node is a sequence determines order of execution.
;
; Returns:
;   A multifield result with the first entry TRUE/FALSE and:
;   on success: ""
;   on failure: a message describing why the import failed
(deffunction behavior-tree-import-selector-branch (?branch-proto
   ?tree-id ?parent-id ?blackboard-scope ?operation-name
   ?run-metadata-proto ?run-metadata-proto-path ?child-index)
  ; Extract and verify the Node proto from the Branch
  (bind ?branch-node-proto (pb-get-field ?branch-proto "node"))
  (if (eq ?branch-node-proto MISSING-FIELD) then
    (return
      (result-create FALSE "Selector branches must have the 'node' field set"))
  )
  (if (pb-has-field ?branch-node-proto "decorators.condition") then
    (pb-remove ?branch-node-proto)
    (return (result-create FALSE
      "Nodes in selector branches must not have decorator conditions"))
  )

  ; Import the Node proto as a child of the selector, but keep the path of the
  ; imported node pointing to the Branch
  (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "node"))
  (bind ?import-node-result
    (behavior-tree-import-node-proto ?tree-id ?parent-id
                                     ?blackboard-scope ?operation-name
                                     ?run-metadata-proto ?s-path ?child-index
                                     ?branch-node-proto))
  (pb-remove ?branch-node-proto)
  (if (not (result-ok ?import-node-result))
    then
      (return ?import-node-result)
  )

  ; Import the condition (if set) as a decorator condition of the imported node
  (if (pb-has-field ?branch-proto "condition") then
    (bind ?branch-condition-proto (pb-get-field ?branch-proto "condition"))
    (bind ?branch-node-fact
      (behavior-tree-import-node-proto-result-value-get-asserted-fact
        ?import-node-result))
    (bind ?branch-node-id (fact-slot-value ?branch-node-fact id))
    (bind ?node-condition-id
      (sym-cat ?tree-id "-" ?branch-node-id "-condition"))
    (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "condition"))
    (bind ?import-cond-result
      (behavior-tree-import-condition ?branch-condition-proto ?tree-id
                                      ?branch-node-id
                                      ?run-metadata-proto ?s-path
                                      ?node-condition-id nil
                                      ?blackboard-scope ?operation-name 1))
    (pb-remove ?branch-condition-proto)
    (if (not (result-ok ?import-cond-result))
     then
       (return ?import-cond-result)
    )
    ; Register this condition as the decorator condition of the imported node
    (modify ?branch-node-fact (condition-id ?node-condition-id))
  )

  (return (result-create TRUE ""))
)

; Recursively imports a single try (Fallback.Try) of a fallback.
;
; This will create a child node of the fallback node based on the 'node' field.
; The 'condition' field will be a decorator condition of the created node if set.
; The specified 'node' must not have a decorator condition.
;
; Args:
;   ?try-proto: the proto to import (recursively). This must be a
;     FallbackNode.Try.
;   ?tree-id: the behavior tree ID the node will belong to
;   ?parent-id: parent node ID for the new node
;   ?blackboard-scope: The blackboard scope to use for the nodes, if relevant.
;   ?operation-name: Name of the operation to associate this tree to.
;   ?run-metadata-proto: proto ID for a RunMetadata proto to which state information
;                 will be written during execution.
;   ?run-metadata-proto-path: proto path for the node proto within enclosing behavior
;                tree state proto
;   ?child-index: index of a node in the context of its parent, e.g., if the
;                 parent node is a sequence determines order of execution.
;
; Returns:
;   A multifield result with the first entry TRUE/FALSE and:
;   on success: ""
;   on failure: a message describing why the import failed
(deffunction behavior-tree-import-fallback-try (?try-proto
   ?tree-id ?parent-id ?blackboard-scope ?operation-name
   ?run-metadata-proto ?run-metadata-proto-path ?child-index)
  ; Extract and verify the Node proto from the Try
  (bind ?try-node-proto (pb-get-field ?try-proto "node"))
  (if (eq ?try-node-proto MISSING-FIELD) then
    (return
      (result-create FALSE "Fallback tries must have the 'node' field set"))
  )
  (if (pb-has-field ?try-node-proto "decorators.condition") then
    (pb-remove ?try-node-proto)
    (return (result-create FALSE
      "Nodes in fallback tries must not have decorator conditions"))
  )

  ; Import the Node proto as a child of the fallback, but keep the path of the
  ; imported node pointing to the Try
  (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "node"))
  (bind ?import-node-result
    (behavior-tree-import-node-proto ?tree-id ?parent-id
                                     ?blackboard-scope ?operation-name
                                     ?run-metadata-proto ?s-path ?child-index
                                     ?try-node-proto))
  (pb-remove ?try-node-proto)
  (if (not (result-ok ?import-node-result))
    then
      (return ?import-node-result)
  )

  ; Import the condition (if set) as a decorator condition of the imported node
  (if (pb-has-field ?try-proto "condition") then
    (bind ?try-condition-proto (pb-get-field ?try-proto "condition"))
    (bind ?try-node-fact
      (behavior-tree-import-node-proto-result-value-get-asserted-fact
        ?import-node-result))
    (bind ?try-node-id (fact-slot-value ?try-node-fact id))
    (bind ?node-condition-id
      (sym-cat ?tree-id "-" ?try-node-id "-condition"))
    (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "condition"))
    (bind ?import-cond-result
      (behavior-tree-import-condition ?try-condition-proto ?tree-id
                                      ?try-node-id
                                      ?run-metadata-proto ?s-path
                                      ?node-condition-id nil
                                      ?blackboard-scope ?operation-name 1))
    (pb-remove ?try-condition-proto)
    (if (not (result-ok ?import-cond-result))
     then
       (return ?import-cond-result)
    )
    ; Register this condition as the decorator condition of the imported node
    (modify ?try-node-fact (condition-id ?node-condition-id))
  )

  (return (result-create TRUE ""))
)

; Extract node fact from behavior-tree-import-node-proto result.
;
; Args:
;   ?result-value: A result value from a successful call to
;                  behavior-tree-import-node-proto.
;
; Returns:
;   The asserted node fact.
(deffunction behavior-tree-import-node-proto-result-value-get-asserted-fact
  (?result-value)
    (return (nth$ 2 ?result-value))
)

; Recursively imports a BehaviorTree.Node proto.
; Retrieves the BehaviorTree.Node proto using the ProtobufManager
; and asserts the corresponding behavior-tree-node fact.
; Recurses over the inner nodes.
;
; Call examples:
; (behavior-tree-import-node-proto Tree1 0 PROCESS "custom_scope"
;                                  ?s-proto ?s-path 0
;                                  ?proto)
;
; Args:
;   ?tree-id: the behavior tree ID the nodes will belong to
;   ?parent-id: parent node ID for new nodes
;   ?blackboard-scope: The blackboard scope to use for the nodes, if relevant.
;   ?operation-name: Name of the operation to associate this tree to.
;   ?run-metadata-proto: proto ID for a RunMetadata proto to which state information
;                 will be written during execution.
;   ?run-metadata-proto-path: proto path for the node proto within enclosing behavior
;                tree state proto
;   ?child-index: index of a node in the context of its parent, e.g., if the
;                 parent node is a sequence determines order of execution.
;   ?node-proto: the proto to import (recursively)
;
; Returns:
;   A multifield with the first entry TRUE/FALSE and
;   on success: as the second entry the fact address of the directly asserted
;               behavior-tree-node fact (this is not transitive for recursion)
;   on failure: a message describing why the import failed
(deffunction behavior-tree-import-node-proto (?tree-id ?parent-id
                                              ?blackboard-scope ?operation-name
                                              ?run-metadata-proto ?run-metadata-proto-path
                                              ?child-index
                                              ?node-proto)
  (bind ?name (pb-get-field ?node-proto "name"))
  (bind ?node-type (pb-which-oneof ?node-proto "node_type"))
  (bind ?node-type-sym (string-to-field (upcase ?node-type)))
  (bind ?nodetype-proto (pb-get-field ?node-proto ?node-type))
  (bind ?node-id (pb-get-field ?node-proto "id"))
  (bind ?sub-import-result (result-create TRUE ""))
  (bind ?children-protos (create$))
  (bind ?node-sub-tree-id nil)
  (bind ?node-retry-max-tries 0)
  (bind ?node-retry-counter-key "")
  (bind ?node-retry-child-id 0)
  (bind ?node-retry-recovery-id 0)
  (bind ?node-comparison-value nil)
  (bind ?node-task-type NOT-SET)
  (bind ?node-action-prototype-uid nil)
  (bind ?node-behavior-call-instance-uid nil)
  (bind ?node-condition-id nil)
  (bind ?node-branch-if-id nil)
  (bind ?node-branch-then-id 0)
  (bind ?node-branch-else-id 0)
  (bind ?node-loop-mode COUNT)
  (bind ?node-loop-while-id nil)
  (bind ?node-loop-max-times 0)
  (bind ?node-loop-for-each-loop-protos (create$))
  (bind ?node-loop-for-each-value-blackboard-key "")
  (bind ?node-loop-for-each-input-protos (create$))
  (bind ?node-loop-for-each-generator-expression "")
  (bind ?node-loop-for-each-generator-expression-id 0)
  (bind ?node-loop-do-id 0)
  (bind ?node-loop-counter-key "")
  (bind ?node-data-blackboard-key "")
  (bind ?node-data-operation NONE)
  (bind ?node-data-create-update-proto 0)
  (bind ?node-breakpoint-type NONE)
  (bind ?node-execution-mode NORMAL)
  (bind ?node-execution-mode-result-state AUTO)
  (bind ?node-debug-suspend FALSE)
  (bind ?node-debug-resume-state SUCCEEDED)
  (bind ?node-on-failure-emit-extended-status-proto-id 0)
  (bind ?node-on-failure-emit-extended-status-blackboard-key "")

  (if (= ?node-id 0) then
    (return (result-create FALSE (str-cat "tree " ?tree-id " unexpectedly had"
      " an node id (0). This should have been generated by a prior step")))
  )

  (if (eq ?node-type NOT-SET) then
    (bind ?node-as-string (pb-tostring ?node-proto))
    ; No need to remove ?nodetype-proto as that cannot exist
    (return
      (result-create FALSE
                     (str-cat "Node " ?node-as-string
                              " has no entry set in node_type oneof")))
  )

  (if (<> ?run-metadata-proto 0) then
    (pb-set-field ?run-metadata-proto (proto-path-join ?run-metadata-proto-path "state") ACCEPTED)
    (pb-set-field ?run-metadata-proto (proto-path-join ?run-metadata-proto-path "id") ?node-id)
  )
  (if (pb-has-field ?node-proto "decorators") then
    (bind ?decorators-proto (pb-get-field ?node-proto "decorators"))

    (if (pb-has-field ?decorators-proto "condition") then
      (bind ?node-condition-id (sym-cat ?tree-id "-" ?node-id "-condition"))
      (bind ?condition-proto (pb-get-field ?decorators-proto "condition"))
      (bind ?s-path
        (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "decorators.condition"))
      (bind ?import-cond-result
        (behavior-tree-import-condition ?condition-proto ?tree-id ?node-id
                                        ?run-metadata-proto ?s-path
                                        ?node-condition-id nil
                                        ?blackboard-scope ?operation-name 1))
      (pb-remove ?condition-proto)
      (if (not (result-ok ?import-cond-result))
       then
         (pb-remove ?decorators-proto)
         (pb-remove ?nodetype-proto)
         (return ?import-cond-result)
      )
    )

    (if (pb-has-field ?decorators-proto "breakpoint") then
      (bind ?breakpoint-type
        (pb-get-field ?decorators-proto "breakpoint"))
      (if (neq ?breakpoint-type TYPE_UNSPECIFIED) then
        (bind ?node-breakpoint-type ?breakpoint-type))
    )

    (if (pb-has-field ?decorators-proto "execution_settings") then
      (bind ?execution-settings-proto
        (pb-get-field ?decorators-proto "execution_settings"))
      (bind ?execution-mode NORMAL)
      (switch (pb-get-field ?execution-settings-proto "mode")
        (case UNSPECIFIED then
          (bind ?node-execution-mode NORMAL)
        )
        (case NORMAL then
          (bind ?node-execution-mode NORMAL)
        )
        (case DISABLED then
          (bind ?node-execution-mode DISABLED)
        )
        (default
          (pb-remove ?execution-settings-proto)
          (pb-remove ?decorators-proto)
          (pb-remove ?nodetype-proto)
          (return (result-create FALSE (str-cat "Importing node execution mode "
                    "encountered an unknown execution mode: "
                    (pb-get-field ?execution-settings-proto "mode"))))
        )
      )
      (if (pb-has-field ?execution-settings-proto "disabled_result_state") then
        (bind ?disabled-result-state
          (pb-get-field ?execution-settings-proto "disabled_result_state"))
        (switch ?disabled-result-state
          (case SUCCEEDED then
            (bind ?node-execution-mode-result-state SUCCEEDED)
          )
          (case FAILED then
            (bind ?node-execution-mode-result-state FAILED)
          )
          (default
            (pb-remove ?execution-settings-proto)
            (pb-remove ?decorators-proto)
            (pb-remove ?nodetype-proto)
            (return (result-create FALSE (str-cat "Importing node execution has"
                      " an invalid disabled_result_state: "
                      ?disabled-result-state "."
                      " Only SUCCEEDED or FAILED are valid.")))
          )
        )
      )
      (pb-remove ?execution-settings-proto)
    )

    (if (pb-has-field ?decorators-proto "on_failure.emit_extended_status") then
      (bind ?failure-settings-proto
        (pb-get-field ?decorators-proto "on_failure"))

      (if (pb-has-field ?failure-settings-proto "emit_extended_status") then
        (bind ?es-settings-proto
          (pb-get-field ?failure-settings-proto "emit_extended_status"))
        (bind ?node-on-failure-emit-extended-status-blackboard-key
          (pb-get-field ?es-settings-proto "to_blackboard_key"))
        (if (pb-has-field ?es-settings-proto "extended_status") then
          (bind ?node-on-failure-emit-extended-status-proto-id
            (pb-get-field ?es-settings-proto "extended_status"))
        )
        (pb-remove ?es-settings-proto)
      )

      (pb-remove ?failure-settings-proto)
    )

    (pb-remove ?decorators-proto)
  )

  (switch ?node-type
    (case sequence then
      (bind ?children-protos
        (pb-get-repeated-values ?nodetype-proto "children"))
      (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "sequence.children"))
      (bind ?sub-import-result
        (behavior-tree-import-node-protos ?tree-id ?node-id
                                          ?blackboard-scope ?operation-name
                                          ?run-metadata-proto ?s-path
                                          ?children-protos))
    )

    (case parallel then
      (bind ?children-protos
        (pb-get-repeated-values ?nodetype-proto "children"))
      (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "parallel.children"))
      (bind ?sub-import-result
        (behavior-tree-import-node-protos ?tree-id ?node-id
                                          ?blackboard-scope ?operation-name
                                          ?run-metadata-proto ?s-path
                                          ?children-protos))
    )

    (case selector then
      (bind ?children-size (pb-get-repeated-length ?nodetype-proto "children"))
      (bind ?branches-size (pb-get-repeated-length ?nodetype-proto "branches"))
      (if (and (> ?children-size 0) (> ?branches-size 0))
        then
          ; Either children or branches must be set
          (bind ?sub-import-result
            (result-create FALSE (str-cat "A Selector node must have either "
              "'children' or 'branches' but not both")))
        else
          (if (> ?children-size 0) then
            (bind ?children-protos
              (pb-get-repeated-values ?nodetype-proto "children"))
            (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "selector.children"))
            (bind ?sub-import-result
              (behavior-tree-import-node-protos ?tree-id ?node-id
                                                ?blackboard-scope ?operation-name
                                                ?run-metadata-proto ?s-path
                                                ?children-protos))
          )
          (if (> ?branches-size 0) then
            (bind ?children-protos
              (pb-get-repeated-values ?nodetype-proto "branches"))
            (bind ?run-metadata-proto-path-prefix
              (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "selector.branches"))
            (loop-for-count (?i ?branches-size)
              (bind ?branch-proto (nth$ ?i ?children-protos))
              (bind ?s-path (if (<> ?run-metadata-proto 0)
                             then (format nil "%s[%d]"
                                          ?run-metadata-proto-path-prefix (- ?i 1))
                             else ""))
              (bind ?sub-import-result
                (behavior-tree-import-selector-branch ?branch-proto ?tree-id ?node-id
                  ?blackboard-scope ?operation-name ?run-metadata-proto ?s-path ?i))
              (if (not (result-ok ?sub-import-result)) then
                (break)
              )
            )
          )
      )
    )

    (case fallback then
      (bind ?children-size (pb-get-repeated-length ?nodetype-proto "children"))
      (bind ?tries-size (pb-get-repeated-length ?nodetype-proto "tries"))
      (if (and (> ?children-size 0) (> ?tries-size 0))
        then
          ; Either children or tries must be set
          (bind ?sub-import-result
            (result-create FALSE (str-cat "A Fallback node must have either "
              "'children' or 'tries' but not both")))
        else
          (if (> ?children-size 0) then
            (bind ?children-protos
              (pb-get-repeated-values ?nodetype-proto "children"))
            (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "fallback.children"))
            (bind ?sub-import-result
              (behavior-tree-import-node-protos ?tree-id ?node-id
                                                ?blackboard-scope ?operation-name
                                                ?run-metadata-proto ?s-path
                                                ?children-protos))
          )
          (if (> ?tries-size 0) then
            (bind ?children-protos
              (pb-get-repeated-values ?nodetype-proto "tries"))
            (bind ?run-metadata-proto-path-prefix
              (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "fallback.tries"))
            (loop-for-count (?i ?tries-size)
              (bind ?try-proto (nth$ ?i ?children-protos))
              (bind ?s-path (if (<> ?run-metadata-proto 0)
                             then (format nil "%s[%d]"
                                          ?run-metadata-proto-path-prefix (- ?i 1))
                             else ""))
              (bind ?sub-import-result
                (behavior-tree-import-fallback-try ?try-proto ?tree-id ?node-id
                  ?blackboard-scope ?operation-name ?run-metadata-proto ?s-path ?i))
              (if (not (result-ok ?sub-import-result)) then
                (break)
              )
            )
          )
      )
    )

    (case branch then
      (bind ?node-branch-if-id (sym-cat ?tree-id "-" ?node-id "-if"))
      (bind ?if-proto (pb-get-field ?nodetype-proto "if"))
      (if (eq ?if-proto MISSING-FIELD) then
        (bind ?sub-import-result
          (result-create FALSE "Cannot import branch node with missing 'if' condition"))
      )
      (if (result-ok ?sub-import-result) then
        (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "branch.if"))
        (bind ?sub-import-result
          (behavior-tree-import-condition ?if-proto ?tree-id ?node-id
                                          ?run-metadata-proto ?s-path
                                          ?node-branch-if-id nil
                                          ?blackboard-scope ?operation-name 1))
        (pb-remove ?if-proto)
      )

      (if (result-ok ?sub-import-result) then
        (if (pb-has-field ?nodetype-proto "then") then
          (bind ?then-proto (pb-get-field ?nodetype-proto "then"))
          (bind ?then-node-type (pb-which-oneof ?then-proto "node_type"))

          (if (neq ?then-node-type NOT-SET) then
            (bind ?s-then-path
              (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "branch.then"))
            (bind ?sub-import-result
              (behavior-tree-import-node-proto ?tree-id ?node-id
                                               ?blackboard-scope ?operation-name
                                               ?run-metadata-proto ?s-then-path
                                               0 ?then-proto))
            (if (result-ok ?sub-import-result)
             then
              (bind ?branch-then-node
                (behavior-tree-import-node-proto-result-value-get-asserted-fact
                  ?sub-import-result))
              (bind ?node-branch-then-id
                (fact-slot-value ?branch-then-node id))
            )
          )
          (pb-remove ?then-proto)
        )

        (if (and (result-ok ?sub-import-result)
                 (pb-has-field ?nodetype-proto "else")) then
          (bind ?else-proto (pb-get-field ?nodetype-proto "else"))
          (bind ?else-node-type (pb-which-oneof ?else-proto "node_type"))

          (if (neq ?else-node-type NOT-SET) then
            (bind ?s-else-path
              (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "branch.else"))
            (bind ?sub-import-result
              (behavior-tree-import-node-proto ?tree-id ?node-id
                                               ?blackboard-scope ?operation-name
                                               ?run-metadata-proto ?s-else-path
                                               0 ?else-proto))
            (if (result-ok ?sub-import-result)
              then
                (bind ?branch-else-node
                  (behavior-tree-import-node-proto-result-value-get-asserted-fact
                    ?sub-import-result))
                (bind ?node-branch-else-id
                  (fact-slot-value ?branch-else-node id))
            )
          )
          (pb-remove ?else-proto)
        )
      )
    )

    (case loop then
      (bind ?node-loop-counter-key
        (pb-get-field ?nodetype-proto "loop_counter_blackboard_key"))
      (bind ?sub-import-result
        (is-valid-blackboard-key ?node-loop-counter-key
                                 "loop node loop_counter_blackboard_key"
                                 EMPTY-OK))
      (if (result-ok ?sub-import-result) then
        (if (pb-has-field ?nodetype-proto "do") then
          (bind ?do-proto (pb-get-field ?nodetype-proto "do"))
          (bind ?s-do-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "loop.do"))

          (bind ?sub-import-result
            (behavior-tree-import-node-proto ?tree-id ?node-id
                                             ?blackboard-scope ?operation-name
                                             ?run-metadata-proto ?s-do-path
                                             0 ?do-proto))

          (if (result-ok ?sub-import-result) then
            (bind ?loop-do-node
              (behavior-tree-import-node-proto-result-value-get-asserted-fact
                ?sub-import-result))
            (bind ?node-loop-do-id (fact-slot-value ?loop-do-node id))
          )
          (pb-remove ?do-proto)
        )
      )

      (if (result-ok ?sub-import-result) then
        (switch (pb-which-oneof ?nodetype-proto "loop_type")
          (case while then
            (bind ?node-loop-while-id (sym-cat ?tree-id "-" ?node-id "-while"))
            (bind ?node-loop-mode WHILE)
            (bind ?while-proto (pb-get-field ?nodetype-proto "while"))
            (bind ?s-while-path
              (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "loop.while"))
            (bind ?sub-import-result
              (behavior-tree-import-condition ?while-proto ?tree-id ?node-id
                                              ?run-metadata-proto ?s-while-path
                                              ?node-loop-while-id nil
                                              ?blackboard-scope ?operation-name
                                              1))
            (pb-remove ?while-proto)
            (if (pb-has-field ?nodetype-proto "max_times") then
              (bind ?node-loop-max-times
                (pb-get-field ?nodetype-proto "max_times"))
            )
          )
          (case for_each then
            (bind ?node-loop-mode FOR-EACH)
            (bind ?for-each-proto (pb-get-field ?nodetype-proto "for_each"))
            (bind ?node-loop-for-each-value-blackboard-key
                    (pb-get-field ?for-each-proto "value_blackboard_key"))

            (bind ?generic-descriptor-pool
              ?*PROTO-DESCRIPTOR-POOL-STANDARD-MESSAGES*)
            (switch (pb-which-oneof ?for-each-proto "for_each_generator_type")
              (case protos then
                (bind ?for-each-input-protos-anylist-proto
                  (pb-get-field ?for-each-proto "protos"))
                (bind ?node-loop-for-each-input-protos
                  (pb-get-repeated-values
                    ?for-each-input-protos-anylist-proto "items"))
                (pb-remove ?for-each-input-protos-anylist-proto)
                ; Already convert to unpacked loop-protos, if protos are given
                ; as input. This ensures that a failed conversion will fail
                ; already at import time and not at run time.
                (bind ?unpack-errors (create$))
                (foreach ?for-each-input-proto
                         ?node-loop-for-each-input-protos
                  (bind ?for-each-input-unpacked-proto-result
                    (pb-cast-from-any-with-pool
                      ?for-each-input-proto ?generic-descriptor-pool ""))
                  (if (result-ok ?for-each-input-unpacked-proto-result)
                    then
                      (bind ?node-loop-for-each-loop-protos
                        (append$ ?node-loop-for-each-loop-protos
                          (result-value ?for-each-input-unpacked-proto-result)))
                    else
                      (bind ?unpack-error-es-proto
                        (result-error ?for-each-input-unpacked-proto-result))
                      (bind ?unpack-error-es-msg
                        (pb-get-field ?unpack-error-es-proto
                                      "user_report.message"))
                      (pb-remove ?unpack-error-es-proto)
                      (bind ?unpack-errors (append$ ?unpack-errors (str-cat
                        "Failed to unpack any proto from for_each protos "
                        "with type: "
                        (pb-get-field ?for-each-input-proto "type_url")
                        ": " ?unpack-error-es-msg ".")))
                  )
                )
                (if (not (empty$ ?unpack-errors)) then
                  (foreach ?for-each-input-proto ?node-loop-for-each-input-protos
                    (pb-remove ?for-each-input-proto))
                  (foreach ?for-each-loop-proto ?node-loop-for-each-loop-protos
                    (pb-remove ?for-each-loop-proto))
                  (bind ?sub-import-result
                    (result-create FALSE (str-cat "Cannot import loop node: "
                                           (str-join "; " ?unpack-errors))))
                )
              )
              (case generator_cel_expression then
                (bind ?node-loop-for-each-generator-expression
                  (pb-get-field ?for-each-proto "generator_cel_expression"))
                (bind ?node-loop-for-each-generator-expression-id (cel-create
                  ?node-loop-for-each-generator-expression
                  ?generic-descriptor-pool))
                (if (= ?node-loop-for-each-generator-expression-id 0) then
                  (bind ?sub-import-result
                    (result-create FALSE (str-cat
                      "Cannot import loop node as creating the CEL expression"
                      " failed for " ?node-loop-for-each-generator-expression)))
                )
              )
              (default
                (bind ?sub-import-result
                  (result-create FALSE
                                 (str-cat "Cannot import loop node "
                                          "with invalid for_each.")))
              )
            )

            (pb-remove ?for-each-proto)

            (if (result-ok ?sub-import-result) then
              (if (pb-has-field ?nodetype-proto "max_times") then
                (bind ?node-loop-max-times
                  (pb-get-field ?nodetype-proto "max_times"))
                (if (<> ?node-loop-max-times 0) then
                  (foreach ?for-each-input-proto ?node-loop-for-each-input-protos
                    (pb-remove ?for-each-input-proto))
                  (foreach ?for-each-loop-proto ?node-loop-for-each-loop-protos
                    (pb-remove ?for-each-loop-proto))
                  (bind ?sub-import-result
                    (result-create FALSE
                      (str-cat "For each loop node defines max_times: "
                        ?node-loop-max-times
                        ". max_times cannot be set in loop nodes with"
                        " for_each.")))
                )
              )
            )
          )
          ; Neither while condition nor for_each specified, still observe
          ; max_times as a count loop.
          (default
            (bind ?node-loop-mode COUNT)
            (if (pb-has-field ?nodetype-proto "max_times") then
              (bind ?node-loop-max-times
                (pb-get-field ?nodetype-proto "max_times"))
            )
          )
        )
      )
    )

    (case retry then
      (bind ?node-retry-max-tries (pb-get-field ?nodetype-proto "max_tries"))
      (bind ?node-retry-counter-key
        (pb-get-field ?nodetype-proto "retry_counter_blackboard_key"))

      (bind ?sub-import-result
        (is-valid-blackboard-key ?node-retry-counter-key
                                 "retry node retry_counter_blackboard_key"
                                 EMPTY-OK))

      (if (result-ok ?sub-import-result) then
        (bind ?s-child-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "retry.child"))
        (bind ?child-proto (pb-get-field ?nodetype-proto "child"))
        (bind ?sub-import-result
          (behavior-tree-import-node-proto ?tree-id ?node-id
                                           ?blackboard-scope ?operation-name
                                           ?run-metadata-proto ?s-child-path
                                           0 ?child-proto))
        (pb-remove ?child-proto)
      )

      (if (result-ok ?sub-import-result) then
        (bind ?retry-child-node
          (behavior-tree-import-node-proto-result-value-get-asserted-fact
            ?sub-import-result))
        (bind ?node-retry-child-id (fact-slot-value ?retry-child-node id))

        (if (pb-has-field ?nodetype-proto "recovery") then
          (bind ?recovery-proto (pb-get-field ?nodetype-proto "recovery"))
          (bind ?s-recovery-path
            (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "retry.recovery"))
          (bind ?sub-import-result
            (behavior-tree-import-node-proto ?tree-id ?node-id
                                             ?blackboard-scope ?operation-name
                                             ?run-metadata-proto ?s-recovery-path
                                             0 ?recovery-proto))
          (pb-remove ?recovery-proto)
          (if (result-ok ?sub-import-result) then
            (bind ?retry-recovery-node
              (behavior-tree-import-node-proto-result-value-get-asserted-fact
                ?sub-import-result))
            (bind ?node-retry-recovery-id
              (fact-slot-value ?retry-recovery-node id))
          )
        )
      )
    )

    (case task then
      ; Convert to CLIPS style symbol as this is stored in the fact later
      (bind ?node-task-type
        (proto-field-to-symbol (pb-which-oneof ?nodetype-proto "task_type")))
      (if (<> ?run-metadata-proto 0) then
        (pb-set-field ?run-metadata-proto (proto-path-join ?run-metadata-proto-path "task.state") ACCEPTED)
      )
      (switch ?node-task-type
        (case CALL-BEHAVIOR then
          (bind ?behavior-call-proto
            (pb-get-field ?nodetype-proto "call_behavior"))
          (bind ?s-task-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "task"))
          (bind ?sub-import-result
            (behavior-call-import ?behavior-call-proto ?tree-id ?node-id
                                  ?operation-name
                                  ?run-metadata-proto ?s-task-path))
          (if (result-ok ?sub-import-result)
           then
            (bind ?action-or-behavior-call-instance (nth$ 2 ?sub-import-result))
            (if (eq (fact-relation ?action-or-behavior-call-instance)
                    plan-action) then
              (bind ?node-action-prototype-uid
                (fact-slot-value ?action-or-behavior-call-instance uid))
            )
            (if (eq (fact-relation ?action-or-behavior-call-instance)
                                   behavior-call-instance) then
              (bind ?node-behavior-call-instance-uid
                (fact-slot-value ?action-or-behavior-call-instance uid))
            )
           else
             (pb-remove ?behavior-call-proto)
          )
        )
        (case EXECUTE-CODE then
          (bind ?code-execution-proto
            (pb-get-field ?nodetype-proto "execute_code"))
          (bind ?sub-import-result
            (code-execution-import ?code-execution-proto ?operation-name
                                   ?tree-id ?node-id))
          (pb-remove ?code-execution-proto)
        )
        (default
          (bind ?sub-import-result
            (result-create FALSE
                           (str-cat "Cannot import task node "
                                    "without call_behavior or execute_code")))
        )
      )
    )

    ; Nothing to be done for fail node
    ;(case fail then)

    (case debug then
      (bind ?node-debug-suspend (pb-has-field ?nodetype-proto "suspend"))
      (if ?node-debug-suspend then
        (bind ?node-debug-resume-state
          (if (pb-get-field ?nodetype-proto "suspend.fail_on_resume")
           then FAILED else SUCCEEDED)))
    )

    (case sub_tree then
      (bind ?sub-tree-proto (pb-get-field ?nodetype-proto "tree"))
      (bind ?s-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "sub_tree.tree"))

      (bind ?sub-import-result
        (behavior-tree-import-proto ?sub-tree-proto ?tree-id
                                    ?blackboard-scope ?operation-name
                                    ?run-metadata-proto ?s-path))
      (if (result-ok ?sub-import-result) then
        (bind ?node-sub-tree-id
          (behavior-tree-import-proto-result-value-get-tree-id
            ?sub-import-result))
      )
      (pb-remove ?sub-tree-proto)
      (bind ?node-type-sym SUB-TREE)
    )

    (case data then
      (bind ?data-source (pb-which-oneof ?nodetype-proto "operation_type"))
      (switch ?data-source
        (case create_or_update then
          (bind ?node-data-operation CREATE-OR-UPDATE)
          (bind ?node-data-create-update-proto
            (pb-get-field ?nodetype-proto "create_or_update"))
          (bind ?node-data-blackboard-key
            (pb-get-field ?node-data-create-update-proto "blackboard_key"))
          (bind ?sub-import-result
            (is-valid-blackboard-key ?node-data-blackboard-key
                                     "data node create_or_udpate blackboard_key"
                                     NON-EMPTY))
          (if (result-ok ?sub-import-result)
           then
            (if (pb-has-field ?node-data-create-update-proto "from_world")
             then
              (bind ?proto-type-name
                (pb-get-any-field-type-name ?node-data-create-update-proto
                                            "from_world.proto"))
              (if (neq ?proto-type-name "intrinsic_proto.executive.WorldQuery")
               then
                 (bind ?proto-path
                   (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path
                               "data.create_or_update.from_world.proto"))
                 (pb-remove ?node-data-create-update-proto)
                 (bind ?sub-import-result
                   (result-create FALSE
                                  (str-cat "Field " ?proto-path
                                           " is of type " ?proto-type-name
                                           " but expected intrinsic_proto.executive.WorldQuery")))
               )
            )
           else
            (pb-remove ?node-data-create-update-proto)
          )
        )

        (case remove then
          (bind ?data-remove-proto (pb-get-field ?nodetype-proto "remove"))
          (bind ?node-data-operation REMOVE)
          (bind ?node-data-blackboard-key
            (pb-get-field ?data-remove-proto "blackboard_key"))
          (bind ?sub-import-result
            (is-valid-blackboard-key ?node-data-blackboard-key
                                     "data node remove blackboard_key"
                                     NON-EMPTY))
          (pb-remove ?data-remove-proto)
        )

        (default
          (bind ?sub-import-result
            (result-create FALSE
                           (str-cat "Node " ?node-as-string
                                    " has no field set for operation_type")))
        )
      )
    )

    (case FALSE then
      (bind ?node-as-string (pb-tostring ?node-proto))
      (bind ?sub-import-result
        (result-create FALSE
                       (str-cat "Node " ?node-as-string
                                " has no entry set in node_type oneof")))
    )
  )

  (foreach ?p ?children-protos (pb-remove ?p))
  (pb-remove ?nodetype-proto)

  ; If a recursive call above yielded an error, propagate that error.
  (if (not (result-ok ?sub-import-result)) then
    (return ?sub-import-result)
  )

  (bind ?node-fact
    (assert
      (behavior-tree-node
       (id ?node-id)
       (parent-id ?parent-id)
       (tree-id ?tree-id)
       (index ?child-index)
       (type ?node-type-sym)
       (name ?name)
       (run-metadata-proto-path ?run-metadata-proto-path)
       (breakpoint-type ?node-breakpoint-type)
       (run-metadata-proto-breakpoint-type ?node-breakpoint-type)
       (execution-mode ?node-execution-mode)
       (run-metadata-proto-execution-mode ?node-execution-mode)
       (execution-mode-result-state ?node-execution-mode-result-state)
       (run-metadata-proto-execution-mode-result-state ?node-execution-mode-result-state)
       (sub-tree-id ?node-sub-tree-id)
       (condition-id ?node-condition-id)
       (task-type ?node-task-type)
       (task-action-prototype-uid ?node-action-prototype-uid)
       (behavior-call-instance-uid ?node-behavior-call-instance-uid)
       (retry-child-id ?node-retry-child-id)
       (retry-recovery-id ?node-retry-recovery-id)
       (retry-max-tries ?node-retry-max-tries)
       (retry-counter-blackboard-key ?node-retry-counter-key)
       (loop-mode ?node-loop-mode)
       (loop-while-id ?node-loop-while-id)
       (loop-max-times ?node-loop-max-times)
       (loop-for-each-loop-protos ?node-loop-for-each-loop-protos)
       (loop-for-each-value-blackboard-key
         ?node-loop-for-each-value-blackboard-key)
       (loop-for-each-input-protos ?node-loop-for-each-input-protos)
       (loop-for-each-generator-expression
         ?node-loop-for-each-generator-expression)
       (loop-for-each-generator-expression-id
         ?node-loop-for-each-generator-expression-id)
       (loop-do-id ?node-loop-do-id)
       (loop-counter-blackboard-key ?node-loop-counter-key)
       (branch-if-id ?node-branch-if-id)
       (branch-then-id ?node-branch-then-id)
       (branch-else-id ?node-branch-else-id)
       (data-blackboard-key ?node-data-blackboard-key)
       (data-operation ?node-data-operation)
       (data-create-update-proto ?node-data-create-update-proto)
       (debug-suspend ?node-debug-suspend)
       (debug-resume-state ?node-debug-resume-state)
       (on-failure-emit-extended-status-proto-id
         ?node-on-failure-emit-extended-status-proto-id)
       (on-failure-emit-extended-status-blackboard-key
         ?node-on-failure-emit-extended-status-blackboard-key)
      )
    )
  )
  (return (result-create TRUE ?node-fact))
)

; Imports a list of node protos.
;
; Args:
;   ?tree-id: the behavior tree ID the nodes will belong to
;   ?parent-id: parent node ID for new nodes
;   ?blackboard-scope: The blackboard scope to use for the nodes, if relevant.
;   ?operation-name: Name of the operation to associate this tree to.
;   ?run-metadata-proto: proto ID for a RunMetadata proto to which state information
;                 will be written during execution.
;   ?run-metadata-proto-path-prefix: proto path for the node proto within enclosing behavior
;                       tree state proto. This is a prefix to be indexed.
;   ?node-protos: the protos to import (recursively)
;
; Returns:
;   A multifield with the first entry TRUE/FALSE and
;   on success: ""
;   on failure: a message describing why the import failed.
(deffunction behavior-tree-import-node-protos (?tree-id ?parent-id
                                               ?blackboard-scope ?operation-name
                                               ?run-metadata-proto ?run-metadata-proto-path-prefix
                                               ?node-protos)
  (bind ?num-protos (length$ ?node-protos))
  (loop-for-count (?i ?num-protos)
    (bind ?node-proto (nth$ ?i ?node-protos))
    (bind ?s-path (if (<> ?run-metadata-proto 0)
                   then (format nil "%s[%d]"
                                ?run-metadata-proto-path-prefix (- ?i 1))
                   else ""))
    (bind ?import-node-result
      (behavior-tree-import-node-proto ?tree-id ?parent-id
                                       ?blackboard-scope ?operation-name
                                       ?run-metadata-proto ?s-path
                                       ?i ?node-proto))
    (if (not (result-ok ?import-node-result)) then
        (return ?import-node-result)
    )
  )
  (return (result-create TRUE ""))
)

; Extract tree id from behavior-tree-import-proto result.
;
; Args:
;   ?result-value: A result value from a successful call to
;                  behavior-tree-import-proto.
;
; Returns:
;   The tree id of the imported tree.
(deffunction behavior-tree-import-proto-result-value-get-tree-id (?result-value)
  (return (nth$ 2 ?result-value))
)

; Imports a behavior tree defined from ?bt-proto.
;
; On failure, all facts and protos created during the (partial) import will be
; removed.
;
; Args:
;   ?bt-proto: The BehaviorTree proto to import.
;   ?parent-tree-id: is the tree id of the tree that contains the imported tree.
;   ?blackboard-scope: The blackboard scope to use for the new tree
;   ?operation-name: Name of the operation to associate this tree to.
;   ?run-metadata-proto: ID of a RunMetadata proto to fill with information about the
;                 BT that is being imported.
;   ?run-metadata-proto-path: proto path in an enclosing tree (if any) to reference values
;
; Returns:
;   A multifield with the first entry TRUE/FALSE and
;   on success: as the second entry the tree id symbol of the asserted
;               behavior-tree fact
;   on failure: a message describing why the import failed
(deffunction behavior-tree-import-proto (?bt-proto ?parent-tree-id
                                         ?blackboard-scope ?operation-name
                                         ?run-metadata-proto ?run-metadata-proto-path)
  (bind ?bt-name (pb-get-field ?bt-proto "name"))
  (bind ?tree-id (pb-get-field ?bt-proto "tree_id"))
  (if (eq ?tree-id "") then
    (return (result-create FALSE (str-cat "Unexpectedly found empty tree id "
      "during import. This should have been generated by a prior step.")))
  )
  (bind ?tree-id (sym-cat ?tree-id))

  (bind ?plan-id (sym-cat ?tree-id "-plan-" (gensym*)))

  (bind ?bt-return-value-expression
    (pb-get-field ?bt-proto "return_value_expression"))

  ; Error if plan with given plan-id already exists
  (if (any-factp ((?plan plan)) (eq ?plan:id ?plan-id))
   then (return (result-create FALSE
                               (str-cat "Plan " ?plan-id " already exists"))))

  ; Error if behavior tree with given tree-id already exists
  (if (any-factp ((?tree behavior-tree)) (eq ?tree:id ?tree-id))
   then (return (result-create FALSE (str-cat "Behavior Tree " ?tree-id
                                        " already exists"))))

  (bind ?s-root-path (gen-run-metadata-proto-path ?run-metadata-proto ?run-metadata-proto-path "root"))
  (bind ?root-proto (pb-get-field ?bt-proto "root"))
  (if (eq ?root-proto MISSING-FIELD) then
    (return (result-create FALSE (str-cat "Tree with id " ?tree-id
                                          " does not have a root node")))
  )
  (if (<> ?run-metadata-proto 0) then
    (pb-set-field ?run-metadata-proto (proto-path-join ?run-metadata-proto-path "state") ACCEPTED)
    (pb-set-field ?run-metadata-proto
                  (proto-path-join ?run-metadata-proto-path "blackboard_scope") ?blackboard-scope)
    (pb-set-field ?run-metadata-proto
                  (proto-path-join ?run-metadata-proto-path "tree_id") (str-cat ?tree-id))
  )
  (bind ?root-id (pb-get-field ?root-proto "id"))
  (if (= ?root-id 0) then
    (return (result-create FALSE (str-cat "tree " ?tree-id " unexpectedly had"
      " an empty root node id (0). This should have been generated by a prior"
      " step")))
  )

  (assert (plan (id ?plan-id) (type BEHAVIOR-TREE))
          (behavior-tree (id ?tree-id) (name ?bt-name)
                         (blackboard-scope ?blackboard-scope)
                         (operation-name ?operation-name)
                         (parent-behavior-tree-id ?parent-tree-id)
                         (plan-id ?plan-id)
                         (root ?root-id) (start-node-id ?root-id)
                         (run-metadata-proto-path ?run-metadata-proto-path)
                         (return-value-expression ?bt-return-value-expression)))

  (bind ?import-node-result
    (behavior-tree-import-node-proto ?tree-id 0
                                     ?blackboard-scope ?operation-name
                                     ?run-metadata-proto ?s-root-path
                                     0 ?root-proto))
  (pb-remove ?root-proto)
  (if (not (result-ok ?import-node-result)) then
     (behavior-tree-delete ?tree-id)
     (return ?import-node-result)
  )

  (return (result-create TRUE ?tree-id))
)

; Get tree ids for all trees.
;
; Returns:
;   A multifield containing all tree ids currently in the executive.
(deffunction behavior-tree-import-get-all-tree-ids ()
  (bind ?tree-ids (create$))
  (do-for-fact ((?bt behavior-tree)) TRUE
    (bind ?tree-ids (append$ ?tree-ids ?bt:id))
  )
  (return ?tree-ids)
)

; Import a top-level BehaviorTree proto.
;
; Args:
;   ?bt-proto: A BehaviorTree proto to import.
;   ?operation-name: Name of the operation the tree is associated with.
;
; Returns:
;   Multifield pair with the first entry TRUE/FALSE and the second entry:
;     On success: The tree id of the imported top-level tree.
;     On failure: a message indicating why the import failed.
(deffunction behavior-tree-import-top-level-proto (?bt-proto ?operation-name
                                                   ?run-metadata-proto ?run-metadata-proto-path)
  ; TODO(b/319835803): Make blackboard scope unique across operations. This
  ; needs to be adapted for client assumptions about this hard-coded scope to
  ; exist.
  (bind ?blackboard-scope ?*BLACKBOARD-SCOPE-PROCESS-TREE*)

  ; Verify that no id from ?bt-proto is already in use
  (bind ?global-tree-ids (behavior-tree-import-get-all-tree-ids))
  ; Ensure consistent tree and node ids for the complete bt-proto.
  (bind ?bt-proto-ensure-ids-result
    (ensure-valid-ids ?bt-proto ?global-tree-ids))
  (if (not (result-ok ?bt-proto-ensure-ids-result)) then
    (bind ?es-proto (result-error ?bt-proto-ensure-ids-result))
    ; Import doesn't have ES support, yet. Just print the contents.
    (bind ?message (pb-get-field ?es-proto "user_report.message"))
    (pb-remove ?es-proto)

    (return (result-create FALSE ?message))
  )

  (errors-retract-recoverable)

  (bind ?import-tree-result
    (behavior-tree-import-proto ?bt-proto nil
                                ?blackboard-scope ?operation-name
                                ?run-metadata-proto ?run-metadata-proto-path))

  (bind ?tree-id nil)
  (if (result-ok ?import-tree-result)
   then
    (bind ?tree-id
      (behavior-tree-import-proto-result-value-get-tree-id ?import-tree-result))
   else
    (return ?import-tree-result)
  )

  (return (result-create TRUE ?tree-id))
)

; Import a BehaviorTree proto into an operation.
;
; The imported BehaviorTree is set as the process tree in the executive state.
;
; Args:
;   ?bt-proto: A BehaviorTree proto to import.
;   ?operation-name: Name of the operation to associate this tree to.
;
; Returns:
;   A result pair with the first value TRUE/FALSE and the second value an
;   optional error message.
(deffunction behavior-tree-import-process-tree-proto (?bt-proto ?operation-name)
  (if (not (any-factp ((?es executive-state)) (eq ?es:state ACTIVE))) then
    (return (result-create FALSE "No executive state on import or not active."))
  )

  (if (any-factp ((?op operation-envelope)) (eq ?op:name ?operation-name)) then
    (bind ?message (str-cat "Found existing operation with name: "
                            ?operation-name ". Delete this first before "
                            "importing a new one."))
    (return (result-create FALSE ?message))
  )

  ; TODO(b/319835803): Remove to allow multiple operations.
  (if (any-factp ((?op operation-envelope)) TRUE) then
    (return (result-create FALSE
      (str-cat "Found existing operation when trying to import process tree. "
               "Delete the operation before importing a new process tree.")))
  )

  (bind ?operation-proto (pb-create "google.longrunning.Operation"))
  (bind ?s-metadata-proto (pb-create "intrinsic_proto.executive.RunMetadata"))
  (pb-set-field ?s-metadata-proto "behavior_tree" ?bt-proto)

  (pb-set-field ?s-metadata-proto "sequence_number" 1)


  ; Set the initial values on the proto to match the default values of the
  ; operation-envelope slots and their run-metadata-proto-* default slots.
  (pb-set-field ?s-metadata-proto "operation_state" ACCEPTED)
  (pb-set-field ?s-metadata-proto "execution_mode"
                (operation-execution-mode-to-proto-mode NORMAL))
  (pb-set-field ?s-metadata-proto "simulation_mode"
                (operation-sim-mode-to-proto-mode REALITY))

  (bind ?tree-import-result
    (behavior-tree-import-top-level-proto ?bt-proto ?operation-name
                                          ?s-metadata-proto "behavior_tree"))

  (if (not (result-ok ?tree-import-result)) then
    (pb-remove ?operation-proto)
    (pb-remove ?s-metadata-proto)
    (return ?tree-import-result)
  )

  ; Set the file descriptor sets if the tree has parameters or return-value
  (bind ?operation-parameter-descriptor-set 0)
  (bind ?operation-parameter-message-name "")
  (bind ?operation-return-value-descriptor-set 0)
  (bind ?operation-return-value-message-name "")
  (if (pb-has-field ?bt-proto "description") then
    (bind ?bt-description (pb-get-field ?bt-proto "description"))
    (if (pb-has-field ?bt-description "parameter_description") then
      (bind ?bt-parameter-description
        (pb-get-field ?bt-description "parameter_description"))
      (bind ?operation-parameter-message-name
        (pb-get-field ?bt-parameter-description "parameter_message_full_name"))
      (bind ?operation-parameter-descriptor-set
        (pb-get-field ?bt-parameter-description "parameter_descriptor_fileset"))
      (pb-remove ?bt-parameter-description)
    )
    (if (pb-has-field ?bt-description "return_value_description") then
      (bind ?bt-return-value-description
        (pb-get-field ?bt-description "return_value_description"))
      (bind ?operation-return-value-message-name
        (pb-get-field ?bt-return-value-description "return_value_message_full_name"))
      (bind ?operation-return-value-descriptor-set
        (pb-get-field ?bt-return-value-description "descriptor_fileset"))
      (pb-remove ?bt-return-value-description)
    )

    (pb-remove ?bt-description)
  )
  (bind ?operation-parameter-pool 0)
  (bind ?operation-return-value-pool 0)
  (if (<> ?operation-parameter-descriptor-set 0) then
    (bind ?operation-parameter-pool
      (pb-add-descriptor-pool ?operation-parameter-descriptor-set
        (str-cat "Parameter descriptors for Operation'" ?operation-name "'")
        "type.googleapis.com/"  ; TODO(b/438412095) set Intrinsic type URL
        ?operation-name))
    (if (= ?operation-parameter-pool 0) then
      (return (result-create FALSE (str-cat "Failed to create operation "
        "parameter pool from descriptor set: "
        (pb-tostring ?operation-parameter-descriptor-set))))
    )
  )
  (if (<> ?operation-return-value-descriptor-set 0) then
    (bind ?operation-return-value-pool
      (pb-add-descriptor-pool ?operation-return-value-descriptor-set
        (str-cat "Return value descriptors for Operation '" ?operation-name "'")
        "type.googleapis.com/"  ; TODO(b/438412095) set Intrinsic type URL
        ?operation-name))
    (if (= ?operation-return-value-pool 0) then
      (return (result-create FALSE (str-cat "Failed to create operation "
        "return value pool from descriptor set: "
        (pb-tostring ?operation-return-value-descriptor-set))))
    )
  )

  (bind ?operation-tree-id (result-value ?tree-import-result))

  (assert (operation-envelope (name ?operation-name)
                              (operation-proto ?operation-proto)
                              (run-metadata-proto ?s-metadata-proto)
                              (parameter-descriptor-set-proto
                                ?operation-parameter-descriptor-set)
                              (parameter-message-name
                                ?operation-parameter-message-name)
                              (parameter-descriptor-pool
                                ?operation-parameter-pool)
                              (return-value-descriptor-set-proto
                                ?operation-return-value-descriptor-set)
                              (return-value-message-name
                                ?operation-return-value-message-name)
                              (return-value-descriptor-pool
                                ?operation-return-value-pool)
                              (operation-tree-id ?operation-tree-id)
                              (start-tree-id ?operation-tree-id)))

  (return (result-create TRUE ""))
)
