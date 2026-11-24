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

; Behavior call instance conformance checks.
;
; For a new behavior-call-instance, these checks confirm that:
; - UIDs being unique
;
; Requires:
; - behavior_call_instance.clp

; ----------------------------------- RULES -----------------------------------

(defrule behavior-call-instance-check-ambiguous-uids
  "Check there is more than 1 behavior-call-instance with the same uid."
  ?bci1 <- (behavior-call-instance (uid ?bci-uid))
  ?bci2 <- (behavior-call-instance (uid ?bci-uid))
  (test (< (fact-index ?bci1) (fact-index ?bci2)))  ; fire once for 2 facts
  (test (neq ?bci1 ?bci2))
 =>
  (assert
    (error (name BEHAVIOR-CALL-INSTANCE-CHECK-AMBIGUOUS-UIDS) (type RECOVERABLE)
           (data ACTION-UID ?bci-uid)
           (message (str-cat "behavior-call-instance facts " (fact-index ?bci1)
                             " and " (fact-index ?bci2)
                             " have the same uid " ?bci-uid))))
)

(defrule behavior-call-instance-type-behavior-tree-requires-tree
  "Check that a rule of type BEHAVIOR-TREE also has a tree assigned"
  ?bci <- (behavior-call-instance (uid ?uid) (skill-type BEHAVIOR-TREE)
                                  (parameterizable-tree-id nil))
 =>
  (assert
    (error (name BEHAVIOR-CALL-INSTANCE-TYPE-BEHAVIOR-TREE-REQUIRES-TREE)
           (type RECOVERABLE) (data ACTION-UID ?uid)
           (message (str-cat "behavior-call-instance " ?uid
                             " is of type BEHAVIOR-TREE, but doesn't have a
                             tree."))))
)
