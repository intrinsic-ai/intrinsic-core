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

; Processing rules for a node of type LOOP.
; The LOOP node can represent while and for-each and counting loops, depending
; on the loop-mode (WHILE/FOR-EACH) and the values set for while (condition) and
; loop-max-times, or loop-for-each-*.
;
; After generic decoration condition checking a loop node is READY and
; transitions through its iterations as follows:
; (transitions marked with  -> T are terminal)
;
; Counting loop
; (COUNT with no loop-while-id condition and optional max-times):
; -start-count(child: ACCEPTED): READY->RUNNING, child: SELECTED
; -do-failed(child: FAILED): -> FAILED                                     -> T
; -do-succeeded(child: SUCCEEDED):
;   num-times < max_times: -> READY, child: ACCEPTED
; -start-count(child: ACCEPTED): READY->RUNNING, child: SELECTED
; -do-succeeded(child: SUCCEEDED):
;   num-times >= max_times: -> SUCCEEDED                                   -> T
;   num-times < max_times: -> READY, child: ACCEPTED
; ...
;
; While loop
; (WHILE with loop-while-id condition and optional max-times):
; -start: READY -> RUNNING, condition: START
; -condition-unsatisfied(child: *): -> SUCCEEDED, condition: ACCEPTED      -> T
; -condition-satisfied(child: ACCEPTED): child: SELECTED, condition: ACCEPTED
; -do-fail(child: FAILED): -> FAILED                                       -> T
; -do-succeed(child: SUCCEEDED):
;   num-times < max_times: -> READY, child: SUCCEEDED
; -start: READY -> RUNNING, condition: START
; -reset-before-cont'-satisfied-while(child: SUCCEEDED, condition:SATISFIED):
;   child: ACCEPTED
; -condition-satisfied(child: ACCEPTED): child: SELECTED, condition: ACCEPTED
; -do-succeed(child: SUCCEEDED):
;   num-times < max_times: -> READY, child: SUCCEEDED
; -start: READY -> RUNNING, condition: START
; -reset-before-cont'-satisfied-while(child: SUCCEEDED, condition:SATISFIED):
;   child: ACCEPTED
; -condition-satisfied(child: ACCEPTED): child: SELECTED, condition: ACCEPTED
; -do-succeed(child: SUCCEEDED):
;   num-times >= max_times: -> SUCCEEDED                                   -> T
;   num-times < max_times: -> READY, child: SUCCEEDED
; ...
;
; ForEach loop
; (FOR-EACH over list of protos)
; -start-for-each(child: ACCEPTED): READY->RUNNING, child: SELECTED
; -do-failed(child: FAILED): -> FAILED                                     -> T
; -for-each-do-succeeded(child: SUCCEEDED):
;   num-times < len(loop-for-each-loop-protos): -> READY, child: ACCEPTED
; -start-for-each(child: ACCEPTED): READY->RUNNING, child: SELECTED
; -do-failed(child: FAILED): -> FAILED                                     -> T
; -for-each-do-succeeded(child: SUCCEEDED):
;   num-times >= len(loop-for-each-loop-protos): -> SUCCEEDED              -> T
;   num-times < len(loop-for-each-loop-protos): -> READY, child: ACCEPTED
; ...

; --------------------------------- FUNCTIONS ---------------------------------

; Update the current counter of the given ?node to ?counter-value
;
; Args:
;   ?node: must be a fact-address of a loop node
;   ?counter-value: New value for the num-times slot of a loop node
(deffunction behavior-tree-loop-node-update-counter (?node ?counter-value)
  (modify ?node (loop-num-times ?counter-value))
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-loop-node-start
  "A loop node repeatedly runs a sub-tree."
  (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode WHILE) (state READY)
                               (loop-while-id ?while-condition-id&~nil)
                               (loop-max-times ?max-times)
                               (loop-num-times ?num-times
                                               &:(or (= ?max-times 0)
                                                     (< ?num-times ?max-times)))
                               (loop-counter-blackboard-key ?key))
  ?cond <- (behavior-tree-condition (id ?while-condition-id) (state ACCEPTED)
                                    (node-id ?node-id) (tree-id ?tree-id))
 =>
  (bind ?iteration-span
    (tracing-cc-start-node-iteration-span ?tree-id ?node-id ?num-times))
  (modify ?node (state RUNNING) (loop-num-times (+ ?num-times 1))
    (iteration-span-reference-id ?iteration-span))
  (if (neq ?key "") then
    (bind ?p (pb-create "google.protobuf.Int64Value"))
    (pb-set-field ?p "value" ?num-times)
    (assert (blackboard-update (key ?key) (scope ?bb-scope)
                               (operation-name ?op-name) (proto-id ?p)
                               (source-type NODE)
                               (source-tree-id ?tree-id)
                               (source-node-id ?node-id)))
  )
  (modify ?cond (state START))
)

(defrule behavior-tree-loop-node-start-count
  "If no condition is given we assume simply 'true' as the condition."
  (behavior-tree (id ?tree-id) (state RUNNING) (blackboard-scope ?bb-scope)
                 (operation-name ?op-name))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode COUNT) (state READY)
                               (loop-max-times ?max-times)
                               (loop-num-times ?num-times
                                               &:(or (= ?max-times 0)
                                                     (< ?num-times ?max-times)))
                               (loop-do-id ?loop-do-id)
                               (loop-counter-blackboard-key ?key))
  ?child <- (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                                (state ACCEPTED))
 =>
  (bind ?iteration-span
    (tracing-cc-start-node-iteration-span ?tree-id ?node-id ?num-times))
  (modify ?node (state RUNNING) (loop-num-times (+ ?num-times 1))
    (iteration-span-reference-id ?iteration-span))
  (if (neq ?key "") then
    (bind ?p (pb-create "google.protobuf.Int64Value"))
    (pb-set-field ?p "value" ?num-times)
    (assert (blackboard-update (key ?key) (scope ?bb-scope)
                               (operation-name ?op-name) (proto-id ?p)
                               (source-type NODE)
                               (source-tree-id ?tree-id)
                               (source-node-id ?node-id)))
  )
  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-loop-node-start-for-each
  "Start a for-each node iteration"
  (behavior-tree (id ?tree-id) (state RUNNING) (blackboard-scope ?bb-scope)
                 (operation-name ?op-name))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode FOR-EACH) (state READY)
                               (loop-for-each-loop-protos $?loop-protos)
                               (loop-for-each-value-blackboard-key
                                 ?current-value-key)
                               (loop-num-times ?num-times&
                                 :(< ?num-times (length$ ?loop-protos)))
                               (loop-do-id ?loop-do-id)
                               (loop-counter-blackboard-key ?loop-counter-key))
  ?child <- (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                                (state ACCEPTED))
 =>
  (bind ?iteration-span
    (tracing-cc-start-node-iteration-span ?tree-id ?node-id ?num-times))
  (modify ?node (state RUNNING) (loop-num-times (+ ?num-times 1))
                (iteration-span-reference-id ?iteration-span))
  (if (neq ?loop-counter-key "") then
    (bind ?p (pb-create "google.protobuf.Int64Value"))
    (pb-set-field ?p "value" ?num-times)
    (assert (blackboard-update (key ?loop-counter-key) (scope ?bb-scope)
                               (operation-name ?op-name) (proto-id ?p)
                               (source-type NODE)
                               (source-tree-id ?tree-id)
                               (source-node-id ?node-id)))
  )
  (if (neq ?current-value-key "") then
    (bind ?current-proto (pb-clone (nth$ (+ ?num-times 1) ?loop-protos)))
    (assert (blackboard-update (key ?current-value-key) (scope ?bb-scope)
                               (operation-name ?op-name)
                               (proto-id ?current-proto)
                               (source-type NODE)
                               (source-tree-id ?tree-id)
                               (source-node-id ?node-id)))
  )

  (behavior-tree-select-node ?child)
)

(defrule behavior-tree-loop-node-for-each-generate-loop-protos
  "In the first iteration generate the protos to loop over"
  ?tree <- (behavior-tree (id ?tree-id) (state RUNNING) (blackboard-scope ?bb-scope)
                 (operation-name ?op-name))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode FOR-EACH) (state READY)
                               (loop-for-each-generator-expression ?gen-expr)
                               (loop-for-each-generator-expression-id
                                                                   ?gen-expr-id)
                               (loop-for-each-loop-protos $?cur-loop-protos&:
                                 (empty$ ?cur-loop-protos))
                               (loop-num-times 0))
 =>
  (bind ?generic-descriptor-pool
        ?*PROTO-DESCRIPTOR-POOL-STANDARD-MESSAGES*)
  (bind ?eval-result
    (cel-eval-to-proto-list ?gen-expr-id ?generic-descriptor-pool
                            ?bb-scope ?op-name))
  (if (not (result-ok ?eval-result)) then
      (bind ?es-proto (extended-status-create 32001))
      (bind ?message
        (str-cat "Failed to evaluate CEL expression '" ?gen-expr
                 "' to generate loop protos"))
      (extended-status-set-message ?es-proto USER ?message)
      (extended-status-set-related-to ?es-proto ?tree ?node)
      (bind ?es-context-proto (result-error ?eval-result))
      (extended-status-add-context ?es-proto ?es-context-proto)
      (behavior-tree-set-node-failed ?node EXECUTION ?message
        EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto)
      (pb-remove ?es-context-proto)
      (pb-remove ?es-proto)
      (return)
  )

  ; Entry 1 is TRUE, [2, N] are the protos
  (bind ?loop-protos (subseq$ ?eval-result 2 (length$ ?eval-result)))

  (if (empty$ ?loop-protos) then
    ; An empty loop immediately succeeds. There should be no iterations on
    ; an empty list.
    (behavior-tree-set-node-succeeded ?node)
    (return)
  )

  (modify ?node (loop-for-each-loop-protos ?loop-protos))
)

(defrule behavior-tree-loop-node-start-iterations-exhausted
  "A loop node is to start (an iteration) but at its max-times."
  ; This usually only happens when the loop counter was changed to max-times
  ; exactly after an iteration has finished (e.g., while suspended)
  (behavior-tree (id ?tree-id) (state RUNNING|SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (state READY)
                               (loop-max-times ?max-times)
                               (loop-num-times ?num-times
                                               &:(and (> ?max-times 0)
                                                      (>= ?num-times ?max-times))))
 =>
  (behavior-tree-set-node-succeeded ?node)
)

(defrule behavior-tree-loop-node-reset-before-continuing-satisfied-while
  "The loop while condition is satisfied, reset the child node for the next iteration."
  (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type LOOP) (loop-mode WHILE) (state RUNNING)
                      (loop-while-id ?while-condition-id&~nil)
                      (loop-do-id ?loop-do-id))
  (behavior-tree-condition (id ?while-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied TRUE))
  ?child <- (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                                (state SUCCEEDED))
 =>
  (behavior-tree-node-reset ?child ?op-name)
)

(defrule behavior-tree-loop-node-condition-satisfied
  "The loop while condition is satisfied, execute loop, reset condition."
  (behavior-tree (id ?tree-id) (state RUNNING) (operation-name ?op-name))
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type LOOP) (loop-mode WHILE) (state RUNNING)
                      (loop-while-id ?while-condition-id&~nil)
                      (loop-do-id ?loop-do-id))
  (behavior-tree-condition (id ?while-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied TRUE))
  ?child <- (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                                (state ACCEPTED))
 =>
  (behavior-tree-select-node ?child)
  (behavior-tree-condition-reset ?while-condition-id ?op-name FALSE)
)

(defrule behavior-tree-loop-node-condition-unsatisfied
  "The loop while condition is unsatisfied, no longer execute loop."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode WHILE)
                               (state RUNNING|CANCELING)
                               (loop-while-id ?while-condition-id&~nil)
                               (loop-do-id ?loop-do-id)
                               (loop-counter-blackboard-key ?key)
                               (iteration-span-reference-id ?iteration-span))
  (behavior-tree-condition (id ?while-condition-id) (state FINISHED)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (satisfied FALSE))
 =>
  (span-end-failure ?iteration-span
    ?*TRACING-STATUS-FAILED-PRECONDITION* "Loop condition false")
  (bind ?node (modify ?node
    (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))

  (behavior-tree-set-node-succeeded ?node)
  (behavior-tree-condition-reset ?while-condition-id ?op-name FALSE)
)

(defrule behavior-tree-loop-node-condition-error
  "Condition evaluation returned with an unreported error"
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING))
  ?node <- (behavior-tree-node (tree-id ?tree-id)
                               (type LOOP) (loop-mode WHILE)
                               (state RUNNING|CANCELING)
                               (loop-while-id ?condition-id&~nil))
  (behavior-tree-condition (id ?condition-id) (state ERROR)
                           (extended-status-proto-id ?context-es))
 =>
  (bind ?expl "Condition did not provide extended status information")
  (if (<> ?context-es 0) then (bind ?expl (pb-tostring ?context-es)))
  (printout warn "While condition '" ?condition-id "' errored: " ?expl crlf)

  (behavior-tree-set-node-failed ?node EXECUTION "Loop while condition failed."
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
)

(defrule behavior-tree-loop-node-do-succeeded
  "The loop body has succeeded."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode COUNT|WHILE)
                               (state RUNNING|CANCELING)
                               (loop-do-id ?loop-do-id)
                               (loop-max-times ?max-times)
                               (loop-num-times ?num-times)
                               (loop-while-id ?while-condition-id)
                               (loop-counter-blackboard-key ?key)
                               (iteration-span-reference-id ?iteration-span))
  ?do-node <- (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                                  (state SUCCEEDED))
  (or (test (eq ?while-condition-id nil))
      (behavior-tree-condition (id ?while-condition-id) (state ACCEPTED)
                               (node-id ?node-id) (tree-id ?tree-id)))
 =>
  (span-end ?iteration-span)
  (if (or (= ?max-times 0) (< ?num-times ?max-times)) then
  ; there are still potentially more loop runs to be done
    (if (eq ?while-condition-id nil) then
    ; otherwise the node will be cleared due to the
    ; rule behavior-tree-loop-node-reset-before-continuing-satisfied-while
    ; after the condition succeeded and before the do node gets activated
    ; again.
      (behavior-tree-node-reset ?do-node ?op-name)
    )
    (modify ?node (state READY)
      (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
   else ; number of runs exhausted
    (bind ?node (modify ?node
      (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))
    (behavior-tree-set-node-succeeded ?node)
  )
)

(defrule behavior-tree-loop-node-for-each-do-succeeded
  "The loop body of a for-each loop has succeeded."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode FOR-EACH)
                               (state RUNNING|CANCELING)
                               (loop-do-id ?loop-do-id)
                               (loop-num-times ?num-times)
                               (loop-for-each-loop-protos $?loop-protos)
                               (loop-for-each-value-blackboard-key
                                 ?current-value-key)
                               (loop-counter-blackboard-key ?key)
                               (iteration-span-reference-id ?iteration-span))
  ?do-node <- (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                                  (state SUCCEEDED))
 =>
  (span-end ?iteration-span)
  (if (< ?num-times (length$ ?loop-protos))
   then
    ; there are still potentially more loop runs to be done
    (behavior-tree-node-reset ?do-node ?op-name)
    (modify ?node (state READY)
      (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
   else ; number of runs exhausted
    (bind ?node (modify ?node
      (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))
    (behavior-tree-set-node-succeeded ?node)
    (if (neq ?current-value-key "") then
      (blackboard-remove ?current-value-key ?bb-scope ?op-name)
    )
  )
)

(defrule behavior-tree-loop-node-do-failed
  "Then loop body has failed."
  (behavior-tree (id ?tree-id) (state RUNNING|CANCELING|SUSPENDING)
                 (operation-name ?op-name)
                 (blackboard-scope ?bb-scope))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode COUNT|WHILE|FOR-EACH)
                               (state RUNNING|CANCELING)
                               (loop-do-id ?loop-do-id)
                               (loop-counter-blackboard-key ?key)
                               (loop-for-each-value-blackboard-key
                                 ?current-value-key)
                               (iteration-span-reference-id ?iteration-span))
  (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                      (state FAILED)
                      (extended-status-proto-id ?context-es))
 =>
  (span-end-failure ?iteration-span ?*TRACING-STATUS-ABORTED* "Do child failed")
  (bind ?node (modify ?node
    (iteration-span-reference-id ?*TRACING-INVALID-SPAN-ID*)))
  (behavior-tree-set-node-failed ?node EXECUTION "Do child failed"
                                 EXTENDED-STATUS-CONTEXT-PROTO-ID ?context-es)
  (if (neq ?current-value-key "") then
    (blackboard-remove ?current-value-key ?bb-scope ?op-name)
  )
)

(defrule behavior-tree-loop-node-suspend
  "Suspend node when tree suspends and children are inactive."
  (behavior-tree (id ?tree-id) (state SUSPENDING))
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                               (type LOOP) (loop-mode COUNT|WHILE|FOR-EACH)
                               (state ?state&RUNNING|READY)
                               (loop-do-id ?loop-do-id)
                               (loop-while-id ?while-condition-id))
  ; Child not still busy (need to wait) and not failed (fail rule fires)
  (not (behavior-tree-node (id ?loop-do-id) (tree-id ?tree-id)
                           (parent-id ?node-id)
                           (state EVALUATING-CONDITION|RUNNING|FAILED)))
 =>
  (modify ?node (state SUSPENDED) (suspended-from-state ?state)
          (suspend-span-reference-id (tracing-start-suspend-span ?node)))
)

(defrule behavior-tree-loop-node-resume
  "Resume node if tree becomes RUNNING again."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?node <- (behavior-tree-node (id ?id) (tree-id ?tree-id)
                               (parent-id ?parent-id)
                               (type LOOP) (loop-mode COUNT|WHILE|FOR-EACH)
                               (state SUSPENDED)
                               (suspended-from-state ?resume-state))
  (or (test (eq ?start-node-id ?id))
      (behavior-tree-node (id ?parent-id) (tree-id ?tree-id) (state RUNNING)))
 =>
  (tracing-end-suspend-span ?node)
  (modify ?node (state ?resume-state) (suspended-from-state nil)
          (suspend-span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-loop-node-cancel
  "Cancels the node's in-flight child."
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type LOOP)
                      (state CANCELING))
  ?child <- (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                                (state SELECTED|READY|RUNNING|SUSPENDED))
 =>
  (modify ?child (state CANCELING)
          (canceling-span-reference-id (tracing-start-canceling-span ?child)))
)

(defrule behavior-tree-loop-node-canceled
  "Cancels the node if its child is waiting."
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id) (type LOOP)
                               (state CANCELING)
                               (loop-while-id ?while-condition-id))
  ; All children are either accepted, or were canceled
  (not (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                           (state ~ACCEPTED&~CANCELED)))

  ; There is no condition which is or still needs to be cancelled
  (not (behavior-tree-condition (id ?while-condition-id)
                                (node-id ?node-id) (tree-id ?tree-id)
                                (state ~ACCEPTED&~FINISHED&~CANCELED)))
 =>
  (behavior-tree-set-node-canceled ?node)
)

(defrule behavior-tree-loop-node-cancel-in-condition
  "Cancels the loop node while still evaluating the while condition."
  (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type LOOP) (loop-mode WHILE) (state CANCELING)
                      (loop-while-id ?while-condition-id&~nil))
  ?cond <- (behavior-tree-condition (id ?while-condition-id)
                                    (node-id ?node-id) (tree-id ?tree-id)
                                    (state ~ACCEPTED&~FINISHED&
                                           ~CANCELING&~CANCELED))
 =>
  (modify ?cond (state CANCELING))
)

(defrule behavior-tree-loop-node-canceled-in-condition
  "Cancels the loop node while still evaluating the while condition."
  ?node <- (behavior-tree-node (id ?node-id) (tree-id ?tree-id)
                      (type LOOP) (loop-mode WHILE) (state CANCELING)
                      (loop-while-id ?while-condition-id&~nil))
  (behavior-tree-condition (id ?while-condition-id)
                           (node-id ?node-id) (tree-id ?tree-id)
                           (state ACCEPTED|CANCELED))
  ; The child must not be running already. Since we reset the condition in a
  ; while loop only in reset-before-continuing-satisfied-while the child can be
  ; in any resting state.
  (behavior-tree-node (tree-id ?tree-id) (parent-id ?node-id)
                      (state ACCEPTED|SUCCEEDED|FAILED|CANCELED))
 =>
  (behavior-tree-set-node-canceled ?node)
)
