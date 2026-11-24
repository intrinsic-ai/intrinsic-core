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

; Executive Inflow CLIPS code (go/intrinsic-executive-predicate-inflow-design)

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate inflow
  "Configuration for a single inflow (data input port)."
  (slot id (type INTEGER) (default ?NONE))
  (slot dds-topic (type STRING) (default ?NONE))

  (slot expr-string (type STRING) (default ?NONE))
  (slot expr-proto (type INTEGER) (default ?NONE))
)

(deftemplate inflow-predicate-mapping
  "Mapping incoming inflow updates to facts to assert/retract."
  (slot inflow-id (type INTEGER) (default ?NONE))
  (slot predicate-name (type SYMBOL) (default ?NONE))
  ; The following must have the exact same arity as the predicate
  (multislot param-values)
)

(deftemplate inflow-update
  "This declares an update for a specific inflow. It is expected to be asserted
   frequently and cleaned up once processed."
  (slot inflow-id (type INTEGER) (default ?NONE))
  (slot expr-result (type SYMBOL) (allowed-values FALSE TRUE) (default ?NONE))
  (multislot timestamp (type INTEGER) (cardinality 2 2) (default-dynamic (now)))
)

; ----------------------------------- RULES -----------------------------------

(defrule inflow-remove-old-updates
  "If there are multiple updates for the same inflow, delete older udpates.
   This way, the following rules will always only process the latest update.
   We need a higher salience here to ensure that we cleanup old stuff before
   processing."
  (declare (salience ?*SALIENCE-HIGH*))
  ?if <- (inflow-update (inflow-id ?inflow-id) (timestamp $?t1))
  (inflow-update (inflow-id ?inflow-id) (timestamp $?t2&:(time> ?t2 ?t1)))
 =>
  (retract ?if)
)
