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

; Consistency checks for execution tracing.

; Similar to the fact that RUNNING nodes should be children of RUNNING nodes
; tracing spans should be contained within another span. By definition we
; assume that when tracing is enabled there is a valid execution span and use
; this as an indicator.

; This file checks whether those assumptions are satisfied.

; ----------------------------------- RULES -----------------------------------

(defrule tracing-check-execution-span-while-not-running
  "Check for an execution span outside of PREPARING, RUNNING, SUSPENDING,
  SUSPENDED, or CANCELING states."
  (operation-envelope
    (state ?state&~PREPARING&~RUNNING&~SUSPENDING&~SUSPENDED&~CANCELING)
    (span-reference-id ?execution-span&~0))
 =>
  (assert (error (name TRACING-CHECK-EXECUTION-SPAN-WHILE-NOT-RUNNING)
                 (type RECOVERABLE)
                 (message (str-cat "Execution span exists while the "
                   "operation is not PREPARING, RUNNING, SUSPENDING, "
                   "SUSPENDED, or CANCELING (state: " ?state ")."))))
)
