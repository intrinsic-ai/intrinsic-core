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

; Representation and handling for behavior call instances.
; Note: Currently only handles behavior trees, but not skills.
;
; --------------------------------- TEMPLATES ---------------------------------

; When executing a behavior call, there are two sides of parameterization:
;
; First the calling tree parameterizes the behavior call instance proto from
; the behavior call prototype proto in its execution context (e.g. blackboard).
; This is the same process for parameterizing a skill and behavior tree.
;
; Second, when executing a behavior call instance, the behavior call instance
; proto is used to parameterize the actual execution. For skills, this means
; passing the parameters proto and resource handles to the skill. For PBTs the
; skills in the PBT are parameterized using the behavior call instance proto.
;
; Thus at the point, where a behavior-call-instance is SELECTED its
; behavior call instance proto must be parameterized (i.e., the parameters proto
; is filled from assignments and the equipment has only handles, but not
; references)
(deftemplate behavior-call-instance
  ; A globally unique ID in the scope of the current executive lifetime. Use
  ; behavior-call-instance-generate-uid to generate a uid.
  ; If setting a custom UID you must guarantee uniqueness by yourself.
  (slot uid (type SYMBOL) (default ?NONE))

  ; References a skill-info:skill-type in skill_info.clp.
  (slot skill-type (type SYMBOL) (allowed-values SKILL BEHAVIOR-TREE))

  ; Proto ID of the imported behavior call proto.
  ; This must never be mutated after import.
  (slot behavior-call-prototype-proto (type INTEGER))
  ; Proto ID of the behavior call proto during execution of an instance.
  (slot behavior-call-instance-proto (type INTEGER))

  ; States follow convention from https://aip.dev/216.
  ; ACCEPTED: action has been formulated and is awaiting selection. If and
  ;           when it is selected depends on the specific semantics, e.g., for a
  ;           behavior tree that task node must have been selected.
  ;           This is the initial state for all behavior tree actions.
  ; SELECTED: behavior call has been selected for execution, i.e., it is
  ;           expected to parameterize and execute the associated behavior tree.
  ; RUNNING: execution is on-going
  ; CANCELLATION-REQUESTED: behavior-call-instance should cancel. As a result
  ;                         this instance should transition to canceling and
  ;                         eventually be canceled, fail or succeed.
  ; CANCELING:  behavior-call-instance cancellation is on-going. If cancellation
  ;             finishes the behavior-call-instance usually is canceled. If the
  ;             behavior-call-instance execution completes before the
  ;             cancellation it can also either succeed or fail.
  ; SUSPENDING: Behavior call is suspending its execution.
  ; SUSPENDED:  Behavior Call execution is suspended.
  ; SUCCEEDED: execution has completed successfully
  ; FAILED: execution has completed with failure
  (slot state (type SYMBOL)
        (allowed-values ACCEPTED SELECTED RUNNING
                        CANCELLATION-REQUESTED CANCELING
                        SUSPENDING
                        SUSPENDED
                        ; Terminal states
                        SUCCEEDED FAILED CANCELED)
        (default ACCEPTED))

  ; This is tracks the state slot and updates the run metadata proto if they
  ; get out of sync, i.e., when the state changes.
  (slot run-metadata-proto-state (type SYMBOL) (default ACCEPTED))

  ; An actual ExtendedStatus proto created and propagated on failure.
  (slot extended-status-proto-id (type INTEGER))

  ; The behavior tree that is being parameterized and executed, when this
  ; instance is executed.
  (slot parameterizable-tree-id (type SYMBOL))

  ; This behavior-call-instance's LogID for related log-context generation.
  ; 0 means that the behavior-call-instance has not yet been logged.
  (slot log-id (type INTEGER))
  (slot logged-completion (type SYMBOL) (allowed-values FALSE TRUE))
)

; --------------------------------- FUNCTIONS ---------------------------------

; Generates a unique ID symbol for a behavior-call-instance.
;
; Args:
;   ?prefix: Prefix for the uid, e.g., a tree-id that
;            is used as part of the uid for easier identification.
;
; Returns:
;   A unique symbol.
(deffunction behavior-call-instance-generate-uid (?prefix)
  (return (sym-cat ?prefix "-behavior-call-instances-" (gensym*)))
)

(deffunction behavior-call-instance-get-skill-id (?uid)
  (do-for-fact ((?bci behavior-call-instance)) (eq ?bci:uid ?uid)
    (if (<> ?bci:behavior-call-prototype-proto 0) then
      (return (pb-get-field ?bci:behavior-call-prototype-proto "skill_id"))
    )
  )
  (return "")
)

; Resets the behavior call instance.
;
; This also resets the associated ?parameterizable-tree-id, so it can be
; executed again.
; It is typically called on a previously executed action.
;
; Args:
;   ?uid: UID of a behavior-call-instance
(deffunction behavior-call-instance-reset (?uid ?keep-counters)
  (do-for-fact ((?bci behavior-call-instance)) (eq ?bci:uid ?uid)
    (pb-remove ?bci:behavior-call-instance-proto)
    (pb-remove ?bci:extended-status-proto-id)
    (if (neq ?bci:parameterizable-tree-id nil) then
      (behavior-tree-reset ?bci:parameterizable-tree-id ?keep-counters))
    (modify ?bci (state ACCEPTED) (log-id 0) (logged-completion FALSE))
  )
)

; Removes the behavior-call-instance with ?uid and deletes the associated tree.
;
; Args:
;   ?uid: UID of a behavior-call-instance
(deffunction behavior-call-instance-remove (?uid)
  (do-for-fact ((?bci behavior-call-instance)) (eq ?bci:uid ?uid)
    (pb-remove ?bci:behavior-call-prototype-proto)
    (pb-remove ?bci:behavior-call-instance-proto)
    (pb-remove ?bci:extended-status-proto-id)
    (if (neq ?bci:parameterizable-tree-id nil) then
      (behavior-tree-delete ?bci:parameterizable-tree-id)
    )
    (retract ?bci)
  )
)

; Converts a given behavior call instance to a nicely printable string.
;
; Args:
;   ?uid: UID of a behavior-call-instance
;
; Returns:
;   String representation of behavior call, may also be an error message.
(deffunction behavior-call-instance-tostring (?uid)
  (do-for-fact ((?bci behavior-call-instance)) (eq ?bci:uid ?uid)
    (if (<> ?bci:behavior-call-prototype-proto 0) then
      (bind ?skill-id
        (pb-get-field ?bci:behavior-call-prototype-proto "skill_id"))
      (return (str-cat ?skill-id " (" ?uid ")"))
    )
    (return (str-cat "Unknown Skill (" ?uid ")"))
  )
)
