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

; NoOp Action meta info in CLIPS

; --------------------------------- TEMPLATES ---------------------------------

; This carries the list of actions considered to be a noop action.
; It is loaded by the ClipsSkillDispatcher.
(deftemplate noop-action-info
  ; The names of the corresponding noop actions.
  (multislot noop-action-names (type STRING))
  )


; ----------------------------------- RULES -----------------------------------

(defrule noop-action-conflict-with-skill
  "Check that no action, which has an associated skill,
  is configured as noop action"
  (skill-info (skill-id ?skill-id))
  (noop-action-info (noop-action-names $? ?skill-id $?))
 =>
  (assert (error (type FATAL) (name NOOP-ACTION-CONFLICT-WITH-SKILL)
                 (message (str-cat "Found skill with id " ?skill-id
                 " to be configured as noop action and skill. Remove the"
                 " action name from the list of noop actions or make sure no"
                 " skill with matching name is configured."))))
)
