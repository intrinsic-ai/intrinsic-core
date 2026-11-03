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


(defglobal
 ?*TRACING-TREE-START-EVENT* = "TREE-STARTED"
)


; ---------------------------- FORWARD DECLARATIONS ----------------------------

; Defined in behavior_tree/state_proto_update.clp
(deffunction run-metadata-proto-update-execution-time (?duration ?operation-name))

; --------------------------------- TEMPLATES ---------------------------------

; A time measurement is associated to a specific operation and updates itself
; based on that.
(deftemplate time-measurement
  (slot operation-name (type STRING) (default ?NONE))
  ; Track if there already was an update this CLIPS run to prevent infinite
  ; loops. At most one update per CLIPS run is performed.
  (multislot last-updated (type INTEGER) (cardinality 2 2))

  ; Start time of an operation. Will be default (0, 0) if the operation hasn't
  ; started yet or was reset.
  (multislot start-time (type INTEGER) (cardinality 2 2))
  ; Duration of an ongoing operation. Will be default (0.0) if the operation
  ; hasn't started yet or was reset.
  (slot duration (type FLOAT) (default 0.0))
)

; ----------------------------------- RULES -----------------------------------

(defrule time-measurement-start
  "Asserts time-measurement for an operation if it doesn't exist, yet."
  (operation-envelope (name ?op-name))
  (time (timestamp $?now))
  (not (time-measurement (operation-name ?op-name)))
 =>
  (assert (time-measurement (operation-name ?op-name)))
  (run-metadata-proto-update-execution-time 0.0 ?op-name)
)

(defrule time-measurement-retract
  "Retracts time-measurement if the operation not longer exists."
  ?tm <- (time-measurement (operation-name ?op-name))
  (not (operation-envelope (name ?op-name)))
 =>
  (retract ?tm)
)

(defrule time-measurement-live-update
  "Updates time measurements if there is a running span."
  ; Do not update if there is no valid span and also do not reset the
  ; time-measurement. This keeps the latest time-measurement for finished
  ; operations.
  (operation-envelope (name ?op-name) (span-reference-id ?span-id&~0))
  (time (timestamp $?now))
  ?tm <- (time-measurement (operation-name ?op-name)
           (last-updated $?last-updated&:(not (set-eq ?last-updated ?now))))
 =>
  (bind ?dur-sec
    (span-calculate-execution-duration ?span-id "Executive Run"
                                       ?*TRACING-TREE-START-EVENT*))
  (modify ?tm (last-updated ?now)
              (start-time (span-get-start-time ?span-id "Executive Run"))
              (duration ?dur-sec))
  (run-metadata-proto-update-execution-time ?dur-sec ?op-name)

  (bind ?topic (str-cat "/executive/operations/" ?op-name "/execution_duration"))
  (bind ?duration-values (seconds-to-duration ?dur-sec))
  (bind ?dur-proto (pb-create "google.protobuf.Duration"))
  (pb-set-field ?dur-proto "seconds" (nth$ 1 ?duration-values))
  (pb-set-field ?dur-proto "nanos" (nth$ 2 ?duration-values))
  (pubsub-publish ?topic ?dur-proto)
  (pb-remove ?dur-proto)
)
