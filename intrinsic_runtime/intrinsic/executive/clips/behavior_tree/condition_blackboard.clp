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

; Processing rules for blackboard CEL conditions of behavior trees.

; CEL (go/cel) expressions allow to extract, modify, or create data. We are
; using those as conditions (go/intrinsic-behavior-tree-design) on blackboard
; data, that is acquired from skills or inflows into the blackboard
; (go/intrinsic-behavior-tree-data-flow-design).

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-condition-blackboard-cel-evaluate
  "Evaluate blackboard CEL condition when conditions are to be evaluated"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?tree <- (behavior-tree (id ?tree-id) (operation-name ?op-name)
                          (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id))
  ?cf <- (behavior-tree-condition (node-id ?node-id) (tree-id ?tree-id)
                                  (type BLACKBOARD-CEL-EXPRESSION) (state START)
                                  (blackboard-cel-expression ?expression)
                                  (blackboard-cel-expression-id ?expression-id)
                                  (satisfied UNKNOWN))
 =>
  (bind ?eval-result (cel-eval-condition ?expression-id ?bb-scope ?op-name))

  (if (not (result-ok ?eval-result)) then
    (bind ?es-proto (result-error ?eval-result))
    (extended-status-set-related-to ?es-proto ?tree ?node)
    (modify ?cf (state ERROR) (extended-status-proto-id ?es-proto))
    (return)
  )

  (modify ?cf (satisfied (result-value ?eval-result)) (state FINISHED))
)

(defrule behavior-tree-condition-blackboard-canceled
  "Blackboard CEL was canceled (before it started)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (node-id ?node-id) (tree-id ?tree-id)
                                  (type BLACKBOARD-CEL-EXPRESSION)
                                  (state CANCELING))
 =>
  (modify ?cf (state CANCELED))
)
