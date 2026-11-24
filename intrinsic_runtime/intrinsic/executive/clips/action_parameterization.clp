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

; Functions for action parameterization via blackboard expressions

; ----------------------------------- FUNCTIONS -------------------------------

; Performs parameter assignment on a given behavior call proto as a helper
; function once a skill-info is available.
;
; Args:
;   ?proto-id: refers to a BehaviorCall proto that holds parameters and assignments
;   ?skill-info: Fact-address of a skill-info that contains param descriptors.
;   ?blackboard-scope: The scope to use for any blackboard keys referred in the
;     assignment-field-name to be assigned
;   ?operation-name: Name of the operation containing the blackboard scope
;
; The function makes sure that if a proto is set in ?param-field-name and
; ?assignment-field-name is not empty, all assignments will be applied to the
; proto in ?param-field-name.
;
; Returns:
;   A multifield of size 2 with the first as TRUE or FALSE.
;   If TRUE the parameterization was successful and the ?proto-id's
;   ?param-field-name is updated with the information from ?assignment-field-name
;   via the blackboard. In this case the second value is an empty string.
;   If FALSE, the parameter assignment was not successful and the
;   ?param-field-name cannot be relied on. In this case the second value is an
;   ExtendedStatus proto id.
(deffunction action-parameterization-parameter-assignment (
    ?proto-id ?skill-info ?blackboard-scope ?operation-name)
  (bind ?param-descriptor-pool-id
    (fact-slot-value ?skill-info parameter-descriptor-pool-id))
  (if (= ?param-descriptor-pool-id 0) then
    (bind ?es-proto (extended-status-create 13701 ERROR))
    (extended-status-set-message ?es-proto USER
        (str-cat "Missing Parameter pool. This should not happen."))
    (return
     (result-create FALSE ?es-proto))
  )

  ; If there are no parameters, we cannot assign to anything.
  ; This usually only happens in tests.
  (if (not (pb-has-field ?proto-id "parameters"))
    then (return (result-create TRUE ?skill-info ""))
  )

  (bind ?param-message-name
    (fact-slot-value ?skill-info parameter-message-name))
  ; Perform the parameter assignment
  (bind ?assign-result (cel-proto-assign ?proto-id ?param-descriptor-pool-id
                                         ?param-message-name
                                         ?blackboard-scope ?operation-name))

  (if (not (result-ok ?assign-result)) then
    (return (result-create FALSE (result-error ?assign-result)))
  )

  (return (result-create TRUE ""))
)

; Performs parameter assignment for a behavior call proto.
;
; A skill-info is required as the skill-info contains the necessary proto
; descriptors. The function automatically handles retrieving the
; descriptor pools from the skill-info.
;
; Args:
;   ?behavior-call-proto-id: a proto id representing the BehaviorCall proto.
;     The proto will be modified by applying action parameterizations via the
;     blackboard.
;   ?blackboard-scope: The scope to use for any blackboard keys referred in the
;     BehaviorCall to be assigned
;   ?operation-name: Name of the operation containing the blackboard scope
;
; Returns:
;   A multifield pair with TRUE/FALSE and on
;   TRUE, if the proto was correctly assigned or did not require assignment;
;   FALSE otherwise. In this case the ?behavior-call-proto-id and cannot be
;   relied on and the second value of the return value is an ExtendedStatus
;   proto id.
(deffunction action-parameterization-assign-behavior-call-proto
    (?behavior-call-proto-id ?blackboard-scope ?operation-name)
  (if (eq (pb-get-repeated-length ?behavior-call-proto-id "assignments") 0)
    then
      (return (result-create TRUE ""))
  )

  (bind ?skill-id (pb-get-field ?behavior-call-proto-id "skill_id"))

  ; At this point there is a BehaviorCall proto with assignments. Thus there
  ; must exist a skill-info with the ?skill-id that contains descriptor
  ; information for the proto paramters.
  (do-for-fact ((?skill-info skill-info))
    (eq ?skill-id ?skill-info:skill-id)

    (bind ?param-assign-result (action-parameterization-parameter-assignment
        ?behavior-call-proto-id ?skill-info ?blackboard-scope ?operation-name))
    (return ?param-assign-result)
  )

  (bind ?es-proto (extended-status-create 15960 ERROR))
  (extended-status-set-message ?es-proto USER
    (str-cat "Could not find skill information to assign skill " ?skill-id))
  (return (result-create FALSE ?es-proto))
)

; Performs parameter assignment for an AnyWithAssignments.
;
; This produces an Any proto that represents the proto within the
; AnyWithAssignments where all specified assignments have been applied.
; The pool-id is required to pack/unpack the Any proto to a specific proto and
; back.
;
; Args:
;   ?any-with-assignments-proto: The AnyWithAssignments to assign.
;     The proto will not be modified
;   ?blackboard-scope: The scope to use for any blackboard keys referred in the
;     AnyWithAssignments to be assigned
;   ?operation-name: Name of the operation containing the blackboard scope
;   ?parameter-message-name: Expected name of the message in the Any
;
; Returns:
;   A multifield pair with TRUE/FALSE and as the second parameter:
;   On TRUE: An Any proto owned by the caller with all required assignments
;            applied.
;   On FALSE: An ExtendedStatus proto owned by the caller explaining why the
;             assignment failed.
(deffunction action-parameterization-assign-any-with-assignments
    (?any-with-assignments-proto ?blackboard-scope ?operation-name ?pool-id
     ?parameter-message-name)
  (bind ?any-w-a-clone (pb-clone ?any-with-assignments-proto))
  (bind ?assign-result
    (cel-proto-assign ?any-w-a-clone ?pool-id
                      ?parameter-message-name
                      ?blackboard-scope ?operation-name))

  (if (not (result-ok ?assign-result)) then
    (pb-remove ?any-w-a-clone)
    (return ?assign-result)
  )

  (bind ?any-proto 0)
  (if (not (pb-has-field ?any-w-a-clone "proto"))
    then
      (bind ?any-proto (pb-create "google.protobuf.Any"))
    else
      (bind ?any-proto (pb-get-field ?any-w-a-clone "proto"))
  )
  (pb-remove ?any-w-a-clone)
  (return (result-create TRUE ?any-proto))
)
