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

; Common functions for behavior tree tracing.
;
; This file provides functions that can be called when starting or ending an
; execution of a behavior tree node and will then start/end the respective
; tracing spans.

; The following invariants must always hold:
; If tracing is enabled in general, there exists a valid execution span in the
; operation-envelope, iff the executive is RUNNING, SUSPENDING, or CANCELING.
; If this is not the case, then no node span must be running.
; If an execution span exists, then all running nodes must have a respective
; node span. Thus it is always possible to find a valid parent span,
; if there is a valid execution-span.

; Corresponding checks for these invariants are in tracing_check.clp.

; --------------------------------- FUNCTIONS ---------------------------------

(deffunction tracing-end-suspend-span (?node)
  "Ends a suspend span for ?node."
  (bind ?suspend-span-reference-id
    (fact-slot-value ?node suspend-span-reference-id))
  (span-end ?suspend-span-reference-id)
)

(deffunction tracing-end-suspend-span-failure (?node ?status-code ?status-msg)
  "Ends a suspend span in ?node  with a failure."
  (bind ?suspend-span-reference-id
    (fact-slot-value ?node suspend-span-reference-id))
  (span-end-failure ?suspend-span-reference-id ?status-code ?status-msg)
)

(deffunction tracing-start-suspend-span (?node)
  "Starts a span for a suspended node.
   Returns the span-reference-id of the started span."

  (bind ?execution-span
    (operation-get-execution-span-by-tree (fact-slot-value ?node tree-id)))
  (if (= ?execution-span ?*TRACING-INVALID-SPAN-ID*)
    then (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (bind ?current-span-reference-id
    (fact-slot-value ?node suspend-span-reference-id))
  (bind ?parent-span-reference-id (fact-slot-value ?node span-reference-id))

  (if (= ?parent-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    ; There is an execution span, thus we expect a valid parent span
    (bind ?error-msg (str-cat "Suspend: invalid node span in node with id "
      (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
    (printout error ?error-msg crlf)
    (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (if (<> ?current-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (format nil
      (str-cat "Starting a suspend span had an active tracing "
               "span with id %d. The span will be ended by force.")
      ?current-span-reference-id))
    (printout error ?error-msg crlf)

    (tracing-end-suspend-span-failure ?node
      ?*TRACING-STATUS-ALREADY-EXISTS* ?error-msg)
  )

  (bind ?span-type "suspend")
  (bind ?suspend-span (span-start "Suspended"
                                    ?parent-span-reference-id ?span-type))
  (if (= ?suspend-span ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (str-cat "Failed to start suspend span for node with id "
      (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
    (printout error ?error-msg crlf)
  )
  (return ?suspend-span)
)


(deffunction tracing-end-canceling-span (?node)
  "Ends a canceling span for ?node."
  (bind ?canceling-span-reference-id
    (fact-slot-value ?node canceling-span-reference-id))
  (span-end ?canceling-span-reference-id)
)

(deffunction tracing-end-canceling-span-failure (?node ?status-code ?status-msg)
  "Ends a canceling span in ?node  with a failure."
  (bind ?canceling-span-reference-id
    (fact-slot-value ?node canceling-span-reference-id))
  (span-end-failure ?canceling-span-reference-id ?status-code ?status-msg)
)

(deffunction tracing-start-canceling-span (?node)
  "Starts a span for a canceling node.
   Returns the span-reference-id of the started span."

  (bind ?execution-span
    (operation-get-execution-span-by-tree (fact-slot-value ?node tree-id)))
  (if (= ?execution-span ?*TRACING-INVALID-SPAN-ID*) then
    (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (bind ?current-span-reference-id
    (fact-slot-value ?node canceling-span-reference-id))
  (bind ?parent-span-reference-id (fact-slot-value ?node span-reference-id))

  (if (= ?parent-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    ; There is an execution span, thus we expect a valid parent span
    (bind ?error-msg (str-cat "Canceling: invalid node span in node with id "
      (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
    (printout error ?error-msg crlf)
    (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (if (<> ?current-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (format nil
      (str-cat "Starting a canceling span had an active tracing "
               "span with id %d. The span will be ended by force.")
      ?current-span-reference-id))
    (printout error ?error-msg crlf)

    (tracing-end-canceling-span-failure ?node
      ?*TRACING-STATUS-ALREADY-EXISTS* ?error-msg)
  )

  (bind ?span-type "canceling")
  (bind ?canceling-span (span-start "Canceling"
                                    ?parent-span-reference-id ?span-type))
  (if (= ?canceling-span ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (str-cat "Failed to start canceling span for node with id "
      (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
    (printout error ?error-msg crlf)
  )
  (return ?canceling-span)
)


(deffunction tracing-end-evaluating-condition-span (?node)
  "Ends a condition span in ?node as the node's condition was evaluated."
  (bind ?condition-span-reference-id
    (fact-slot-value ?node condition-span-reference-id))
  (span-end ?condition-span-reference-id)
)

(deffunction tracing-end-evaluating-condition-span-failure (?node ?status-code ?status-msg)
  "Ends a condition span in ?node as the node's condition was evaluated.
   Takes an optional status code for failed evaluations."
  (bind ?condition-span-reference-id
    (fact-slot-value ?node condition-span-reference-id))
  (span-end-failure ?condition-span-reference-id ?status-code ?status-msg)
)

(deffunction tracing-start-evaluating-condition-span (?node)
  "Starts a span for the condition evaluation phase of a node.
   ?node must have a ~nil condition-id.
   Returns the span-reference-id of the started span."

  (bind ?execution-span
    (operation-get-execution-span-by-tree (fact-slot-value ?node tree-id)))
  (if (= ?execution-span ?*TRACING-INVALID-SPAN-ID*)
    then (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (bind ?current-span-reference-id
    (fact-slot-value ?node condition-span-reference-id))
  (bind ?parent-span-reference-id (fact-slot-value ?node span-reference-id))

  (if (= ?parent-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    ; There is an execution span, thus we expect a valid parent span
    (bind ?error-msg (str-cat "Evaluating Condition: Invalid node span "
      "in node with id "
      (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
    (printout error ?error-msg crlf)
    (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (if (<> ?current-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (format nil
      (str-cat "Starting an evaluating condition span had an active tracing "
               "span with id %d. The span will be ended by force.")
      ?current-span-reference-id))
    (printout error ?error-msg crlf)

    (tracing-end-evaluating-condition-span-failure ?node
      ?*TRACING-STATUS-ALREADY-EXISTS* ?error-msg)
  )

  (bind ?node-type (lowcase (fact-slot-value ?node type)))
  (bind ?span-type (str-cat ?node-type "-condition"))
  (bind ?condition-span (span-start "Evaluating Decorator Condition"
    ?parent-span-reference-id ?span-type))
  (if (= ?condition-span ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (str-cat "Failed to start evaluating condition span "
      "for node with id "
      (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
    (printout error ?error-msg crlf)
  )
  (return ?condition-span)
)

(deffunction tracing-determine-parent-condition-span (?condition)
  "Determines the correct parent span for ?condition (fact address).
   The parent span might originate from another condition
   or a behavior tree node."
  (bind ?condition-id (fact-slot-value ?condition id))
  (bind ?condition-parent-id (fact-slot-value ?condition parent-id))

  ; 1. Check if ?condition is the decorator condition of a behavior tree node
  (do-for-fact ((?node behavior-tree-node))
    (eq ?node:condition-id ?condition-id)
    (return ?node:condition-span-reference-id)
  )

  ; 2. Check if ?condition is the if-condition of a branch node
  (do-for-fact ((?branch-node behavior-tree-node))
    (and (eq ?branch-node:type BRANCH)
         (eq ?branch-node:branch-if-id ?condition-id))
    (return ?branch-node:span-reference-id)
  )

  ; 3. Check if ?condition is the while-condition of a loop node
  (do-for-fact ((?loop-node behavior-tree-node))
    (and (eq ?loop-node:type LOOP)
         (eq ?loop-node:loop-while-id ?condition-id))
    (return ?loop-node:iteration-span-reference-id)
  )

  ; 4. Check if ?condition is a child of another condition
  (if (neq ?condition-parent-id nil) then
    (do-for-fact ((?parent-condition behavior-tree-condition))
      (eq ?parent-condition:id ?condition-parent-id)
      (return ?parent-condition:span-reference-id)
    )
  )

  (bind ?error-msg (str-cat "Could not find parent for condition with id "
    ?condition-id))
  (printout error ?error-msg crlf)

  (return ?*TRACING-INVALID-SPAN-ID*)
)


(deffunction tracing-end-condition-span (?condition)
  "Ends a condition span running in ?condition as this condition was evaluated."
  (bind ?span-reference-id (fact-slot-value ?condition span-reference-id))
  (span-end ?span-reference-id)
)

(deffunction tracing-end-condition-span-failure (?condition ?status-code ?status-msg)
  "Ends a condition span running in ?condition as this condition was evaluated.
   Takes an optional status-code for failed evaluations."
  (bind ?span-reference-id (fact-slot-value ?condition span-reference-id))
  (span-end-failure ?span-reference-id ?status-code ?status-msg)
)

(deffunction tracing-start-condition-span (?condition)
  "Starts a span for evaluating this condition.
   Returns the span-reference-id of the started span."

  (bind ?execution-span
    (operation-get-execution-span-by-tree (fact-slot-value ?condition tree-id)))
  (if (= ?execution-span ?*TRACING-INVALID-SPAN-ID*)
    then (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (bind ?current-span-reference-id
    (fact-slot-value ?condition span-reference-id))
  (if (<> ?current-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (format nil
      (str-cat "Starting a condition span had an active tracing "
               "span with id %d. The span will be ended by force.")
      ?current-span-reference-id))
    (printout error ?error-msg crlf)

    (tracing-end-condition-span-failure ?condition
      ?*TRACING-STATUS-ALREADY-EXISTS* ?error-msg)
  )

  (bind ?parent-span-reference-id
    (tracing-determine-parent-condition-span ?condition))
  (if (= ?parent-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    ; There is an execution span, thus we expect a valid parent span
    (bind ?error-msg (str-cat "Condition: Could not find parent span "
      "for condition with id "  (fact-slot-value ?condition id)))
    (printout error ?error-msg crlf)
    (return ?*TRACING-INVALID-SPAN-ID*)
  )

  (bind ?condition-type (lowcase (fact-slot-value ?condition type)))
  (bind ?condition-name ?condition-type)
  ; Mark root conditions in loop and branch nodes
  (do-for-fact ((?branch-node behavior-tree-node))
    (and (eq ?branch-node:type BRANCH)
         (eq ?branch-node:branch-if-id (fact-slot-value ?condition id)))
    (bind ?condition-name (str-cat "Branch Condition (" ?condition-type ")"))
  )
  (do-for-fact ((?loop-node behavior-tree-node))
    (and (eq ?loop-node:type LOOP)
         (eq ?loop-node:loop-while-id (fact-slot-value ?condition id)))
    (bind ?condition-name (str-cat "While Condition (" ?condition-type ")"))
  )

  (bind ?condition-span (span-start ?condition-name
    ?parent-span-reference-id ?condition-type))
  (if (= ?condition-span ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (str-cat "Failed to start condition span for "
      "condition with id " (fact-slot-value ?condition id)))
    (printout error ?error-msg crlf)
  )
  (return ?condition-span)
)


; --------------------------------- RULES ---------------------------------

(defrule tracing-condition-start
  "Start a tracing span, when a condition is to be evaluated"
  (declare (salience ?*SALIENCE-TRACING-CONDITIONS*))
  ?cf <- (behavior-tree-condition (state START) (tree-id ?tree-id)
                                  (span-reference-id 0))
  ; Fire only if operation envelope is running a tracing span
  (operation-envelope (operation-tree-id ?operation-tree-id&
                        :(eq ?operation-tree-id
                          (behavior-tree-get-top-level-tree-id ?tree-id)))
                      (span-reference-id ~0))
 =>
  (bind ?new-span-id (tracing-start-condition-span ?cf))
  (if (<> ?new-span-id ?*TRACING-INVALID-SPAN-ID*) then
    (modify ?cf (span-reference-id ?new-span-id))
  )
)

(defrule tracing-condition-finished
  "End a tracing span, when a condition is finished"
  (declare (salience ?*SALIENCE-TRACING-CONDITIONS*))
  ?cf <- (behavior-tree-condition (state FINISHED) (satisfied ?satisfied)
    (span-reference-id ~0))
 =>
  (if (eq ?satisfied TRUE) then
    (tracing-end-condition-span ?cf)
  else
    (tracing-end-condition-span-failure ?cf
      ?*TRACING-STATUS-ABORTED* "Condition unsatisfied")
  )
  (modify ?cf (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule tracing-condition-error
  "End a tracing span, when a condition errors"
  (declare (salience ?*SALIENCE-TRACING-CONDITIONS*))
  ?cf <- (behavior-tree-condition (id ?condition-id) (state ERROR)
    (span-reference-id ~0))
 =>
  (bind ?error-msg (str-cat "Condition '" ?condition-id "' errored"))
  (tracing-end-condition-span-failure ?cf
    ?*TRACING-STATUS-INTERNAL* ?error-msg)
  (modify ?cf (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)
