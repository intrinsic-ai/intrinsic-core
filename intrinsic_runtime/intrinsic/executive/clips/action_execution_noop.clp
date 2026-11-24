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

(defrule action-execution-noop
  "Makes action succeed if configured as NoOp action."
  (behavior-tree (plan-id ?plan-id) (state RUNNING|CANCELING))
  (world (id ?world-id))
  ?af <- (plan-action (plan-id ?plan-id) (id ?id) (skill-id ?action-skill-id)
                      (state SELECTED|READY) (executable TRUE)
                      (log-id ~0) (span-reference-id ?trace-span-id))
  (noop-action-info (noop-action-names $? ?action-skill-id $?))
 =>
  (printout debug "Treating action '" (plan-action-tostring ?plan-id ?id)
                  "' as a no-op." crlf)
  (bind ?now (now))
  (modify ?af (state EXECUTION-SUCCEEDED)
          (execution-start-time ?now)
          (execution-end-time ?now)
          (world-id-execution-after
            (world-clone ?world-id "action_noop" ?trace-span-id)))
)
