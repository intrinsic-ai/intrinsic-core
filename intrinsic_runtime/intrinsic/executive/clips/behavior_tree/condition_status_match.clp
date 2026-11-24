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

; Processing rules for status match conditions of behavior trees.

; This uses an ExtendedStatus proto at a given blackboard key to look for a
; user-defined match, cf. go/intrinsic-behavior-tree-extended-status-design.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-condition-status-match-evaluate
  "Evaluate status match condition when conditions are to be evaluated"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree (id ?tree-id) (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?cf <- (behavior-tree-condition (node-id ?node-id) (tree-id ?tree-id)
                                  (type EXTENDED-STATUS-MATCH) (state START)
                                  (extended-status-match-blackboard-key ?bb-key)
                                  (extended-status-match-component ?match-comp)
                                  (extended-status-match-code ?match-code)
                                  (satisfied UNKNOWN))
  (blackboard-item (key ?bb-key) (scope ?bb-scope) (operation-name ?op-name)
                   (proto-id ?es-proto))
 =>
  (bind ?result TRUE)
  (if (and (neq ?match-comp "")
           (neq ?match-comp (pb-get-field ?es-proto "status_code.component")))
   then (bind ?result FALSE))
  (if (and (neq ?match-code 0)
           (neq ?match-code (pb-get-field ?es-proto "status_code.code")))
   then (bind ?result FALSE))

  (modify ?cf (satisfied ?result) (state FINISHED))
)

(defrule behavior-tree-condition-status-match-evaluate-no-value
  "Evaluate status match condition when conditions are to be evaluated"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  (behavior-tree (id ?tree-id) (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?cf <- (behavior-tree-condition (id ?id) (node-id ?node-id) (tree-id ?tree-id)
                                  (type EXTENDED-STATUS-MATCH) (state START)
                                  (extended-status-match-blackboard-key ?bb-key)
                                  (satisfied UNKNOWN))
  (not (blackboard-item (key ?bb-key) (scope ?bb-scope)
                        (operation-name ?op-name)))
 =>
  (printout warn "No matching blackboard item " ?op-name ":" ?bb-scope ":"
            ?bb-key " for condition " ?tree-id ":" ?node-id " (" ?id ")" crlf)
  (modify ?cf (satisfied FALSE) (state FINISHED))
)

(defrule behavior-tree-condition-status-match-canceled
  "Blackboard CEL was canceled (before it started)"
  (declare (salience ?*SALIENCE-BEHAVIOR-TREE-CONDITION*))
  ?cf <- (behavior-tree-condition (node-id ?node-id) (tree-id ?tree-id)
                                  (type EXTENDED-STATUS-MATCH)
                                  (state CANCELING))
 =>
  (modify ?cf (state CANCELED))
)
