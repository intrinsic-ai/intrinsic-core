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

; Processing rules for a node of type FAIL

; The FAIL node is used for directing the control flow of execution by throwing
; a failure. When it is READY, if changes the state to RUNNING, and then
; changes directly to FAILED. Its failure-message is printed out at std output.

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-fail-node-start
  "A FAIL node switches to RUNNING immediately when READY"
  (behavior-tree (state RUNNING) (id ?tree-id))
  ?fail-node <- (behavior-tree-node (type FAIL) (state READY)
                                    (tree-id ?tree-id))
 =>
  (modify ?fail-node (state RUNNING))
)

(defrule behavior-tree-fail-node-running
  "When the FAIL node's state is set to RUNNING, it throws an error with the
   given failure message and its state is set to FAILED.
   This will also fail when the tree is SUSPENDING or CANCELING as it finishes
   instantaneously."
  ?tree <- (behavior-tree (state RUNNING|SUSPENDING|CANCELING) (id ?tree-id))
  ?fail-node <- (behavior-tree-node (type FAIL) (state RUNNING|CANCELING)
                                    (id ?fail-node-id) (tree-id ?tree-id)
                  (on-failure-emit-extended-status-proto-id ?emit-es-proto))
 =>
  (bind ?message "")
  (if (<> ?emit-es-proto 0)
   then (bind ?message (pb-get-field ?emit-es-proto "title")))

  (printout debug "Behavior Tree FAIL node with id " ?fail-node-id
                  " failed with message: '" ?message  "'" crlf)

  (if (eq ?message "") then (bind ?message "Node failed intentionally"))

  (bind ?es-proto 0)
  (if (= ?emit-es-proto 0) then
    ; If there is no emit-es-proto configured generate a generic
    ; ExtendedStatus, so that there is some report of the node failing
    (bind ?es-proto (extended-status-create 31600))
    (extended-status-set-message ?es-proto USER ?message)
    (extended-status-set-related-to ?es-proto ?tree ?fail-node)
  )

  (behavior-tree-set-node-failed ?fail-node EXECUTION ?message
    EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
  (pb-remove ?es-proto)
)
