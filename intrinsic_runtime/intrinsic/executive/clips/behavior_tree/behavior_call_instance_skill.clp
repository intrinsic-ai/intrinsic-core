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

; Execution rules for behavior-call-instances of type SKILL

; ----------------------------------- RULES -----------------------------------

(defrule behavior-call-instance-skill-unimplemented
  "Skills are still executed via plan-action. This should not exist."
  (declare (salience ?*SALIENCE-HIGH*))
  (behavior-call-instance (uid ?uid) (skill-type SKILL))
  (behavior-tree-node (tree-id ?tree-id) (type TASK)
                      (behavior-call-instance-uid ?uid))
 =>
  (assert (error (name BEHAVIOR-CALL-INSTANCE-SKILL-UNIMPLEMENTED)
                 (type RECOVERABLE) (behavior-tree-id ?tree-id)
                 (message (str-cat "A behavior-call-instance of type SKILL "
                                   "exists in " ?tree-id
                                   ". This is currently not implemented."))))
)
