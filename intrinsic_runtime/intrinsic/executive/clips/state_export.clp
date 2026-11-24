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

; Code that deals with exporting Executive operation state

; Requires:
; - protobuf: create protos

; --------------------------------- FUNCTIONS ---------------------------------

; This function creates a LoggedOperationProto from working memory.
; Collects process, state, and world and aggregates into single proto suitable
; for gRPC response or logging.
;
; Args:
;   ?world-id: world ID to record in the state
;   ?tree-id: tree ID of the node that triggered the log/action
;   ?node-id: node ID of the node that triggered the log/action
;   ?options: additional options. Supported options are:
;     INCLUDE-CONTEXT: sets log_context field
;
; Returns:
;   ID of generated proto stored in ProtobufManager.
(deffunction logged-operation-create-proto (?world-id ?tree-id ?node-id $?options)
  (bind ?p (pb-create "intrinsic_proto.executive.LoggedOperation"))

  (bind ?session-log-id 0)
  (do-for-fact ((?es executive-state)) TRUE
    (bind ?session-log-id ?es:session-log-id)
  )

  (do-for-fact ((?op operation-envelope)) TRUE
    (bind ?operation-proto (pb-create "google.longrunning.Operation"))

    ; Update the tracked RunMetadata. All values but the behavior_tree must be
    ; set. The behavior_tree field is updated during execution.
    (bind ?metadata-proto ?op:run-metadata-proto)

    (pb-set-field ?metadata-proto "world_id" ?world-id)
    (pb-set-field ?metadata-proto "execution_mode"
                  (operation-execution-mode-to-proto-mode ?op:execution-mode))
    (pb-set-field ?metadata-proto "simulation_mode"
                  (operation-sim-mode-to-proto-mode ?op:simulation-mode))

    (bind ?op-tree-id ?op:operation-tree-id)
    (bind ?bt-log-id 0)
    (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?op-tree-id)
      (bind ?bt-log-id ?bt:log-id)
      (pb-set-field ?operation-proto "name" ?op:name)
      ; The DEPRECATED behavior_tree_state field (defined in behavior_tree.proto
      ; as intrinsic_proto.executive.BehaviorTree.State) does not contain a
      ; PREPARING enum value. Therefore, we map PREPARING to RUNNING for
      ; backward compatibility (matching clips_executive_service.cc).
      (if (eq ?op:state PREPARING)
        then (pb-set-field ?metadata-proto "behavior_tree_state" RUNNING)
        else (pb-set-field ?metadata-proto "behavior_tree_state" ?op:state)
      )
      (pb-set-field ?metadata-proto "operation_state" ?op:state)
      (switch ?op:state
        (case SUCCEEDED then
          (pb-set-field ?operation-proto "done" TRUE)
          (bind ?result-proto
            (pb-create "intrinsic_proto.executive.RunResponse")
          )
          (pb-pack-to-any-field ?operation-proto "response" ?result-proto)
          (pb-remove ?result-proto)
        )
        (case FAILED then
          (pb-set-field ?operation-proto "done" TRUE)
          (bind ?error-proto (pb-create "google.rpc.Status"))
          ; We don't know which kind of error this was, hence set a custom code.
          ; 2 is UNKNOWN
          (pb-set-field ?error-proto "code" 2)
          (pb-set-field ?error-proto "message" "Behavior Tree failed execution")
          (bind ?es-proto (pb-clone ?op:extended-status-proto-id))
          (if (= ?es-proto 0) then
            (bind ?es-proto (extended-status-create 13000))
            (extended-status-set-message ?es-proto USER
              "Failed to retrieve extended status from operation")
          )
          (bind ?es-any-proto (pb-cast-to-any ?es-proto))
          (pb-remove ?es-proto)
          (pb-set-field ?error-proto "details[*]" ?es-any-proto)
          (pb-remove ?es-any-proto)
          (pb-set-field ?operation-proto "error" ?error-proto)
          (pb-remove ?error-proto)
        )
        (case CANCELED then
          (pb-set-field ?operation-proto "done" TRUE)
          (bind ?error-proto (pb-create "google.rpc.Status"))
          ; We don't know which kind of error this was, hence set a custom code.
          ; 1 is CANCELLED
          (pb-set-field ?error-proto "code" 1)
          (pb-set-field ?error-proto "message" "User cancelled Behavior Tree")
          (pb-set-field ?operation-proto "error" ?error-proto)
          (pb-remove ?error-proto)
        )
      )
    )

    (bind ?tracing-info-proto
      (pb-create "intrinsic_proto.executive.RunMetadata.TracingInfo"))

    (pb-set-field ?tracing-info-proto "skill_trace_handling"
      (operation-skill-trace-handling-to-proto-mode ?op:skill-trace-handling))

    (bind ?add-tracing-info TRUE)
    (do-for-fact ((?ati-flag flag))
        (and (eq ?ati-flag:name tracing-add-tracing-info-to-state)
             (eq ?ati-flag:type BOOL))
      (bind ?add-tracing-info ?ati-flag:value)
    )

    (if ?add-tracing-info then
      (if (or (neq ?op:trace-id "") (neq ?op:trace-url "")) then
        (pb-set-field ?tracing-info-proto "trace_id" ?op:trace-id)
        (pb-set-field ?tracing-info-proto "trace_url" ?op:trace-url)
      )
    )
    (pb-set-field ?metadata-proto "tracing_info" ?tracing-info-proto)
    (pb-remove ?tracing-info-proto)

    (if (member$ INCLUDE-CONTEXT ?options) then
      (bind ?context-proto (log-context-create ?session-log-id ?bt-log-id 0 0
                                               ?op:name))
      (pb-set-field ?metadata-proto "log_context" ?context-proto)
      (pb-remove ?context-proto)
    )

    (if (and (neq ?tree-id "") (neq ?node-id 0)) then
      (bind ?bt-context
        (pb-create "intrinsic_proto.executive.BehaviorTreeContext"))
      (bind ?node-identifier
        (pb-create "intrinsic_proto.executive.BehaviorTreeContext.NodeIdentifier"))
      (pb-set-field ?node-identifier "tree_id" ?tree-id)
      (pb-set-field ?node-identifier "node_id" ?node-id)
      (pb-set-field ?bt-context "action_node_identifier" ?node-identifier)
      (pb-remove ?node-identifier)

      (pb-set-field ?p "behavior_tree_context" ?bt-context)
      (pb-remove ?bt-context)
    )

    (pb-set-field ?p "operation" ?operation-proto)

    (pb-remove ?operation-proto)

    (pb-pack-to-any-field ?p "operation.metadata" ?metadata-proto)
  )
  (return ?p)
)
