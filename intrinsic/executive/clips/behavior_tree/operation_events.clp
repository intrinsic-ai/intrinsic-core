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

(deffunction operation-events-add-node-state-change-event (?operation-name
               ?tree-id ?node-id ?proto-state ?was-recovered)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))
  (set-node-identifier-proto ?event-proto "node_state.node_identifier"
                             ?tree-id ?node-id)

  (pb-set-field ?event-proto "node_state.state" ?proto-state)
  (pb-set-field ?event-proto "node_state.recovered" ?was-recovered)
  (operation-events-add-event ?operation-name ?event-proto)

)

(deffunction operation-events-add-counter-change-event (?operation-name
               ?tree-id ?node-id ?counter)

  (bind ?event-proto
    (pb-create "intrinsic_proto.executive.OperationEvent"))
  (set-node-identifier-proto ?event-proto "counter.node_identifier"
                             ?tree-id ?node-id)

  (pb-set-field ?event-proto "counter.value" ?counter)
  (operation-events-add-event ?operation-name ?event-proto)

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

