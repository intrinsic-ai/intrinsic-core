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

; This file centralizes error handling

; --------------------------------- TEMPLATES ---------------------------------

; An error represents an unexpected situation.
(deftemplate error
  ; The name identifying the problem, either for easy identification for
  ; post-mortem analysis (FATAL), or to ease automated analysis and recovery
  ; (RECOVERABLE). A good rule of thumb is to use the name of the rule asserting
  ; the error.
  (slot name (type SYMBOL) (default ?NONE))

  ; Informative message for human operator
  (slot message (type STRING))
  (multislot time (type INTEGER) (cardinality 2 2) (default-dynamic (now)))

  ; We distinguish two error types:
  ; RECOVERABLE: it is an error the executive can recover from, e.g., by
  ;              executing a different behavior tree, display the error to
  ;              the user.
  ; FATAL: this is a critical problem, basic assumptions or expectations are
  ;        violated, the executive must be restarted to recover and the problem
  ;        investigated.
  (slot type (type SYMBOL) (allowed-values RECOVERABLE FATAL))

  ; Set to TRUE for errors not to be shown un the logs unless running on
  ; privileged (internal) systems.
  (slot privileged (type SYMBOL) (allowed-values FALSE TRUE))

  ; If true, when this error is reported, also trigger a detailed report of the
  ; CLIPS factbase or traces. This is usually set when the error indicates an
  ; inconsistency and is only a symptom of the underlying issue without a known
  ; root-cause.
  (slot trigger-full-report (type SYMBOL) (allowed-values FALSE TRUE))

  ; Arbitrary data useful in relation to the error name. A typical example is
  ; to use this to avoid reporting an error multiple types in case of
  ; symmetries, e.g., asserting the two pieces of data involved, have the error
  ; emitting rule check for the other order.
  (multislot data)

  ; Mark behavior tree an error is related to, if any
  (slot behavior-tree-id (type SYMBOL))
)

; --------------------------------- FUNCTIONS ---------------------------------

; Retract all errors from the fact base.
(deffunction errors-retract-all ()
  (delayed-do-for-all-facts ((?e error)) TRUE (retract ?e))
)

(deffunction errors-retract-recoverable ()
  (delayed-do-for-all-facts ((?e error)) (eq ?e:type RECOVERABLE) (retract ?e))
)

; ----------------------------------- RULES -----------------------------------

(defrule error-print-recoverable
  "Print errors as they occur"
  ; Always immediately print errors as they occur
  (declare (salience ?*SALIENCE-FIRST*))
  (error (name ?name) (message ?message) (type RECOVERABLE) (privileged ?priv))
 =>
  (bind ?channel (if ?priv then internal else warn))
  (printout ?channel "Recoverable error (" ?name "): " ?message crlf)
)

(defrule error-print-fatal
  "Print errors as they occur"
  ; Always immediately print errors as they occur
  (declare (salience ?*SALIENCE-FIRST*))
  (error (name ?name) (message ?message) (type FATAL) (privileged ?priv))
 =>
  (bind ?channel (if ?priv then internal else error))
  (printout ?channel "GURU MEDITATION (" ?name "): " ?message crlf)
)
