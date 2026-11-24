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

; Consistency checks for the blackboard

; The blackboard implementation assumes that keys are unique on the blackboard.
; This file checks whether those assumptions are satisfied.
; ----------------------------------- RULES -----------------------------------

(defrule blackboard-check-two-items-with-the-same-key
  "Blackboard Keys must be unique"
  ?b1 <- (blackboard-item (key ?key) (scope ?scope) (operation-name ?op-name))
  ?b2 <- (blackboard-item (key ?key) (scope ?scope) (operation-name ?op-name))
  (test (neq ?b1 ?b2))
  (not (error (name BLACKBOARD-CHECK-TWO-ITEMS-WITH-THE-SAME-KEY)
              (data ?key ?scope)))
 =>
  (assert (error (name BLACKBOARD-CHECK-TWO-ITEMS-WITH-THE-SAME-KEY)
                 (type RECOVERABLE)
                 (data ?key ?scope)
                 (message (str-cat "Blackboard item must be unique, but"
                                   " multiple items with key " ?key
                                   ", scope " ?scope ", and operation name "
                                   ?op-name " exist"))))
)
