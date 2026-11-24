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

; Processing rules for subtree conditions of behavior trees.

; Conditions may again be another behavior tree.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-condition-subtree-start
  "Start sub-tree condition when conditions are to be evaluated"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (node-id ?node-id) (tree-id ?tree-id)
                                  (type SUB-TREE) (state START)
                                  (sub-tree-id ?sub-tree-id)
                                  (span-reference-id ?parent-span-id))
  ?sub-tree <- (behavior-tree (id ?sub-tree-id) (state ACCEPTED))
 =>
  (bind ?sub-tree-span
    (tracing-cc-start-tree-span ?sub-tree-id ?parent-span-id))
  (modify ?sub-tree (state RUNNING) (span-reference-id ?sub-tree-span))
  (modify ?cf (state EVALUATING))
)

(defrule behavior-tree-condition-subtree-succeeded
  "The subtree condition became satisfied"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type SUB-TREE) (state EVALUATING|SUSPENDING)
                                  (sub-tree-id ?sub-tree-id))
  (behavior-tree (id ?sub-tree-id) (state SUCCEEDED))
 =>
  (modify ?cf (satisfied TRUE) (state FINISHED))
)

(defrule behavior-tree-condition-subtree-failed
  "The subtree condition became unsatisfied"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type SUB-TREE) (state EVALUATING|SUSPENDING)
                                  (sub-tree-id ?sub-tree-id))
  (behavior-tree (id ?sub-tree-id) (state FAILED))
 =>
  (modify ?cf (satisfied FALSE) (state FINISHED))
)

(defrule behavior-tree-condition-subtree-suspend
  "Suspend sub-tree if containing tree is suspending."
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?cf <- (behavior-tree-condition (type SUB-TREE) (tree-id ?tree-id)
                                  (state EVALUATING)
                                  (sub-tree-id ?sub-tree-id))
  (behavior-tree (id ?sub-tree-id) (state RUNNING))
 =>
  (if (behavior-tree-suspend ?sub-tree-id) then
    (modify ?cf (state SUSPENDING))
  )
)

(defrule behavior-tree-condition-subtree-suspended
  "Sub-tree has been suspended."
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?cf <- (behavior-tree-condition (type SUB-TREE) (tree-id ?tree-id)
                                  (state SUSPENDING)
                                  (sub-tree-id ?sub-tree-id))
  (behavior-tree (id ?sub-tree-id) (state SUSPENDED))
 =>
  (modify ?cf (state SUSPENDED))
)

(defrule behavior-tree-condition-subtree-resume
  "Suspend sub-tree if containing tree is suspending."
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree (id ?tree-id) (state RUNNING))
  ?cf <- (behavior-tree-condition (type SUB-TREE) (tree-id ?tree-id)
                                  (state SUSPENDED)
                                  (sub-tree-id ?sub-tree-id))
  ?sub-tree <- (behavior-tree (id ?sub-tree-id) (state SUSPENDED))
 =>
  (modify ?sub-tree (state RUNNING))
  (modify ?cf (state EVALUATING))
)

(defrule behavior-tree-condition-subtree-cancel
  "Cancel sub-tree when condition canceling"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree-condition (type SUB-TREE) (tree-id ?tree-id)
                           (state CANCELING) (sub-tree-id ?sub-tree-id))
  (behavior-tree (id ?sub-tree-id) (state RUNNING|SUSPENDING|SUSPENDED))
 =>
  (behavior-tree-cancel ?sub-tree-id)
)

(defrule behavior-tree-condition-subtree-canceled
  "Canceled when sub-tree has finished or wasn't started, yet"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type SUB-TREE) (tree-id ?tree-id)
                                  (state CANCELING) (sub-tree-id ?sub-tree-id))
  (behavior-tree (id ?sub-tree-id) (state ACCEPTED|CANCELED|SUCCEEDED|FAILED))
 =>
  (modify ?cf (state CANCELED))
)
