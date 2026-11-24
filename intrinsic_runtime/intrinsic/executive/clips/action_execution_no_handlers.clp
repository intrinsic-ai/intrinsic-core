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

; Action execution for actions not handled otherwise

; NOTE: this action executor skips projection.

; ----------------------------------- RULES -----------------------------------

(defrule action-execution-no-handlers
  "Catch all for selected actions not handled by other executors, fails them if
   no matching handler can be found."
  ; Low salience to not accidentally preempt any other action executor
  (declare (salience ?*SALIENCE-LOW*))
  (world (id ?world-id))
  (behavior-tree (plan-id ?plan-id) (state RUNNING|CANCELING)
                 (log-id ?bt-log-id&~0))
  ?af <- (plan-action (plan-id ?plan-id) (id ?id)
                      (state SELECTED|READY) (executable TRUE)
                      (log-id ~0) (span-reference-id ?trace-span-id))
 =>
  (assert (error (type RECOVERABLE) (name ACTION-EXECUTION-NO-HANDLERS)
      (message (str-cat "Failing action " (plan-action-tostring ?plan-id ?id)
      " because no handler was configured for this action."))))
  (bind ?now (now))
  (modify ?af (state EXECUTION-FAILED)
          (execution-start-time ?now)
          (execution-end-time ?now)
          (world-id-execution-after
            (world-clone ?world-id "action_no_handlers" ?trace-span-id)))
)
