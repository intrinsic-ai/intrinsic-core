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

; Import CodeExecution

; --------------------------------- FUNCTIONS ---------------------------------

; Imports a CodeExecution proto for a given task node.
;
; Args:
;   ?code-execution-proto: CodeExecution proto to import
;   ?operation-name: Name of the operation that this CodeExecution is in
;   ?tree-id: tree id of the task node containing the CodeExecution
;   ?node-id: node id of the task node containing the CodeExecution
;
; Returns:
;   A multifield with the first entry TRUE/FALSE and
;   on success: as the second entry fact-adress of the asserted
;               code-execution-instance
;   on failure: a message describing why the import failed
(deffunction code-execution-import (?code-execution-proto
                                    ?operation-name ?tree-id ?node-id)
  (bind ?return-value-key
    (pb-get-field ?code-execution-proto "return_value_key"))
  (bind ?valid-key-result
    (is-valid-blackboard-key ?return-value-key
                             (str-cat "code-execution at tree: " ?tree-id
                                      " node: " ?node-id)
                             EMPTY-OK))
  (if (not (result-ok ?valid-key-result)) then
    (return ?valid-key-result)
  )

  (bind ?code-proto 0)
  (bind ?code-type (pb-which-oneof ?code-execution-proto "code"))
  (switch ?code-type
    (case python_code then
      (bind ?code-proto
        (pb-get-field ?code-execution-proto "python_code"))
    )
    (default
      (return (result-create FALSE (str-cat "Could not import CodeExecution"
        " as no code proto was set for node " ?tree-id ":" ?node-id)))
    )
  )

  (bind ?parameter-message-full-name
    (pb-get-field ?code-execution-proto "parameter_message_full_name"))
  (bind ?return-value-message-full-name
    (pb-get-field ?code-execution-proto "return_value_message_full_name"))

  (bind ?prototype-parameters-proto 0)
  (if (pb-has-field ?code-execution-proto "parameters")
    then
      (bind ?prototype-parameters-proto (pb-get-field ?code-execution-proto "parameters"))
    else
      (if (neq ?parameter-message-full-name "") then
        (pb-remove ?code-proto)
        (return (result-create FALSE (str-cat "Could not import CodeExecution"
          " as no parameters proto was set for node " ?tree-id ":" ?node-id
          " but a parameter message was defined")))
      )
  )

  (bind ?file-descriptor-set-proto 0)
  (bind ?file-descriptor-set-pool 0)
  (if (pb-has-field ?code-execution-proto "file_descriptor_set")
    then
      (bind ?file-descriptor-set-proto (pb-get-field ?code-execution-proto "file_descriptor_set"))

      (bind ?file-descriptor-set-pool
          (pb-add-descriptor-pool ?file-descriptor-set-proto
            (str-cat "code-execution-instance (operation_name: " ?operation-name
                    ", tree_id: " ?tree-id ", node_id: " ?node-id ")")
            (behavior-tree-process-type-url-prefix
              (behavior-tree-get-process-asset-id ?tree-id))
            ?operation-name))
      (if (= ?file-descriptor-set-pool 0) then
        (return (result-create FALSE
          (str-cat "Code Execution: Pool creation failed for node " ?tree-id
                  ":" ?node-id)))
      )
    else
      ; code-execution-proto does not have "file_descriptor_set"
      (if (or (neq ?parameter-message-full-name "") (neq ?return-value-message-full-name "")) then
        (pb-remove ?code-proto)
        (pb-remove ?prototype-parameters-proto)
        (return (result-create FALSE (str-cat "Could not import CodeExecution"
          " as no file_descriptor_set proto was set for node " ?tree-id ":" ?node-id
          " but a parameter and/or return value message was defined")))
      )
  )

  (bind ?cei (assert (code-execution-instance
    (operation-name ?operation-name)
    (tree-id ?tree-id)
    (node-id ?node-id)
    (parameters-prototype-proto ?prototype-parameters-proto)
    (code-proto ?code-proto)
    (return-value-key ?return-value-key)
    (parameter-message-full-name ?parameter-message-full-name)
    (return-value-message-full-name ?return-value-message-full-name)
    (file-descriptor-set-proto ?file-descriptor-set-proto)
    (file-descriptor-set-pool ?file-descriptor-set-pool)
  )))

  (return (result-create TRUE ?cei))
)
