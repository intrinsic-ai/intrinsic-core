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

; Reset skill instance IDs after a skill instance is used
;
; Requires:
; - operation.clp: operation-envelope template
; - ClipsSkillDispatcher: Contains the function definition for
;                         skill-reset-instance-ids

; ----------------------------------- RULES -----------------------------------

; Reset skill instance IDs once a behavior tree has been executed.
(defrule skill-reset-instance-ids-on-bt-completion
  ?op <- (operation-envelope (operation-tree-id ?tree-id)
                             (state ?state&SUCCEEDED|FAILED|CANCELED)
                             (skill-instances-are-reset FALSE))
 =>
  ; TODO(b/319835803): This would reset *all* instance ids when one operation is
  ; finished, but another is still running.
  (bind ?instance-id-reset-result (skill-reset-instance-ids))
  (bind ?instance-id-reset-ok (nth$ 1 ?instance-id-reset-result))
  (bind ?instance-id-reset-error (nth$ 2 ?instance-id-reset-result))
  (if (not ?instance-id-reset-ok)
    then
      (printout error "Unable to reset skill instance IDs after executing "
                      "behavior tree with ID '" ?tree-id "' due to the "
                      "following error: " ?instance-id-reset-error crlf)
  )
  (modify ?op (skill-instances-are-reset TRUE))
)
