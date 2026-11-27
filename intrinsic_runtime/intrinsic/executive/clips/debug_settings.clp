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

; Runtime debug settings configured via ExecutiveDebug.SetDebugSettings.
; All settings default to off/unset. For executive developer debugging only.

(defglobal
  ?*DEBUG-TEXT-LOGGING-LOOP-NODE-RESET* = FALSE
  ?*DEBUG-EXTRA-TRACES-LOOP-NODE-RESET* = FALSE
  ?*DEBUG-EXTRA-TRACES-STRUCTURED-LOGGING* = FALSE
  ?*DEBUG-EXTRA-TRACES-EMBED-SKILL-TRACES* = UNSET
)

; Pending post-reset cleanup asserted only when a loop reset opens a full-reset
; span.
(deftemplate debug-loop-reset-pending
  (slot full-span-id (type INTEGER) (default 0))
)

; Starts the optional "Loop node reset (full)" and "Loop node reset" debug
; spans attached to ?loop-node-span. Returns (create$ ?full-reset-span ?reset-span).
(deffunction debug-loop-reset-start-spans (?loop-node-span)
  (bind ?full-reset-span ?*TRACING-INVALID-SPAN-ID*)
  (bind ?reset-span ?*TRACING-INVALID-SPAN-ID*)
  (if (and (eq ?*DEBUG-EXTRA-TRACES-LOOP-NODE-RESET* TRUE)
           (<> ?loop-node-span ?*TRACING-INVALID-SPAN-ID*)) then
    (bind ?full-reset-span
      (span-start "Loop node reset (full)" ?loop-node-span
                  "loop-node-reset-full"))
    (bind ?reset-span
      (span-start "Loop node reset" ?loop-node-span "loop-node-reset"))
  )
  (return (create$ ?full-reset-span ?reset-span))
)

; Ends the direct "Loop node reset" span and schedules ?full-reset-span to be
; ended by the salience-1 rule debug-loop-reset-finish once all higher-salience
; post-reset rules (notably SALIENCE-HIGHER state_proto_update rules) have
; finished and before the next salience-0 loop rule runs.
(deffunction debug-loop-reset-end-spans (?spans)
  (bind ?full-reset-span (nth$ 1 ?spans))
  (bind ?reset-span (nth$ 2 ?spans))
  (span-end ?reset-span)
  (if (<> ?full-reset-span ?*TRACING-INVALID-SPAN-ID*) then
    (assert (debug-loop-reset-pending (full-span-id ?full-reset-span)))
  )
)

; Fires after all post-reset higher-salience rules (> 0, notably
; SALIENCE-HIGHER state_proto_update rules) and immediately before the next
; salience-0 loop rule starts the child or finishes the loop.
(defrule debug-loop-reset-finish
  (declare (salience 1))
  ?pending <- (debug-loop-reset-pending (full-span-id ?full-span-id))
 =>
  (retract ?pending)
  (span-end ?full-span-id)
)
