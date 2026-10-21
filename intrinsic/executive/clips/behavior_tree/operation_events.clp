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

; Operation Events
;
; Publishes events that happened on a behavior tree during a CLIPS run at the
; end of the run.

; Requires:
; - saliences.clp: salience globals


(deffunction operation-events-add-event (?operation-name ?event-proto))

; --------------------------------- TEMPLATES ---------------------------------

; Collects operation events to be send out. There should only be at most one
; fact per operation that collects events.
(deftemplate operation-events
  ; Name of the operation this refers to.
  (slot operation-name (type STRING))

  ; Proto id containing the current events.
  ; This is always an intrinsic_proto.executive.OperationEvents.
  (slot events-proto (type INTEGER))
)


; --------------------------------- FUNCTIONS ---------------------------------


(deffunction run-metadata-proto-get-element-path
    (?run-metadata-proto-path ?operation-name)

  (do-for-fact ((?op operation-envelope)) (eq ?op:name ?operation-name)
    (if (<> ?op:run-metadata-proto 0) then
      (return
        (pb-get-repeated-values ?op:run-metadata-proto
          (proto-path-join ?run-metadata-proto-path "element_path.ids")))
    )
  )

  (return (create$))
)


(deffunction operation-events-add-tree-state-change-event (?operation-name
               ?proto-state ?run-metadata-proto-path)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))

  (bind ?element-path
    (run-metadata-proto-get-element-path ?run-metadata-proto-path
                                         ?operation-name))
  (pb-set-repeated-values ?event-proto "tree_state.element_path.ids"
                          ?element-path)


  (pb-set-field ?event-proto "tree_state.state" ?proto-state)
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-node-state-change-event (?operation-name
               ?proto-state ?was-recovered ?run-metadata-proto-path)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))

  (bind ?element-path
    (run-metadata-proto-get-element-path ?run-metadata-proto-path
                                         ?operation-name))
  (pb-set-repeated-values ?event-proto "node_state.element_path.ids"
                          ?element-path)


  (pb-set-field ?event-proto "node_state.state" ?proto-state)
  (pb-set-field ?event-proto "node_state.recovered" ?was-recovered)
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-condition-state-change-event (?operation-name
               ?proto-state ?satisfied ?run-metadata-proto-path)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))

  (bind ?element-path
    (run-metadata-proto-get-element-path ?run-metadata-proto-path
                                         ?operation-name))
  (pb-set-repeated-values ?event-proto "condition_state.element_path.ids"
                          ?element-path)


  (pb-set-field ?event-proto "condition_state.state" ?proto-state)
  (if (eq ?proto-state FINISHED) then
    (pb-set-field ?event-proto "condition_state.satisfied" ?satisfied)
  )
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-task-node-state-change-event (?operation-name
               ?proto-state ?run-metadata-proto-path)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))

  (bind ?element-path
    (run-metadata-proto-get-element-path ?run-metadata-proto-path
                                         ?operation-name))
  (pb-set-repeated-values ?event-proto "task_node_state.element_path.ids"
                          ?element-path)


  (pb-set-field ?event-proto "task_node_state.state" ?proto-state)
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-task-node-execute-code-stdout-event
               (?operation-name ?stdout ?run-metadata-proto-path)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))

  (bind ?element-path
    (run-metadata-proto-get-element-path ?run-metadata-proto-path
                                         ?operation-name))
  (pb-set-repeated-values
    ?event-proto "task_node_execute_code_stdout.element_path.ids" ?element-path)


  (pb-set-field ?event-proto "task_node_execute_code_stdout.stdout" ?stdout)
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-counter-change-event (?operation-name
               ?counter ?run-metadata-proto-path)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))

  (bind ?element-path
    (run-metadata-proto-get-element-path ?run-metadata-proto-path
                                         ?operation-name))
  (pb-set-repeated-values ?event-proto "counter.element_path.ids" ?element-path)


  (pb-set-field ?event-proto "counter.value" ?counter)
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-operation-state-change-event (?operation-name
               ?proto-state)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))
  (pb-set-field ?event-proto "operation_state.operation_state" ?proto-state)
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-operation-result-change-event (?operation-name
               ?operation-proto)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))
  (pb-set-field ?event-proto "operation_result.done"
                (pb-get-field ?operation-proto "done"))
  (if (pb-has-field ?operation-proto "error") then
    (bind ?error-proto (pb-get-field ?operation-proto "error"))
    (pb-set-field ?event-proto "operation_result.error" ?error-proto)
    (pb-remove ?error-proto)
  )
  (if (pb-has-field ?operation-proto "response") then
    (bind ?response-proto (pb-get-field ?operation-proto "response"))
    (pb-set-field ?event-proto "operation_result.response" ?response-proto)
    (pb-remove ?response-proto)
  )
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-breakpoint-change-event (?operation-name
               ?tree-id ?node-id ?breakpoint-type)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))
  (pb-set-field ?event-proto "breakpoint.breakpoint.tree_id" ?tree-id)
  (pb-set-field ?event-proto "breakpoint.breakpoint.node_id" ?node-id)
  (if (neq ?breakpoint-type NONE) then
    (pb-set-field ?event-proto "breakpoint.breakpoint.type" ?breakpoint-type)
  )
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-node-execution-settings-change-event
               (?operation-name ?mode ?result-state ?run-metadata-proto-path)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))

  (bind ?element-path
    (run-metadata-proto-get-element-path ?run-metadata-proto-path
                                         ?operation-name))
  (pb-set-repeated-values
    ?event-proto "node_execution_settings.element_path.ids" ?element-path)


  (pb-set-field ?event-proto "node_execution_settings.settings.mode" ?mode)
  ; disabled_result_state will only be set if the mode is set to DISABLED and
  ; the user explicitly set a custom behavior. See behavior-tree-node slot
  ; execution-mode-result-state for AUTO behavior.
  (if (and (eq ?mode DISABLED) (neq ?result-state AUTO)) then
    (pb-set-field ?event-proto
                  "node_execution_settings.settings.disabled_result_state"
                  ?result-state)
  )
  (operation-events-add-event ?operation-name ?event-proto)

)

; Adds a RunMetadataChangeEvent for ?operation-name.
;
; The event is a snapshot of all RunMetadataChangeEvent fields, copied from the
; operation's run-metadata-proto. Thus call this after the run-metadata-proto
; has been updated.
;
; Args:
;   ?operation-name: Name of the operation for this event
(deffunction operation-events-add-run-metadata-change-event (?operation-name)

  (do-for-fact ((?op operation-envelope)) (eq ?op:name ?operation-name)
    (bind ?rmd-proto ?op:run-metadata-proto)
    (if (= ?rmd-proto 0) then
      (return)
    )

    (bind ?event-proto
      (pb-create "intrinsic_proto.executive.OperationEvent"))
    (foreach ?field (create$ "world_id" "scene_id" "execution_mode"
                             "simulation_mode")
      (pb-set-field ?event-proto (str-cat "run_metadata." ?field)
                    (pb-get-field ?rmd-proto ?field))
    )
    (foreach ?field (create$ "start_tree_id" "start_node_id")
      (if (pb-has-field ?rmd-proto ?field) then
        (pb-set-field ?event-proto (str-cat "run_metadata." ?field)
                      (pb-get-field ?rmd-proto ?field))
      )
    )
    (if (pb-has-field ?rmd-proto "start_time") then
      (bind ?start-time-proto (pb-get-field ?rmd-proto "start_time"))
      (pb-set-field ?event-proto "run_metadata.start_time" ?start-time-proto)
      (pb-remove ?start-time-proto)
    )
    (operation-events-add-event ?operation-name ?event-proto)
  )

)



; Adds an event for ?operation-name
;
; Args:
;   ?operation-name: Name of the operation for this event
;   ?event-proto: An OperationEvent proto. This proto is consumed by this
;                 function.
(deffunction operation-events-add-event (?operation-name ?event-proto)
  (if (= ?event-proto 0) then
    (return)
  )

  (do-for-fact ((?op-events operation-events))
               (eq ?op-events:operation-name ?operation-name)
    (pb-set-field ?op-events:events-proto "events[*]" ?event-proto)
    (pb-remove ?event-proto)
    (return)
  )

  ; No operation-events fact for ?operation-name. Otherwise the do-for-fact
  ; would have found it. Create a new one with an OperationEvents proto.
  (bind ?events-proto (pb-create "intrinsic_proto.executive.OperationEvents"))
  (pb-set-field ?events-proto "events[*]" ?event-proto)
  (pb-remove ?event-proto)
  (assert (operation-events (operation-name ?operation-name)
                            (events-proto ?events-proto)))
)

; ----------------------------------- RULES -----------------------------------

(defrule operation-events-publish
  (declare (salience ?*SALIENCE-COLLECT-EVENTS*))
  ?events <- (operation-events
               (operation-name ?op-name) (events-proto ?events-proto))
  (operation-envelope (name ?op-name) (run-metadata-proto ?rmd-proto)
                                      (operation-proto ?op-proto))
 =>
  (if (= ?events-proto 0) then
    (retract ?events)
    (return)
  )
  (if (= (pb-get-repeated-length ?events-proto "events") 0) then
    (pb-remove ?events-proto)
    (retract ?events)
    (return)
  )
  (if (or (= ?rmd-proto 0) (= ?op-proto 0)) then
    (printout error "Got operation events, but not operation or "
                    "runmetadata proto. Something is inconsistent." crlf)
    (pb-remove ?events-proto)
    (retract ?events)
    (return)
  )

  (bind ?last-rmd-sequence (pb-get-field ?rmd-proto "sequence_number"))
  (bind ?current-rmd-sequence (+ ?last-rmd-sequence 1))

  (pb-set-field ?rmd-proto "sequence_number" ?current-rmd-sequence)
  (pb-set-field ?events-proto "sequence_number" ?current-rmd-sequence)

  (bind ?events-topic (str-cat "/executive/operations/" ?op-name "/events"))
  (pubsub-publish ?events-topic ?events-proto)

  (pb-remove ?events-proto)
  (retract ?events)
)

