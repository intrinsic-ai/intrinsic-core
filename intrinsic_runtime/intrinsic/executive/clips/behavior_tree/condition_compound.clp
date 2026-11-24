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

; Processing rules for compound conditions of behavior tree nodes.

; Compound conditions are conjunction ("and") and disjunction ("or") of other
; conditions.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-condition-compound-start
  ?cf <- (behavior-tree-condition (type AND|OR) (id ?id) (state START))
 =>
  (modify ?cf (state EVALUATING))
)

(defrule behavior-tree-condition-compound-and-select-condition
  "Select the next condition in an and compound condition"
  ?cf <- (behavior-tree-condition (type AND) (id ?id) (state EVALUATING))
  ?sc <- (behavior-tree-condition (id ?condition-id) (parent-id ?id)
                                  (state ACCEPTED)
                                  (compound-condition-index ?cid))
  (not (exists (behavior-tree-condition (parent-id ?id) (satisfied FALSE))))
  ; Verify that all previous conditions have been evaluated. If a previous
  ; condition is not satisfied the compound will never be satisfied so there is
  ; no need to start the current condition, this is just an optimization.
  (not (behavior-tree-condition (compound-condition-index
                                 ?other-id&:(< ?other-id ?cid))
                                (parent-id ?id)
                                (satisfied FALSE|UNKNOWN)))
 =>
  (modify ?sc (state START))
)

(defrule behavior-tree-condition-compound-or-select-condition
  "Select the next condition in an or compound condition"
  ?cf <- (behavior-tree-condition (type OR) (id ?id) (state EVALUATING))
  ?sc <- (behavior-tree-condition (id ?condition-id) (parent-id ?id)
                                  (state ACCEPTED)
                                  (compound-condition-index ?cid))
  (not (behavior-tree-condition (parent-id ?id) (satisfied TRUE)))
  ; Verify that all previous conditions have been evaluated. If a previous
  ; condition is satisfied the compound will always be satisfied so there is
  ; no need to start the current condition, this is just an optimization.
  (not (behavior-tree-condition (compound-condition-index
                                 ?other-id&:(< ?other-id ?cid))
                                (parent-id ?id)
                                (satisfied TRUE|UNKNOWN)))
 =>
  (modify ?sc (state START))
)

(defrule behavior-tree-condition-AND-satisfied
  "Logical AND condition became satisified (all children are satisfied)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-COND-COMPOUND*))
  ?cf <- (behavior-tree-condition (type AND) (id ?id) (state EVALUATING))

  ; All child conditions are finished
  (not (exists (behavior-tree-condition (parent-id ?id) (state ~FINISHED))))

  ; There is no unsatisfied child sub-condition, i.e., all sub-conditions
  ; are satisfied, or there are none.
  (not (exists (behavior-tree-condition (parent-id ?id)
                                        (satisfied UNKNOWN|FALSE))))
 =>
  (modify ?cf (satisfied TRUE) (state FINISHED))
)

(defrule behavior-tree-condition-AND-unsatisfied
  "Logical AND condition became unsatisified (any child is unsatisfied)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-COND-COMPOUND*))
  ?cf <- (behavior-tree-condition (type AND) (id ?id) (state EVALUATING))

  (exists (behavior-tree-condition (parent-id ?id) (satisfied FALSE)))
 =>
  (modify ?cf (satisfied FALSE) (state FINISHED))
)

(defrule behavior-tree-condition-OR-satisfied
  "Logical OR condition became satisified"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-COND-COMPOUND*))
  ?cf <- (behavior-tree-condition (type OR) (id ?id) (state EVALUATING))

  (exists (behavior-tree-condition (parent-id ?id) (satisfied TRUE)))
 =>
  (modify ?cf (satisfied TRUE) (state FINISHED))
)

(defrule behavior-tree-condition-OR-unsatisfied
  "Logical OR condition became unsatisified"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-COND-COMPOUND*))
  ?cf <- (behavior-tree-condition (type OR) (id ?id) (state EVALUATING))

  ; All child conditions are finished
  (not (exists (behavior-tree-condition (parent-id ?id) (state ~FINISHED))))

  (not (exists (behavior-tree-condition (parent-id ?id)
                                        (satisfied UNKNOWN|TRUE))))
 =>
  (modify ?cf (satisfied FALSE) (state FINISHED))
)

(defrule behavior-tree-condition-compound-error
  "Logical compound condition errors if any child errors."
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (tree-id ?tree-id) (node-id ?node-id) (id ?id)
                                  (type AND|OR) (state ~ERROR))
  ?tree <- (behavior-tree (id ?tree-id))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id))
  ; Since we execute conditions strictly in order we will exit after
  ; encountering the first error, hence no need for intermediate extended
  ; statuses.
  (behavior-tree-condition (tree-id ?tree-id) (node-id ?node-id)
                           (parent-id ?id) (state ERROR)
                           (extended-status-proto-id ?es-proto))
 =>
  (modify ?cf (state ERROR) (extended-status-proto-id ?es-proto))
)

(defrule behavior-tree-condition-compound-suspending
  "Logical compound condition is suspending (some child is suspending)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type AND|OR) (id ?id) (state EVALUATING))
  (exists (behavior-tree-condition (parent-id ?id) (state SUSPENDING)))
 =>
  (modify ?cf (state SUSPENDING))
)

(defrule behavior-tree-condition-compound-suspended
  "Logical compound condition is suspended (all children are suspended)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type AND|OR) (id ?id) (state SUSPENDING))
  ; At least one is suspended (to avoid that the forall matches simply because
  ; all children have finished.
  (exists (behavior-tree-condition (parent-id ?id) (state SUSPENDED)))
  ; All are suspended or have finished
  (forall (behavior-tree-condition (id ?child-id) (parent-id ?id))
    (behavior-tree-condition (id ?child-id) (state SUSPENDED|FINISHED)))
 =>
  (modify ?cf (state SUSPENDED))
)

(defrule behavior-tree-condition-compound-resumed
  "Logical compound condition has been resumed (some child has resumed)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type AND|OR) (id ?id) (state SUSPENDED))
  (not (behavior-tree-condition (parent-id ?id) (state SUSPENDED)))
 =>
  (modify ?cf (state EVALUATING))
)

(defrule behavior-tree-condition-compound-cancel
  "Logical compound condition is to be canceled (children not yet canceling)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree-condition (type AND|OR) (id ?id) (state CANCELING))
  ?cf <- (behavior-tree-condition (parent-id ?id) (state ~CANCELING&~CANCELED))
 =>
  (modify ?cf (state CANCELING))
)

(defrule behavior-tree-condition-compound-canceled
  "Logical compound condition has being canceled (child have canceled)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (type AND|OR) (id ?id) (state CANCELING))
  (not (behavior-tree-condition (parent-id ?id) (state ~CANCELED)))
 =>
  (modify ?cf (state CANCELED))
)
