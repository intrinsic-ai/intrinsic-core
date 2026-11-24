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

; Skill meta info in CLIPS

; --------------------------------- TEMPLATES ---------------------------------

; This carries meta info about available skills.
; skill-info facts are managed by the SkillClientGenerator.
; They should be considered read-only during a CLIPS run.
(deftemplate skill-info
  ; The skill-id referring to intrinsic_proto.skills.Skill.id.
  (slot skill-id (type STRING) (default ?NONE))
  ; The operation the skill-info was created with
  (slot operation-name (type STRING))

  (slot parameter-descriptor-pool-id (type INTEGER))
  (slot parameter-message-name (type STRING))
  (slot return-value-descriptor-pool-id (type INTEGER))
  (slot return-value-message-name (type STRING))

  ; Determines, if this skill is executed as a skill or as a parameterizable
  ; behavior tree.
  (slot skill-type (type SYMBOL)
    (allowed-values SKILL BEHAVIOR-TREE) (default SKILL))
)

; This template is used to communicate from the ClipsSkillDispatcher.
(deftemplate skill-status
  ; UID for this action (scoped to the lifetime of the executive)
  (slot action-id (type SYMBOL))
  ; New status of the skill
  ; PROJECTED: projection has succeeded.
  ; CANCELING: cancellation request has been processed and is on-going
  ; CANCELING-EXECUTION-TIMEOUT: execution has timed out and triggered automatic
  ;                              cancellation of the skill's execution.
  ; FAILED: skill has failed to execute.
  ; SUCCEEDED: skill has succeeded execution.
  ; CANCELED: skill has completed requested cancellation.
  (slot status (type SYMBOL) (default UNKNOWN)
        (allowed-values UNKNOWN PROJECTED RUNNING CANCELING
                        CANCELING-EXECUTION-TIMEOUT
                        FAILED SUCCEEDED CANCELED))
  ; An optional error message if the skill failed
  (slot message (type STRING))
  ; The resulting return value, set if the skill produced a return value.
  (slot return-value-proto-id (type INTEGER))

  ; If the skill failed may contain an ExtendedStatus proto with detailed error
  ; information to propagate in the behavior tree.
  (slot extended-status-proto-id (type INTEGER))
)

; ------------------------------ FUNCTIONS  ------------------------------------

; Removes the skill-info with fact address ?skill-info.
(deffunction skill-info-remove (?skill-info)
  (bind ?parameter-descriptor-pool-id (fact-slot-value ?skill-info
    parameter-descriptor-pool-id))
  (bind ?return-value-descriptor-pool-id  (fact-slot-value ?skill-info
    return-value-descriptor-pool-id))
  (if (<> ?parameter-descriptor-pool-id 0) then
    (pb-remove-descriptor-pool ?parameter-descriptor-pool-id)
  )
  (if (<> ?return-value-descriptor-pool-id 0) then
    (pb-remove-descriptor-pool ?return-value-descriptor-pool-id)
  )
  (retract ?skill-info)
)

