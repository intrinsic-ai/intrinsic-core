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

; Processing rules for not conditions of behavior tree nodes.

; A "not" condition inverts the truth value of the sub-condition.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-condition-not-start
  ?cf <- (behavior-tree-condition (type NOT) (id ?id) (state START))
  ?sc <- (behavior-tree-condition (parent-id ?id) (state ACCEPTED))
 =>
  (modify ?sc (state START))
  (modify ?cf (state EVALUATING))
)

(defrule behavior-tree-condition-not-satisfied
  "Logical NOT condition became satisified (sub-condition false)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type NOT) (id ?id) (state EVALUATING))
  (behavior-tree-condition (parent-id ?id) (state FINISHED) (satisfied FALSE))
 =>
  (modify ?cf (satisfied TRUE) (state FINISHED))
)

(defrule behavior-tree-condition-not-unsatisfied
  "Logical NOT condition became unsatisfied (sub-condition true)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type NOT) (id ?id) (state EVALUATING))
  (behavior-tree-condition (parent-id ?id) (state FINISHED) (satisfied TRUE))
 =>
  (modify ?cf (satisfied FALSE) (state FINISHED))
)

(defrule behavior-tree-condition-not-error
  "Logical NOT condition errors if any child errors."
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (tree-id ?tree-id) (node-id ?node-id) (id ?id)
                                  (type NOT) (state ~ERROR))
  ?tree <- (behavior-tree (id ?tree-id))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id))
  (behavior-tree-condition (tree-id ?tree-id) (node-id ?node-id)
                           (parent-id ?id) (state ERROR)
                           (extended-status-proto-id ?es-proto))
 =>
  (modify ?cf (state ERROR) (extended-status-proto-id ?es-proto))
)

(defrule behavior-tree-condition-not-suspending
  "Logical NOT condition is suspending (child is suspending)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type NOT) (id ?id) (state EVALUATING))
  (behavior-tree-condition (parent-id ?id) (state SUSPENDING))
 =>
  (modify ?cf (state SUSPENDING))
)

(defrule behavior-tree-condition-not-suspended
  "Logical NOT condition is suspended (child is suspended)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type NOT) (id ?id)
                                  (state EVALUATING|SUSPENDING))
  (behavior-tree-condition (parent-id ?id) (state SUSPENDED))
 =>
  (modify ?cf (state SUSPENDED))
)

(defrule behavior-tree-condition-not-resumed
  "Logical NOT condition has been resumed (child has resumed)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type NOT) (id ?id)
                                  (state SUSPENDING|SUSPENDED))
  (behavior-tree-condition (parent-id ?id) (state EVALUATING))
 =>
  (modify ?cf (state EVALUATING))
)

(defrule behavior-tree-condition-not-cancel
  "Logical NOT condition is to be canceled (child is not yet canceling)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree-condition (type NOT) (id ?id) (state CANCELING))
  ?cf <- (behavior-tree-condition (parent-id ?id) (state ~CANCELING))
 =>
  (modify ?cf (state CANCELING))
)

(defrule behavior-tree-condition-not-canceled
  "Logical NOT condition has being canceled (child has canceled)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type NOT) (id ?id) (state CANCELING))
  (behavior-tree-condition (parent-id ?id) (state CANCELED))
 =>
  (modify ?cf (state CANCELED))
)
