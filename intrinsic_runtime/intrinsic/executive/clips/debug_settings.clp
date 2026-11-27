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

; Runtime debug settings and developer experiments.
; All settings and experiments default to off/unset. For executive developer
; debugging only.

; =============================================================================
; SETUP: Debug Settings & Experiments
; =============================================================================
;
; Adding an experiment:
;   1. Add an uppercase symbol constant (e.g. EXP_MY_EXPERIMENT) to
;      ?*DEBUG-EXPERIMENTS* below.
;   2. Guard experimental code with:
;        (if (debug-experiment-active EXP_MY_EXPERIMENT) then ...)
;   3. Activate it explicitly via DebugSettings.enabled_experiments or from
;      anywhere via:
;        (debug-experiment-enable EXP_MY_EXPERIMENT)
;        (debug-experiment-disable EXP_MY_EXPERIMENT)

(defglobal
  ; Runtime settings configured via ExecutiveDebug.SetDebugSettings.
  ?*DEBUG-TEXT-LOGGING-LOOP-NODE-RESET* = FALSE
  ?*DEBUG-EXTRA-TRACES-LOOP-NODE-RESET* = FALSE
  ?*DEBUG-EXTRA-TRACES-STRUCTURED-LOGGING* = FALSE
  ?*DEBUG-EXTRA-TRACES-EMBED-SKILL-TRACES* = UNSET

  ; Defined debug experiment constants.
  ?*DEBUG-EXPERIMENTS* = (create$
    ; Skip writing RunMetadata state proto fields while still marking updates
    ; done in CLIPS facts so state_proto_update rules run normally.
    EXP_LOOP_RESET_SKIP_STATE_PROTO_UPDATE
    ; Skip building/recording/publishing executive OperationEvents.
    EXP_LOOP_RESET_SKIP_OPERATION_EVENTS
  )
)

; =============================================================================
; IMPLEMENTATION: Internal State & Helpers
; =============================================================================

(defglobal
  ; Set of currently active experiment symbols.
  ?*DEBUG-ACTIVE-EXPERIMENTS* = (create$)

  ; Internal counters populated during a loop node reset when
  ; ?*DEBUG-TEXT-LOGGING-LOOP-NODE-RESET* is TRUE.
  ?*DEBUG-LOOP-RESET-ACTIVE* = FALSE
  ?*DEBUG-LOOP-RESET-NODES* = 0
  ?*DEBUG-LOOP-RESET-NODES-SKIPPED* = 0
  ?*DEBUG-LOOP-RESET-TASK-NODES* = 0
  ?*DEBUG-LOOP-RESET-TREES* = 0
  ?*DEBUG-LOOP-RESET-TREES-SKIPPED* = 0
  ?*DEBUG-LOOP-RESET-CONDITIONS* = 0
  ?*DEBUG-LOOP-RESET-PLAN-ACTIONS* = 0
  ?*DEBUG-LOOP-RESET-BEHAVIOR-CALL-INSTANCES* = 0
  ?*DEBUG-LOOP-RESET-CODE-EXECUTION-INSTANCES* = 0
)

; --- Loop node reset debug statistics helpers ---

(deffunction debug-loop-reset-inc-nodes ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-NODES* (+ ?*DEBUG-LOOP-RESET-NODES* 1))
  )
)

(deffunction debug-loop-reset-inc-nodes-skipped ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-NODES-SKIPPED*
      (+ ?*DEBUG-LOOP-RESET-NODES-SKIPPED* 1))
  )
)

(deffunction debug-loop-reset-inc-task-nodes ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-TASK-NODES* (+ ?*DEBUG-LOOP-RESET-TASK-NODES* 1))
  )
)

(deffunction debug-loop-reset-inc-trees ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-TREES* (+ ?*DEBUG-LOOP-RESET-TREES* 1))
  )
)

(deffunction debug-loop-reset-inc-trees-skipped ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-TREES-SKIPPED*
      (+ ?*DEBUG-LOOP-RESET-TREES-SKIPPED* 1))
  )
)

(deffunction debug-loop-reset-inc-conditions ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-CONDITIONS* (+ ?*DEBUG-LOOP-RESET-CONDITIONS* 1))
  )
)

(deffunction debug-loop-reset-inc-plan-actions ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-PLAN-ACTIONS*
      (+ ?*DEBUG-LOOP-RESET-PLAN-ACTIONS* 1))
  )
)

(deffunction debug-loop-reset-inc-behavior-call-instances ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-BEHAVIOR-CALL-INSTANCES*
      (+ ?*DEBUG-LOOP-RESET-BEHAVIOR-CALL-INSTANCES* 1))
  )
)

(deffunction debug-loop-reset-inc-code-execution-instances ()
  (if ?*DEBUG-LOOP-RESET-ACTIVE* then
    (bind ?*DEBUG-LOOP-RESET-CODE-EXECUTION-INSTANCES*
      (+ ?*DEBUG-LOOP-RESET-CODE-EXECUTION-INSTANCES* 1))
  )
)

; Resets loop-reset debug counters and starts timing when
; ?*DEBUG-TEXT-LOGGING-LOOP-NODE-RESET* is TRUE. Returns (now) when active, or
; an empty multifield (create$) when disabled.
(deffunction debug-loop-reset-stats-start ()
  (if (neq ?*DEBUG-TEXT-LOGGING-LOOP-NODE-RESET* TRUE) then
    (return (create$))
  )
  (bind ?*DEBUG-LOOP-RESET-NODES* 0)
  (bind ?*DEBUG-LOOP-RESET-NODES-SKIPPED* 0)
  (bind ?*DEBUG-LOOP-RESET-TASK-NODES* 0)
  (bind ?*DEBUG-LOOP-RESET-TREES* 0)
  (bind ?*DEBUG-LOOP-RESET-TREES-SKIPPED* 0)
  (bind ?*DEBUG-LOOP-RESET-CONDITIONS* 0)
  (bind ?*DEBUG-LOOP-RESET-PLAN-ACTIONS* 0)
  (bind ?*DEBUG-LOOP-RESET-BEHAVIOR-CALL-INSTANCES* 0)
  (bind ?*DEBUG-LOOP-RESET-CODE-EXECUTION-INSTANCES* 0)
  (bind ?*DEBUG-LOOP-RESET-ACTIVE* TRUE)
  (return (now))
)

; Stops loop-reset statistics collection and prints the summary line.
(deffunction debug-loop-reset-stats-end
    (?loop-node ?start-time ?total-bt-nodes ?experiment-label)
  (bind ?*DEBUG-LOOP-RESET-ACTIVE* FALSE)
  (bind ?duration-ms (* (time-diff-sec (now) ?start-time) 1000.0))
  (bind ?experiment-suffix "")
  (if (neq ?experiment-label "") then
    (bind ?experiment-suffix (str-cat " | experiment=" ?experiment-label))
  )
  (printout t (format nil
    "Loop node reset (%s:%d, loop_num_times=%d): %.3f ms | nodes=%d (skipped=%d, task=%d, total_in_env=%d) | trees=%d (skipped=%d) | conditions=%d | plan_actions=%d | behavior_calls=%d | code_executions=%d%s%n"
    (fact-slot-value ?loop-node tree-id)
    (fact-slot-value ?loop-node id)
    (fact-slot-value ?loop-node loop-num-times)
    ?duration-ms
    ?*DEBUG-LOOP-RESET-NODES*
    ?*DEBUG-LOOP-RESET-NODES-SKIPPED*
    ?*DEBUG-LOOP-RESET-TASK-NODES*
    ?total-bt-nodes
    ?*DEBUG-LOOP-RESET-TREES*
    ?*DEBUG-LOOP-RESET-TREES-SKIPPED*
    ?*DEBUG-LOOP-RESET-CONDITIONS*
    ?*DEBUG-LOOP-RESET-PLAN-ACTIONS*
    ?*DEBUG-LOOP-RESET-BEHAVIOR-CALL-INSTANCES*
    ?*DEBUG-LOOP-RESET-CODE-EXECUTION-INSTANCES*
    ?experiment-suffix))
)

; --- General experiment activation (usable from anywhere) ---

(deffunction debug-experiment-enable (?experiment)
  (if (eq ?experiment NONE) then
    (return TRUE)
  )
  (if (eq (member$ ?experiment ?*DEBUG-EXPERIMENTS*) FALSE) then
    (printout error "Unknown debug experiment: " ?experiment crlf)
    (return FALSE)
  )
  (if (eq (member$ ?experiment ?*DEBUG-ACTIVE-EXPERIMENTS*) FALSE) then
    (bind ?*DEBUG-ACTIVE-EXPERIMENTS*
      (create$ ?*DEBUG-ACTIVE-EXPERIMENTS* ?experiment))
  )
  (return TRUE)
)

(deffunction debug-experiment-disable (?experiment)
  (bind ?idx (member$ ?experiment ?*DEBUG-ACTIVE-EXPERIMENTS*))
  (if (neq ?idx FALSE) then
    (bind ?*DEBUG-ACTIVE-EXPERIMENTS*
      (delete$ ?*DEBUG-ACTIVE-EXPERIMENTS* ?idx ?idx))
  )
)

(deffunction debug-experiments-clear ()
  (if (> (length$ ?*DEBUG-ACTIVE-EXPERIMENTS*) 0) then
    (bind ?*DEBUG-ACTIVE-EXPERIMENTS* (create$))
  )
)

(deffunction debug-experiment-active (?experiment)
  (neq (member$ ?experiment ?*DEBUG-ACTIVE-EXPERIMENTS*) FALSE)
)

; Format the active experiment label for loop-reset spans and logs, or "" if
; no experiment is active.
(deffunction debug-active-experiments-label ()
  (if (> (length$ ?*DEBUG-ACTIVE-EXPERIMENTS*) 0) then
    (return (implode$ ?*DEBUG-ACTIVE-EXPERIMENTS*))
  )
  (return "")
)
; Pending post-reset cleanup asserted only when a loop reset opens a full-reset
; span.
(deftemplate debug-loop-reset-pending
  (slot full-span-id (type INTEGER) (default 0))
)

; Starts the optional "Loop node reset (full)" and "Loop node reset" debug
; spans attached to ?loop-node-span, tagging them with ?experiment-label when
; non-empty. Returns (create$ ?full-reset-span ?reset-span).
(deffunction debug-loop-reset-start-spans (?loop-node-span ?experiment-label)
  (bind ?full-reset-span ?*TRACING-INVALID-SPAN-ID*)
  (bind ?reset-span ?*TRACING-INVALID-SPAN-ID*)
  (if (and (eq ?*DEBUG-EXTRA-TRACES-LOOP-NODE-RESET* TRUE)
           (<> ?loop-node-span ?*TRACING-INVALID-SPAN-ID*)) then
    (bind ?full-reset-span
      (span-start "Loop node reset (full)" ?loop-node-span
                  "loop-node-reset-full"))
    (if (and (neq ?experiment-label "")
             (<> ?full-reset-span ?*TRACING-INVALID-SPAN-ID*)) then
      (span-add-attribute ?full-reset-span "loop_reset_experiment"
                          ?experiment-label)
    )
    (bind ?reset-span
      (span-start "Loop node reset" ?loop-node-span "loop-node-reset"))
    (if (and (neq ?experiment-label "")
             (<> ?reset-span ?*TRACING-INVALID-SPAN-ID*)) then
      (span-add-attribute ?reset-span "loop_reset_experiment"
                          ?experiment-label)
    )
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
