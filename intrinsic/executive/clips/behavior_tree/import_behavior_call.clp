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

; Import BehaviorCalls

; --------------------- FORWARD DECLARATIONS  ---------------------------------

(deffunction behavior-tree-import-proto-result-value-get-tree-id
  (?result-value))
(deffunction behavior-tree-import-proto (?bt-proto ?parent-tree-id
                                         ?blackboard-scope ?operation-name
                                         ?element-path-prefix
                                         ?run-metadata-proto ?run-metadata-proto-path))

; --------------------------------- FUNCTIONS ---------------------------------

; Determines, if the given ?behavior-call-proto-id is a skill or
; parameterizable behavior tree.
;
; Args:
;   ?behavior-call-proto: The BehaviorCall to determine the skill type for.
;
; Returns:
;   A multifield pair with TRUE/FALSE and as the second parameter:
;   on success: either SKILL or BEHAVIOR-TREE (see skill-info:skill-type).
;   on failure: message why the type could not be determined.
(deffunction behavior-call-determine-type (?behavior-call-proto)
  (bind ?skill-id (pb-get-field ?behavior-call-proto "skill_id"))

  (do-for-fact ((?si skill-info)) (eq ?si:skill-id ?skill-id)
    (return (result-create TRUE ?si:skill-type))
  )
  (do-for-fact ((?noop noop-action-info))
    (member$ ?skill-id ?noop:noop-action-names)
    (return (result-create TRUE SKILL))
  )

  (return (result-create FALSE (str-cat "Could not find skill or noop action "
                                        ?skill-id ". Is the skill "
                                        "registered in the application?")))
)

; Imports a BehaviorCall that is a parameterizable behavior tree.
;
; Args:
;   ?behavior-call-proto-id: The BehaviorCall referring to a BEHAVIOR-TREE skill
;   ?tree-id: The tree that the behavior-call-instance is in.
;   ?node-id: The node that the behavior-call-instance is in.
;   ?operation-name: Name of the operation to associate this tree to.
;   ?run-metadata-proto: Associated run metadata proto with the tree for export
;   ?run-metadata-proto-path: Run metadata path of the node containing the
;     task with the BehaviorCall. The element path of this node is the element
;     path prefix for all elements in the imported PBT instance.
;
; Returns:
;   A multifield with the first entry TRUE/FALSE and
;   on success: as the second entry fact-adress of the asserted fact
;   on failure: a message describing why the import failed
(deffunction behavior-call-import-tree (?behavior-call-proto-id
                                        ?tree-id ?node-id
                                        ?operation-name
                                        ?run-metadata-proto ?run-metadata-proto-path)
  (bind ?skill-id (pb-get-field ?behavior-call-proto-id "skill_id"))
  (bind ?get-bt-result (skill-get-behavior-tree-proto ?skill-id))

  (if (not (result-ok ?get-bt-result)) then
    (return (result-create FALSE (str-cat "Failed to get tree for skill id: "
      ?skill-id " when importing behavior instance: "
      (result-error ?get-bt-result))))
  )

  (bind ?pbt-proto (result-value ?get-bt-result))

  ; Ensure unique ids for this proto instance
  (bind ?tree-id-prefix (generate-tree-id-prefix-for-node ?tree-id ?node-id))
  (bind ?pbt-proto-ensure-ids-result
    (ensure-valid-ids-with-prefix ?pbt-proto ?tree-id-prefix))
  (if (not (result-ok ?pbt-proto-ensure-ids-result)) then
    (pb-remove ?pbt-proto)
    (bind ?es-proto (result-error ?pbt-proto-ensure-ids-result))
    ; Import doesn't have ES support, yet. Just print the contents.
    (bind ?message (pb-get-field ?es-proto "user_report.message"))
    (pb-remove ?es-proto)
    (return (result-create FALSE (str-cat "Inconsistent tree ids within PBT "
                                   "with skill id " ?skill-id ": " ?message)))
  )

  (bind ?s-tree-path "")
  (bind ?element-path-prefix (create$))
  (if (and (<> ?run-metadata-proto 0) (neq ?run-metadata-proto-path "")) then
    ; Insert the PBT proto in the task node in the state proto.
    ; It is important to do this before importing the PBT, so that the state
    ; proto is prepared with the PBT.
    (bind ?s-tree-path
      (proto-path-join ?run-metadata-proto-path "task.called_tree_state"))
    (if (pb-has-field ?run-metadata-proto ?s-tree-path) then
      ; TODO(b/353890789): Make this a warning, but not an error as it is
      ; possible to import this, although maybe not what a user wants.
      ; Warning message should read similar to: Field for the called tree state
      ; was already set at: ?s-tree-path This is an output only field and must
      ; not be set for importing.
    )
    (pb-set-field ?run-metadata-proto ?s-tree-path ?pbt-proto)


    ; The PBT is a new nested process instance: Its elements are prefixed with
    ; the element path of the calling node, which must already be written to the
    ; run metadata.
    (bind ?element-path-prefix
      (pb-get-repeated-values ?run-metadata-proto
        (proto-path-join ?run-metadata-proto-path "element_path.ids")))
    (if (empty$ ?element-path-prefix) then
      (pb-remove ?pbt-proto)
      (return (result-create FALSE
        (str-cat "Element path of the node calling behavior tree with skill id "
                 ?skill-id " is not set. It must be set before importing the "
                 "called behavior tree.")))
    )

  )

  (bind ?uid (behavior-call-instance-generate-uid ?tree-id))
  ; Blackboard scope for the PBT is the internal tree id, e.g., BT-TEST:1/PBT-ID
  (bind ?pbt-tree-scope (pb-get-field ?pbt-proto "tree_id"))
  ; Note: Using ?pbt-tree-scope as the scope, thus opening a new scope for a PBT
  (bind ?pbt-import-result
    (behavior-tree-import-proto ?pbt-proto ?tree-id
                                ?pbt-tree-scope ?operation-name
                                ?element-path-prefix
                                ?run-metadata-proto ?s-tree-path))
  (pb-remove ?pbt-proto)
  (if (not (result-ok ?pbt-import-result)) then
    (return (result-create FALSE (str-cat "Failed to import parameterized tree:"
      " " (result-error ?pbt-import-result) " for behavior tree with skill id "
      ?skill-id)))
  )
  (bind ?pbt-tree-id
    (behavior-tree-import-proto-result-value-get-tree-id ?pbt-import-result))

  (bind ?asserted-fact (assert
    (behavior-call-instance (uid ?uid) (skill-type BEHAVIOR-TREE)
      (behavior-call-prototype-proto ?behavior-call-proto-id)
      (parameterizable-tree-id ?pbt-tree-id))))

  (return (result-create TRUE ?asserted-fact))
)

; Imports a single plan-action from a BehaviorCall proto.
;
; On success a plan-action fact is asserted. In conformance with the usage of
; the plan-action fact, an action proto will be created and stored in the
; plan-action fact. The passed in ?behavior-call-proto will also be stored.
;
; Args:
;   plan-id: ID to set as plan-id in action
;   action-id: ID to set as id in action
;   behavior-call-proto: proto with behavior specification for a skill,
;     read data from, create action proto and set equivalent data there,
;     set proto-id slot to the action proto (both protos must persist for the
;     entire lifetime of the plan-action fact).
;   action-uid: UID of the action to import
;
; Returns:
;   Pair with two entries:
;   1: TRUE for success, FALSE for failure
;   2: on success: fact-index of asserted plan-action fact,
;      on failure: text message explaining the failure
(deffunction behavior-call-import-skill
    (?plan-id ?action-id ?behavior-call-proto ?action-uid)
  (bind ?skill-id (pb-get-field ?behavior-call-proto "skill_id"))
  (bind ?return-value-name
    (pb-get-field ?behavior-call-proto "return_value_name"))
  (bind ?valid-key-result (is-valid-blackboard-key ?return-value-name
                                                   "import skill"
                                                   EMPTY-OK))
  (if (not (result-ok ?valid-key-result)) then
    (return (result-create FALSE (str-cat "Skill " ?skill-id " uses an invalid "
                                          "key for the return_value_name: "
                                          (result-error ?valid-key-result))))
  )

  (bind ?ch-mode UNSPECIFIED)
  (if (pb-has-field ?behavior-call-proto "skill_execution_options") then
    (bind ?conflict-handling-mode
      (pb-get-field ?behavior-call-proto "skill_execution_options.conflict_handling_mode"))
    (if (eq ?conflict-handling-mode FAIL) then (bind ?ch-mode FAIL))
    (if (eq ?conflict-handling-mode WAIT) then (bind ?ch-mode WAIT))
  )

  (bind ?asserted-fact
    (assert (plan-action (id ?action-id) (uid ?action-uid)
                         (plan-id ?plan-id)
                         (skill-id ?skill-id)
                         (return-value-name ?return-value-name)
                         (conflict-handling-mode ?ch-mode)
                         (behavior-call-proto-id ?behavior-call-proto))))
  (return (create$ TRUE ?asserted-fact))
)

; Imports a BehaviorCall proto into a given tree with ?tree-id.
; Depending on the BehaviorCall either a skill or a parameterizable behavior
; tree will be imported.
;
; Args:
;   ?behavior-call-proto-id: BehaviorCall proto to import
;   ?tree-id: tree-id of the tree containing the behavior call
;   ?node-id: node id of the node containing the behavior call
;   ?operation-name: Name of the operation to associate this tree to.
;   ?run-metadata-proto: Associated run metadata proto with the tree for export
;   ?run-metadata-proto-path: Run metadata path of the node containing the
;     task with the BehaviorCall
;
; Returns:
;   A multifield with the first entry TRUE/FALSE and
;   on success: as the second entry fact-adress of the asserted plan-action or
;               behavior-call-instance
;   on failure: a message describing why the import failed
(deffunction behavior-call-import (?behavior-call-proto-id ?tree-id ?node-id
                                   ?operation-name
                                   ?run-metadata-proto ?run-metadata-proto-path)
  (bind ?skill-type-result
    (behavior-call-determine-type ?behavior-call-proto-id))

  (if (not (result-ok ?skill-type-result)) then
    (return (result-create FALSE (result-error ?skill-type-result)))
  )
  (switch (result-value ?skill-type-result)
    (case SKILL then
      (bind ?prototype-plan-id (sym-cat ?tree-id "-action-prototypes"))
      (bind ?action-id (plan-next-action-id ?prototype-plan-id))
      (bind ?action-uid
        (plan-action-uid ?prototype-plan-id ?action-id))
      (return
        (behavior-call-import-skill ?prototype-plan-id ?action-id
                                    ?behavior-call-proto-id ?action-uid))
    )
    (case BEHAVIOR-TREE then
      (return (behavior-call-import-tree ?behavior-call-proto-id
                                         ?tree-id ?node-id
                                         ?operation-name
                                         ?run-metadata-proto ?run-metadata-proto-path))
    )
  )
)
