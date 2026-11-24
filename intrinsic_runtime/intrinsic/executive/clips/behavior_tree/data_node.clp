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

; Processing rules for a node of type DATA.

; The DATA node is a leaf node that can be used to create, update, or delete
; data in the blackboard.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-data-node-create-or-update
  "A data node that creates or updates blackboard data"
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name)
                 (blackboard-scope ?blackboard-scope))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (type DATA)
                               (state READY)
                               (data-operation CREATE-OR-UPDATE)
                               (data-blackboard-key ?blackboard-key)
                               (data-create-update-proto ?create-update-proto))
  (world (id ?world-id))
 =>
  (bind ?generic-descriptor-pool
    ?*PROTO-DESCRIPTOR-POOL-STANDARD-MESSAGES*)
  (switch (pb-which-oneof ?create-update-proto "input_type")
    (case cel_expression then
      ; Evaluate CEL Expression, put data into blackboard
      (bind ?cel-expr (pb-get-field ?create-update-proto "cel_expression"))
      (bind ?eval-result
        (cel-eval-to-proto-with-type ?cel-expr "" ?generic-descriptor-pool
                                     ?blackboard-scope ?op-name))
      (if (result-ok ?eval-result)
        then
          (assert (blackboard-update (key ?blackboard-key)
                                     (scope ?blackboard-scope)
                                     (operation-name ?op-name)
                                     (proto-id (result-value ?eval-result))))
          (behavior-tree-set-node-succeeded ?node)
        else
          (bind ?es-proto (result-error ?eval-result))
          (extended-status-set-related-to ?es-proto ?tree ?node)
          (behavior-tree-set-node-failed ?node EXECUTION
            "Failed to evaluate expression"
            EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
          (pb-remove ?es-proto)
      )
    )
    (case from_world then
      (bind ?from-world-proto (pb-get-field ?create-update-proto "from_world"))
      ; As the WorldQuery is used by the executive directly the generated pool
      ; contains descriptor for the world query to assign to.
      (bind ?assign-result
        (cel-proto-assign ?from-world-proto ?*PROTO-DESCRIPTOR-POOL-GENERATED*
                          "intrinsic_proto.executive.WorldQuery"
                          ?blackboard-scope ?op-name))
      (bind ?error-msg "")
      (bind ?error-es-proto 0)
      (if (not (result-ok ?assign-result))
       then
        (bind ?error-msg (str-cat "Value assignment for world query failed."))
        (bind ?error-es-proto (result-error ?assign-result))
       else
        (bind ?query-proto
          (pb-unpack-from-any-field ?from-world-proto "proto"
                                    ?*PROTO-DESCRIPTOR-POOL-GENERATED*))
        (if (= ?query-proto 0)
         then
           (bind ?error-msg "Failed to cast world query proto")
         else
          (if (neq (pb-get-type-name ?query-proto)
                   "intrinsic_proto.executive.WorldQuery")
           then
            (bind ?error-msg (str-cat "Proto is of type " (pb-get-type-name ?query-proto)
                       " but expected intrinsic_proto.executive.WorldQuery"))
           else
            (bind ?any-list-proto (world-query ?world-id ?query-proto))
            (if (= ?any-list-proto 0)
             then
              (bind ?error-msg "World query did not yield a result")
             else
              (assert (blackboard-update (key ?blackboard-key)
                                         (scope ?blackboard-scope)
                                         (operation-name ?op-name)
                                         (proto-id ?any-list-proto)))
              (behavior-tree-set-node-succeeded ?node)
            )
          )
        )
        (pb-remove ?query-proto)
      )
      (pb-remove ?from-world-proto)
      (if (neq ?error-msg "") then
        (bind ?es-proto (extended-status-create 32100))
        (extended-status-set-message ?es-proto USER ?error-msg)
        (extended-status-set-related-to ?es-proto ?tree ?node)
        (if (<> ?error-es-proto 0) then
          (extended-status-add-context ?es-proto ?error-es-proto)
        )
        (behavior-tree-set-node-failed ?node EXECUTION ?error-msg
          EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
        (pb-remove ?es-proto)
      )
      (pb-remove ?error-es-proto)
    )
    (case proto then
      (bind ?any-proto (pb-get-field ?create-update-proto "proto"))

      (bind ?type-url (pb-get-field ?any-proto "type_url"))
      (if (eq ?type-url MISSING-FIELD)
        then
          (bind ?message "Proto is not a valid Any")
          (bind ?es-proto (extended-status-create 32100))
          (extended-status-set-message ?es-proto USER ?message)
          (extended-status-set-related-to ?es-proto ?tree ?node)
          (behavior-tree-set-node-failed ?node EXECUTION ?message
            EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
          (pb-remove ?es-proto)
          (return)
      )

      ; If the proto is a skill proto use a matching descriptor pool, otherwise
      ; fall back to the generic pool.
      (bind ?get-skill-id-result
        (pb-get-skill-or-asset-id-from-type-url ?type-url))
      (bind ?descriptor-pool ?generic-descriptor-pool)
      (if (result-ok ?get-skill-id-result) then
        (do-for-fact ((?si skill-info))
          (eq ?si:skill-id (result-value ?get-skill-id-result))
          (bind ?descriptor-pool ?si:parameter-descriptor-pool-id)
        )
      )

      (bind ?proto-cast-result
        (pb-cast-from-any-with-pool ?any-proto ?descriptor-pool ""))
      (pb-remove ?any-proto)
      (if (result-ok ?proto-cast-result)
       then
        (bind ?proto-id (result-value ?proto-cast-result))
        (assert (blackboard-update (key ?blackboard-key)
                                   (scope ?blackboard-scope)
                                   (operation-name ?op-name)
                                   (proto-id ?proto-id)))
        (behavior-tree-set-node-succeeded ?node)
       else
        (bind ?es-proto-id (result-error ?proto-cast-result))
        (behavior-tree-set-node-failed ?node EXECUTION
           (str-cat "Failed to cast data node input proto from Any")
           EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto-id)
        (pb-remove ?es-proto-id)
      )
    )
    (case protos then
      (bind ?proto-id (pb-get-field ?create-update-proto "protos"))
      (assert (blackboard-update (key ?blackboard-key)
                                 (scope ?blackboard-scope)
                                 (operation-name ?op-name)
                                 (proto-id ?proto-id)))
      (behavior-tree-set-node-succeeded ?node)
    )

    (default
      (bind ?es-proto (extended-status-create 32100))
      (extended-status-set-message ?es-proto USER "Missing input type")
      (extended-status-set-related-to ?es-proto ?tree ?node)
      (behavior-tree-set-node-failed ?node EXECUTION "Missing input_type"
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?es-proto)
    )
  )
)

(defrule behavior-tree-data-node-remove
  "A data node that removes blackboard data"
  (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name)
                 (blackboard-scope ?blackboard-scope))
  ?node <- (behavior-tree-node (tree-id ?tree-id) (type DATA)
                               (state READY)
                               (data-operation REMOVE)
                               (data-blackboard-key ?blackboard-key))
 =>
  (blackboard-remove ?blackboard-key ?blackboard-scope ?op-name)
  (behavior-tree-set-node-succeeded ?node)
)
