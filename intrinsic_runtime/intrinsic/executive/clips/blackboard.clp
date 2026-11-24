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

; Blackboard for exchanging information gathered during execution (i.e. return
; values, inflows).

; The TDD describing the blackboard can be found here:
; go/intrinsic-behavior-trees
; The usage of the blackboard for return values and inflows is described here:
; go/intrinsic-behavior-tree-data-flow-design

; --------------------------------- GLOBALS - ---------------------------------

; LINT.IfChange
(defglobal ?*BLACKBOARD-SCOPE-PROCESS-TREE* = "PROCESS_TREE")
; LINT.ThenChange(//intrinsic_apis/intrinsic/executive/proto/executive_service.proto)

; --------------------------------- TEMPLATES ---------------------------------

; Fact representing the blackboard used for relaying return values and inflows.
; A blackboard-item is uniquely identified by its key and scope.
(deftemplate blackboard-item
  (slot key (type STRING) (default ?NONE))
  (slot scope (type STRING) (default ?NONE))
  (slot operation-name (type STRING) (default ?NONE))
  (slot proto-id (type INTEGER) (default ?NONE))
  (slot keep-on-flush (type SYMBOL) (allowed-values TRUE FALSE)
                      (default FALSE))
  (multislot updated (type INTEGER) (cardinality 2 2) (default-dynamic (now)))
)

(deftemplate blackboard-update
  (slot key (type STRING) (default ?NONE))
  (slot scope (type STRING) (default ?NONE))
  (slot operation-name (type STRING) (default ?NONE))
  (slot proto-id (type INTEGER) (default ?NONE))
  (slot keep-on-flush (type SYMBOL) (allowed-values TRUE FALSE)
                      (default FALSE))
  ; Time the blackboard-update was requested
  ; Updates to the same value are expected to be processed in order
  (multislot requested (type INTEGER) (cardinality 2 2) (default-dynamic (now)))

  ; A source where the blackboard-update originated
  ; TODO(b/438409848) Extend possible sources (service, snapshot, execution -> params)
  (slot source-type (type SYMBOL) (allowed-values UNKNOWN NODE)
                    (default UNKNOWN))
  ; Only valid when source is a NODE as part of its execution logic (e.g., a
  ; loop counter)
  (slot source-tree-id (type SYMBOL))
  (slot source-node-id (type INTEGER))
)

; ---------------------------- FORWARD DECLARATIONS ----------------------------

(deffunction behavior-tree-update-counters
  (?key ?scope ?operation-name ?blackboard-counter-value
   ?source-type ?source-tree-id ?source-node-id))

; --------------------------------- FUNCTIONS ---------------------------------

(deffunction blackboard-flush (?scope ?operation-name)
  "Removes all entries in ?scope for which keep-on-flush is not set."
  (delayed-do-for-all-facts ((?bi blackboard-item))
    (and (eq ?bi:keep-on-flush FALSE)
         (eq ?bi:scope ?scope)
         (eq ?bi:operation-name ?operation-name))
    (if (neq ?bi:proto-id 0) then (pb-remove ?bi:proto-id))
    (retract ?bi)
  )
)

(deffunction blackboard-remove (?key ?scope ?operation-name)
  "Removes all entries from the blackboard with matching key."
  (do-for-fact ((?bi blackboard-item))
    (and (eq ?bi:key ?key)
         (eq ?bi:scope ?scope)
         (eq ?bi:operation-name ?operation-name))

    (if (neq ?bi:proto-id 0) then (pb-remove ?bi:proto-id))
    (retract ?bi)
  )
)

(deffunction blackboard-remove-operation-protos (?operation-name)
  "Removes all entries for the given operation."
  (delayed-do-for-all-facts ((?bi blackboard-item))
    (eq ?bi:operation-name ?operation-name)
    (if (neq ?bi:proto-id 0) then (pb-remove ?bi:proto-id))
    (retract ?bi)
  )
)

; Updates counters of nodes if they have the given ?key as a blackboard counter.
(deffunction blackboard-update-counters (?key ?scope ?operation-name
                                             ?proto-id ?source-type
                                             ?source-tree-id ?source-node-id)
  (if (= ?proto-id 0) then
    (return)
  )
  (if (not (pb-has-field ?proto-id "value")) then
    (return)
  )

  ; TODO(b/438409848) Define from what sources updates are accepted
  ; For now, disallow updates from NODE as updates are currently too inefficient.
  (if (eq ?source-type NODE) then
    (return)
  )

  (bind ?blackboard-counter-value (pb-get-field ?proto-id "value"))
  ; The proto either doesn't have a `value` field (returns error of type SYMBOL)
  ; or is not an INTEGER and thus cannot be a counter.
  (if (neq (type ?blackboard-counter-value) INTEGER) then
    (return)
  )

  (behavior-tree-update-counters ?key ?scope ?operation-name ?blackboard-counter-value
                                 ?source-type ?source-tree-id ?source-node-id)
)

; ----------------------------------- RULES -----------------------------------

(defrule blackboard-update-exists
  "Removes and re-creates blackboard-item fact for already existing key."
  ; Adding items with high salience to ensure they are evaluated before
  ; conditions.
  (declare (salience ?*SALIENCE-HIGH*))
  ?u <- (blackboard-update (key ?key) (scope ?scope) (operation-name ?op-name)
                           (proto-id ?proto-id) (keep-on-flush ?keep-on-flush)
                           (requested $?request-time)
                           (source-type ?source-type)
                           (source-tree-id ?source-tree-id)
                           (source-node-id ?source-node-id))
  ?item <- (blackboard-item (key ?key) (scope ?scope) (operation-name ?op-name)
                            (proto-id ?previous-proto-id))

  (not (exists
    (blackboard-update (key ?key) (scope ?scope) (operation-name ?op-name)
                       (requested
                         $?other-request-time&:(time> ?request-time ?other-request-time)))))
 =>
  (if (neq ?previous-proto-id 0) then (pb-remove ?previous-proto-id))
  (modify ?item (proto-id ?proto-id) (keep-on-flush ?keep-on-flush)
                (updated (now)))
  (retract ?u)

  (blackboard-update-counters ?key ?scope ?op-name ?proto-id
                              ?source-type ?source-tree-id ?source-node-id)

  (if (= ?proto-id 0) then
    (printout error (str-cat "Got blackboard-update with empty proto for " ?key
      " in " ?scope " from " ?op-name) crlf)
  )
)

(defrule blackboard-update-new
  "Creates new blackboard-item fact for blackboard-update."
  ; Adding items with high salience to ensure they are evaluated before
  ; conditions and PBT executions.
  (declare (salience ?*SALIENCE-HIGH*))
  ?u <- (blackboard-update (key ?key) (scope ?scope) (operation-name ?op-name)
                           (proto-id ?proto-id) (keep-on-flush ?keep-on-flush)
                           (requested $?request-time)
                           (source-type ?source-type)
                           (source-tree-id ?source-tree-id)
                           (source-node-id ?source-node-id))
  (not (blackboard-item (key ?key) (scope ?scope) (operation-name ?op-name)))

  (not (exists
    (blackboard-update (key ?key) (scope ?scope) (operation-name ?op-name)
                       (requested
                         $?other-request-time&:(time> ?request-time ?other-request-time)))))
 =>
  (assert (blackboard-item (key ?key) (scope ?scope) (operation-name ?op-name)
                           (proto-id ?proto-id) (keep-on-flush ?keep-on-flush)))
  (retract ?u)

  (blackboard-update-counters ?key ?scope ?op-name ?proto-id
                              ?source-type ?source-tree-id ?source-node-id)

  (if (= ?proto-id 0) then
    (printout error (str-cat "Got new blackboard-update with empty proto for " ?key
      " in " ?scope " from " ?op-name) crlf)
  )
)
