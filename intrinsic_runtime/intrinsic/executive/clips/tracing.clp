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

; Common definitions for execution tracing.

(defglobal
 ; Status codes for ending opencensus traces according to
 ; opencensus/trace/status_code.h
 ?*TRACING-STATUS-OK* = 0
 ?*TRACING-STATUS-CANCELLED* = 1
 ?*TRACING-STATUS-UNKNOWN* = 2
 ?*TRACING-STATUS-INVALID-ARGUMENT* = 3
 ?*TRACING-STATUS-DEADLINE-EXCEEDED* = 4
 ?*TRACING-STATUS-NOT-FOUND* = 5
 ?*TRACING-STATUS-ALREADY-EXISTS* = 6
 ?*TRACING-STATUS-PERMISSION-DENIED* = 7
 ?*TRACING-STATUS-UNAUTHENTICATED* = 16
 ?*TRACING-STATUS-RESOURCE-EXHAUSTED* = 8
 ?*TRACING-STATUS-FAILED-PRECONDITION* = 9
 ?*TRACING-STATUS-ABORTED* = 10
 ?*TRACING-STATUS-OUT-OF-RANGE* = 11
 ?*TRACING-STATUS-UNIMPLEMENTED* = 12
 ?*TRACING-STATUS-INTERNAL* = 13
 ?*TRACING-STATUS-UNAVAILABLE* = 14
 ?*TRACING-STATUS-DATA-LOSS* = 15

 ; If span-reference-id is set to this, it is not representing a valid span.
 ?*TRACING-INVALID-SPAN-ID* = 0
)

; ---------------------------- TEMPLATES ----------------------------

; This fact might or might not exist depending if we are tracing CLIPS runs.
; It is automatically set by the executor in the case that we are tracing and
; the contained span-reference-id can be used during CLIPS runs to parent
; traces of internal calls that might take time.
(deftemplate tracing-clips-run-context
  (slot span-reference-id (type INTEGER) (default ?*TRACING-INVALID-SPAN-ID*))
)

; ---------------------------- FORWARD DECLARATIONS ----------------------------

; defined in operation_handling.clp
(deffunction operation-get-execution-span-by-tree (?tree-id))

; --------------------------------- FUNCTIONS ---------------------------------

(deffunction tracing-determine-operation-update-state-execution-span
  (?current-state ?target-state ?current-span ?target-span)
  "Determine the execution-span to be set for operation-update-state.
   Depending on the requested state transition either the ?current-span is
   retained or the ?target-span is used."

  (bind ?new-span-reference-id ?*TRACING-INVALID-SPAN-ID*)
  ; Update to the new ?target-span iff transitioning to PREPARING
  ; unless the executive is already in PREPARING, SUSPENDING or SUSPENDED,
  ; then keep the old span ?current-span.
  (if (and (eq ?target-state PREPARING)
           (neq ?current-state PREPARING)
           (neq ?current-state SUSPENDING)
           (neq ?current-state SUSPENDED))
    then
      (if (<> ?current-span ?*TRACING-INVALID-SPAN-ID*) then
        (bind ?error-msg (format nil
          (str-cat "Transitioning to " ?target-state " state from %s had an active "
            "tracing span with id %d. The span will be ended by force.")
          (str-cat ?current-state) ?current-span))
        (printout error ?error-msg crlf)
        (span-end-failure ?current-span ?*TRACING-STATUS-ALREADY-EXISTS*
          (str-cat "A span was already active when switching to " ?target-state "."))
      )

      (bind ?new-span-reference-id ?target-span)
    else
      (if (<> ?target-span ?*TRACING-INVALID-SPAN-ID*) then
        (bind ?error-msg (format nil
          (str-cat "Ignoring span-reference-id %d in operation-update-state,"
                   " while transitioning from %s to %s.")
          ?target-span (str-cat ?current-state) (str-cat ?target-state)))
        (printout error ?error-msg crlf)
      )

      (bind ?new-span-reference-id ?current-span)
  )
  (return ?new-span-reference-id)
)

(deffunction tracing-end-action-span (?action)
  "Ends a span running in ?action when ?action is SUCCEEDED or FAILED."
  (bind ?action-span-reference-id (fact-slot-value ?action span-reference-id))
  (span-end ?action-span-reference-id)
)

(deffunction tracing-end-action-span-failure (?action ?status-code ?status-msg)
  "Ends a span running in ?action when ?action is SUCCEEDED or FAILED.
   Takes an optional status-code for failed executions."
  (bind ?action-span-reference-id (fact-slot-value ?action span-reference-id))
  (span-end-failure ?action-span-reference-id ?status-code ?status-msg)
)

(deffunction tracing-start-action-span (?pa ?parent-span-reference-id)
  "Starts a span for a plan-action ?pa. Typically called, when ?pa is
   duplicated from a prototype and SELECTED for execution.
   If a valid ?parent-span-reference-id is given the action span will be a
   child of ?parent-span-reference-id, otherwise the action span will be a
   new root span in a new trace.
   This function will always start a span as it is the root span for a
   skill trace, even if behavior tree tracing is disabled.
   Returns the span-reference-id of the started span."

  (bind ?pa-span-reference-id (fact-slot-value ?pa span-reference-id))
  (if (<> ?pa-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (format nil
      (str-cat "Starting an action span had an active tracing "
               "span with id %d. The span will be ended by force.")
      ?pa-span-reference-id))
    (printout error ?error-msg crlf)

    (tracing-end-action-span-failure ?pa
      ?*TRACING-STATUS-ALREADY-EXISTS* ?error-msg)
  )

  (bind ?action-skill-id (lowcase (str-cat (fact-slot-value ?pa skill-id))))
  (bind ?span-name (str-cat "Executing Action: '" ?action-skill-id "'"))
  (if (= ?parent-span-reference-id ?*TRACING-INVALID-SPAN-ID*)
    then
      (bind ?pa-span (span-start-root-span ?span-name "action_execution"))
    else
      (bind ?pa-span (span-start ?span-name ?parent-span-reference-id "action_execution"))
  )
  (if (<> ?pa-span ?*TRACING-INVALID-SPAN-ID*)
    then
      (span-add-attribute ?pa-span "action_name" ?action-skill-id)
    else
      (bind ?error-msg (str-cat "Failed to start span for action with id "
        (fact-slot-value ?pa id) " in " (fact-slot-value ?pa plan-id)))
      (printout error ?error-msg crlf)
  )
  (return ?pa-span)
)

(deffunction tracing-link-skill-trace (?node ?action)
  "Add a tracing link for the skill trace running in ?action
   to the node span in ?node.
   Typically called on a task node executing this skill."

  (bind ?node-span-reference-id (fact-slot-value ?node span-reference-id))
  (bind ?node-tree-id (fact-slot-value ?node tree-id))
  (if (= ?node-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (if (<> (operation-get-execution-span-by-tree ?node-tree-id)
            ?*TRACING-INVALID-SPAN-ID*) then
      (bind ?error-msg (str-cat "tracing-link-skill-trace: "
        "Invalid node span in node with id "
        (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
      (printout error ?error-msg crlf)
    )
    (return)
  )
  (bind ?action-span-reference-id (fact-slot-value ?action span-reference-id))
  (if (= ?action-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (if (<> (operation-get-execution-span-by-tree ?node-tree-id)
            ?*TRACING-INVALID-SPAN-ID*) then
      (bind ?error-msg (str-cat "tracing-link-skill-trace: "
        "Invalid action span in action with id "
        (fact-slot-value ?action id) " in " (fact-slot-value ?action plan-id)))
      (printout error ?error-msg crlf)
    )
    (return)
  )

  (bind ?trace-id-hex (span-get-trace-id ?action-span-reference-id))
  (bind ?span-id-hex (span-get-span-id ?action-span-reference-id))
  (bind ?trace-url "")
  (do-for-fact ((?gcp-flag flag))
      (and (eq ?gcp-flag:name google-cloud-project)
           (eq ?gcp-flag:type STRING))
    (bind ?trace-url
      (span-get-trace-url ?action-span-reference-id ?gcp-flag:value))
  )
  (span-add-attribute ?node-span-reference-id "skill_trace_id" ?trace-id-hex)
  (span-add-attribute ?node-span-reference-id "skill_trace_link" ?trace-url)

  ; TODO(b/254824039): Once resolved, remove this flag and always enable
  (if (any-factp ((?tlst-flag flag))
      (and (eq ?tlst-flag:name tracing-link-opencensus)
           (eq ?tlst-flag:type BOOL)
           (eq ?tlst-flag:value TRUE))) then
    (span-add-child-link ?node-span-reference-id ?trace-id-hex ?span-id-hex)
  )
)

(deffunction tracing-start-code-execution-span (?cei ?parent-span-reference-id)
  "Starts a span for a code-execution-instance ?cei.
   If a valid ?parent-span-reference-id is given the span will be a
   child of ?parent-span-reference-id, otherwise the span will be a
   new root span in a new trace.
   Returns the span-reference-id of the started span."

  (bind ?cei-span-reference-id (fact-slot-value ?cei span-reference-id))
  (if (<> ?cei-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (bind ?error-msg (format nil
      (str-cat "Starting a code execution span had an active tracing "
               "span with id %d. The span will be ended by force.")
      ?cei-span-reference-id))
    (printout error ?error-msg crlf)

    (span-end-failure ?cei-span-reference-id
      ?*TRACING-STATUS-ALREADY-EXISTS* ?error-msg)
  )

  (bind ?span-name "CodeExecution")
  (if (= ?parent-span-reference-id ?*TRACING-INVALID-SPAN-ID*)
    then
      (bind ?cei-span (span-start-root-span ?span-name "code_execution"))
    else
      (bind ?cei-span (span-start ?span-name ?parent-span-reference-id "code_execution"))
  )
  (if (= ?cei-span ?*TRACING-INVALID-SPAN-ID*)
    then
      (bind ?error-msg (str-cat "Failed to start span for code execution on node with id "
        (fact-slot-value ?cei node-id) " in " (fact-slot-value ?cei tree-id)))
      (printout error ?error-msg crlf)
  )
  (return ?cei-span)
)

(deffunction tracing-link-code-execution-trace (?node ?cei)
  "Add a tracing link for the code execution trace running in ?cei
   to the node span in ?node.
   Typically called on a task node executing this code execution."

  (bind ?node-span-reference-id (fact-slot-value ?node span-reference-id))
  (bind ?node-tree-id (fact-slot-value ?node tree-id))
  (if (= ?node-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (if (<> (operation-get-execution-span-by-tree ?node-tree-id)
            ?*TRACING-INVALID-SPAN-ID*) then
      (bind ?error-msg (str-cat "tracing-link-code-execution-trace: "
        "Invalid node span in node with id "
        (fact-slot-value ?node id) " in " (fact-slot-value ?node tree-id)))
      (printout error ?error-msg crlf)
    )
    (return)
  )
  (bind ?cei-span-reference-id (fact-slot-value ?cei span-reference-id))
  (if (= ?cei-span-reference-id ?*TRACING-INVALID-SPAN-ID*) then
    (if (<> (operation-get-execution-span-by-tree ?node-tree-id)
            ?*TRACING-INVALID-SPAN-ID*) then
      (bind ?error-msg (str-cat "tracing-link-code-execution-trace: "
        "Invalid code execution span in code execution with node-id "
        (fact-slot-value ?cei node-id) " in " (fact-slot-value ?cei tree-id)))
      (printout error ?error-msg crlf)
    )
    (return)
  )

  (bind ?trace-id-hex (span-get-trace-id ?cei-span-reference-id))
  (bind ?span-id-hex (span-get-span-id ?cei-span-reference-id))
  (bind ?trace-url "")
  (do-for-fact ((?gcp-flag flag))
      (and (eq ?gcp-flag:name google-cloud-project)
           (eq ?gcp-flag:type STRING))
    (bind ?trace-url
      (span-get-trace-url ?cei-span-reference-id ?gcp-flag:value))
  )
  (span-add-attribute ?node-span-reference-id "code_execution_trace_id" ?trace-id-hex)
  (span-add-attribute ?node-span-reference-id "code_execution_trace_link" ?trace-url)

  (if (any-factp ((?tlst-flag flag))
      (and (eq ?tlst-flag:name tracing-link-opencensus)
           (eq ?tlst-flag:type BOOL)
           (eq ?tlst-flag:value TRUE))) then
    (span-add-child-link ?node-span-reference-id ?trace-id-hex ?span-id-hex)
  )
)
