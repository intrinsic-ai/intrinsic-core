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

; Code execution templates

; --------------------------------- TEMPLATES ---------------------------------

; This template is used to communicate from the CodeExecutionDispatcher.
(deftemplate code-execution-state-update
  ; Uniquely associates a code-execution-instance to a specific node
  (slot operation-name (type STRING) (default ?NONE))
  (slot tree-id (type SYMBOL) (default ?NONE))
  (slot node-id (type INTEGER) (default ?NONE))

  ; New state of the code execution
  (slot state (type SYMBOL) (default UNKNOWN)
               (allowed-values UNKNOWN RUNNING CANCELING
                               FAILED SUCCEEDED CANCELED))

  ; The resulting return value, if the code execution produced a return value.
  ; This is an Any proto.
  (slot return-value-proto (type INTEGER))

  ; If the code execution failed may contain an ExtendedStatus proto with
  ; detailed error information to propagate in the behavior tree.
  (slot extended-status-proto (type INTEGER))
)

; This template is used to communicate stdout responses from the
; CodeExecutionDispatcher.
(deftemplate code-execution-response-stdout
  ; Uniquely associates a code-execution-instance to a specific node
  (slot operation-name (type STRING) (default ?NONE))
  (slot tree-id (type SYMBOL) (default ?NONE))
  (slot node-id (type INTEGER) (default ?NONE))

  ; Output
  (slot stdout (type STRING))
  (slot sequence (type INTEGER) (default 0))
)

; --------------------------------- FUNCTIONS ---------------------------------

; Removes the code-execution-state-update
;
; This fully removes the code-execution-state-update including its stored
; protos. If these are kept outside the code-execution-state-update, do not call
; this function, but simply retract the fact.
;
; Args:
;   ?su: Fact-address of the code-execution-state-update.
(deffunction code-execution-state-update-remove (?su)
  (pb-remove (fact-slot-value ?su return-value-proto))
  (pb-remove (fact-slot-value ?su extended-status-proto))
  (retract ?su)
)
