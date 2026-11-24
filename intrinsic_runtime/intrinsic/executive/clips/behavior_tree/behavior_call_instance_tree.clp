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

; Execution rules for behavior-call-instances with parameterizable behavior
; trees

; ------------------------------- FUNCTIONS -----------------------------------

; Determines if a node in ?tree-id find is inside a parameterizable
; behavior tree and if yes determine the behavior-call-instance.
;
; ?tree-id itself can be a PBT or any parent tree of ?tree-id can be a PBT.
; The closest enclosing PBT is the one that ?tree-id is contained in.
;
; Args:
;   ?tree-id: Tree id that a node is running in
;
; Returns:
;   UID of the behavior-call-instance that ?tree-id is in
;   or nil, if ?tree-id is not part of a PBT.
(deffunction behavior-call-instance-find-enclosing-pbt-instance-uid (?tree-id)
  ; Base case: ?tree-id is a PBT
  (do-for-fact ((?bci behavior-call-instance))
    (eq ?bci:parameterizable-tree-id ?tree-id)
    (return ?bci:uid)
  )
  ; ?tree-id is not a PBT -> Check if it has a parent
  (do-for-fact ((?bt behavior-tree))
    (and (eq ?bt:id ?tree-id)
         (neq ?bt:parent-behavior-tree-id nil))
    (return (behavior-call-instance-find-enclosing-pbt-instance-uid
              ?bt:parent-behavior-tree-id))
  )
  ; ?tree-id is not a PBT and doesn't have a parent -> root process tree.
  (return nil)
)

; Find a resource handle in the given behavior-call.
;
; Args:
;   ?resource-reference the resource reference in the resources map to look up
;   ?pbt-behavior-call-proto: The behavior call to look the resource up in
;   ?error-msg-prefix: Generic error information to include in specific errors
;                      raised here
;
; Returns:
;   A multifield result, where the first value is TRUE on success,
;   FALSE on failure; and the second
;     - on success: The actual found handle
;     - on failure: An error message describing the reason for the error
(deffunction behavior-call-instance-find-resource-from-behavior-call
  (?resource-reference ?pbt-behavior-call-proto ?error-msg-prefix)
  ; Determine the resource handle from ?pbt-behavior-call-proto
  ; for ?resource-reference. For this the following must hold:
  ; 1. The ?pbt-behavior-call-proto exists
  (if (= ?pbt-behavior-call-proto 0) then
    (return (result-create FALSE (str-cat ?error-msg-prefix
      " The BehaviorCall instance proto was not set. Make sure the"
      " BehaviorCall is set when creating the Task node.")))
  )

  ; 2. The ?pbt-behavior-call-proto has a resource entry for
  ;    ?resource-reference
  (bind ?pbt-resource-specification-proto
    (pb-get-map-value ?pbt-behavior-call-proto "resources"
      ?resource-reference))
  (if (eq ?pbt-resource-specification-proto MISSING-FIELD) then
    (return (result-create FALSE (str-cat ?error-msg-prefix
      " The resource reference is not specified in the PBT's"
      " BehaviorCall instance proto. Make sure to parameterize this"
      " resource in the BehaviorCall when creating the Task node.")))
  )
  ; 3. The resource entry for ?resource-reference in
  ;    ?pbt-behavior-call-proto is a resource handle
  (if (neq (pb-which-oneof ?pbt-resource-specification-proto
             "resource_specification_type") handle) then
    (pb-remove ?pbt-resource-specification-proto)
    (return (result-create FALSE (str-cat ?error-msg-prefix
      " The resource reference specified in the PBT's BehaviorCall"
      " instance proto is also a reference (should be a handle)")))
  )

  (bind ?resource-handle
    (pb-get-field ?pbt-resource-specification-proto "handle"))
  (pb-remove ?pbt-resource-specification-proto)

  (return (result-create TRUE ?resource-handle))
)

; Find a resource handle in the given resources map.
;
; Args:
;   ?resource-reference the resource reference in the resources map to look up
;   ?resources: A multifield map, where every two pairwise entries are
;               ?key ?handle.
;   ?error-msg-prefix: Generic error information to include in specific errors
;                      raised here
;
; Returns:
;   A multifield result, where the first value is TRUE/FALSE and the second
;     - on success: The actual found handle
;     - on failure: An error message describing the reason for the error
(deffunction behavior-call-instance-find-resource-from-operation-resources
  (?resource-reference ?resources ?error-msg-prefix)
  (bind ?value-lookup (map-value ?resources ?resource-reference))
  (if (eq ?value-lookup MISSING-FIELD) then
    (return (result-create FALSE (str-cat ?error-msg-prefix
      " as the resource reference is not specified in the operation's"
      " resources. Make sure to set this resource in the resources field in the"
      " StartOperationRequest")))
  )
  (return (result-create TRUE ?value-lookup))
)

; Parameterizes the resources in the BehaviorCall proto instance.
;
; This is called whenever a task node is started either for executing a skill or
; a PBT. In both cases the passed in ?behavior-call-instance-proto is that of
; the task node to be executed and its resources are to be parameterized.
;
; The source of information for the resource parameterization is given
; implicitly by the ?tree-id that this BehaviorCall is in. If the ?tree-id is a
; tree within a PBT the BehaviorCall that called this PBT provides the resource
; handles. If the ?tree-id is in a process tree the operation provides the
; resource handles.
;
; It is not an error when there are no input resource handles either in a PBT or
; process tree. Only when the BehaviorCall to be parameterized actually defines
; resource references to be parameterized an error is raised.
;
; Args:
;   ?behavior-call-instance-proto: BehaviorCall proto used as the execution
;                                  instance.
;   ?tree-id: Tree ID of the tree that contains the behavior-call-instance with
;             ?behavior-call-instance-proto.
;
; Returns:
;   A multifield pair with TRUE/FALSE and as the second value
;   on success: The passed in ?behavior-call-instance-proto, which has
;               parameterized resources, i.e., only handles.
;   on failure: A message why the parameterization failed.
(deffunction behavior-call-instance-parameterize-resources
  (?behavior-call-instance-proto ?tree-id)
  ; First determine the BehaviorCall proto the is used for execution of the PBT
  ; that contains the ?behavior-call-instance-proto.
  ; This can be 0, if running in a process tree that does not define resources.
  ; In that case look up the resources specified in the operation.
  (bind ?pbt-behavior-call-proto 0)
  (bind ?op-resources (create$ ))

  ; Where the resource specification is taken from:
  ; TASK-NODE-CALL when this ?behavior-call-instance-proto is in a PBT called by
  ; a task node
  ; OPERATION when this ?behavior-call-instance-proto is in the operation tree
  ; (which is a PBT)
  (bind ?resource-context TASK-NODE-CALL)

  (bind ?pbt-behavior-call-instance-uid
    (behavior-call-instance-find-enclosing-pbt-instance-uid ?tree-id))
  (if (neq ?pbt-behavior-call-instance-uid nil) then
      (bind ?resource-context TASK-NODE-CALL)
      (do-for-fact ((?pbt-behavior-call-instance behavior-call-instance))
        (eq ?pbt-behavior-call-instance:uid ?pbt-behavior-call-instance-uid)
        (bind ?pbt-behavior-call-proto
          ?pbt-behavior-call-instance:behavior-call-instance-proto)
      )
    else
      ; ?tree-id is in a process tree
      (bind ?resource-context OPERATION)
      (do-for-fact ((?bt behavior-tree) (?op operation-envelope))
        (and (eq ?bt:id ?tree-id) (eq ?bt:operation-name ?op:name))
        (bind ?op-resources ?op:resources)
      )
  )

  ; Second update all resource keys in ?behavior-call-instance-proto that have
  ; a reference from ?pbt-behavior-call-proto resource handles.
  (foreach ?resource-key
           (pb-get-map-keys ?behavior-call-instance-proto "resources")
    (bind ?resource-specification-proto
      (pb-get-map-value ?behavior-call-instance-proto "resources"
        ?resource-key))
    (bind ?resource-specification-proto-type (pb-which-oneof
      ?resource-specification-proto "resource_specification_type"))
    (switch ?resource-specification-proto-type
      (case handle then
        ; All good, this is already a handle
        (pb-remove ?resource-specification-proto)
      )
      (case reference then
        (bind ?resource-reference
          (pb-get-field ?resource-specification-proto "reference"))
        (bind ?error-msg-prefix (str-cat "Resource parameterization"
            " failed for resource '" ?resource-key "' referring to '"
            ?resource-reference "' in skill '"
            (pb-get-field ?behavior-call-instance-proto "skill_id")
            "' running in '" ?tree-id "'"))
        (bind ?resource-lookup-result (result-create FALSE "Invalid result"))
        (if (eq ?resource-context TASK-NODE-CALL)
          then
            (bind ?resource-lookup-result
              (behavior-call-instance-find-resource-from-behavior-call
                ?resource-reference ?pbt-behavior-call-proto ?error-msg-prefix))
          else  ; ?resource-context OPERATION
            (bind ?resource-lookup-result
              (behavior-call-instance-find-resource-from-operation-resources
                ?resource-reference ?op-resources ?error-msg-prefix))
        )

        (if (not (result-ok ?resource-lookup-result)) then
          (pb-remove ?resource-specification-proto)
          (return ?resource-lookup-result)
        )

        (bind ?resource-handle (result-value ?resource-lookup-result))
        (pb-set-field ?resource-specification-proto "handle" ?resource-handle)
        (pb-set-map-value ?behavior-call-instance-proto "resources"
                          ?resource-key
                          ?resource-specification-proto)
        (pb-remove ?resource-specification-proto)
      )
    )
  )
  (return (result-create TRUE ?behavior-call-instance-proto))
)

; Parameterizes 'params' on the blackboard for a BehaviorCall proto instance.
;
; This is the main part of a PBT parameterization. Its main goal is to take the
; parameters field from the calling BehaviorCall and put it on the blackboard of
; the called PBT at the 'params' key.
;
; As the parameters field on the BehaviorCall is an Any this involves casting
; from the Any to generate a usable proto.
;
; Args:
;   ?behavior-call-instance-proto: BehaviorCall proto used as the execution
;                                  instance.
;   ?pbt-scope: Blackboard scope of the PBT to be parameterized, i.e., the PBT
;               that ?behavior-call-instance-proto calls.
;   ?op-name: Operation name for scoping the blackboard item
;
; Returns:
;   A multifield pair with TRUE/FALSE and as the second value
;   on success: empty string, ""
;   on failure: An ExtendedStatus proto id.
(deffunction behavior-call-instance-parameterize-blackboard
  (?behavior-call-instance-proto ?pbt-scope ?op-name)

  (bind ?param-proto (pb-get-field ?behavior-call-instance-proto "parameters"))
  (if (neq ?param-proto MISSING-FIELD) then
    (bind ?skill-id (pb-get-field ?behavior-call-instance-proto "skill_id"))
    ; Put the param proto on the blackboard under its actual type, not as an Any
    ; Therefore, get the respective skill-info fact to retrieve descriptor pool
    ; for casting.
    (do-for-fact ((?skill-info skill-info))
      (eq ?skill-info:skill-id ?skill-id)

      (bind ?parameter-cast-result
        (pb-cast-from-any-with-pool ?param-proto
                          ?skill-info:parameter-descriptor-pool-id
                          ?skill-info:parameter-message-name))
      (if (not (result-ok ?parameter-cast-result)) then
        (bind ?es-cast-proto (result-error ?parameter-cast-result))
        (bind ?es-proto (extended-status-create 13302 ERROR))
        (extended-status-set-message ?es-proto USER
          (str-cat "Parameterization of behavior tree for " ?skill-id
                   " failed"))
        (extended-status-add-context ?es-proto ?es-cast-proto)
        (pb-remove ?es-cast-proto)
        (pb-remove ?param-proto)
        (return (result-create FALSE ?es-proto))
      )

      (bind ?parameter-proto-id (result-value ?parameter-cast-result))
      (assert (blackboard-update
        (key "params") (scope ?pbt-scope) (operation-name ?op-name)
        (proto-id ?parameter-proto-id) (keep-on-flush FALSE)))
    )
    (pb-remove ?param-proto)
  )
  (return (result-create TRUE ""))
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-call-instance-tree-start
  "Start PBT after corresponding behavior-call-instance became selected."
  ?bci <- (behavior-call-instance (state SELECTED) (skill-type BEHAVIOR-TREE)
                                  (uid ?uid)
                                  (behavior-call-instance-proto ?bc-proto)
                                  (parameterizable-tree-id ?pbt-id)
                                  (log-id ~0))
  ?pbt <- (behavior-tree (id ?pbt-id) (state ACCEPTED)
                         (operation-name ?op-name)
                         (blackboard-scope ?pbt-scope))
 =>
  (printout debug
    "Starting PBT for " (behavior-call-instance-tostring ?uid) crlf)

  (bind ?parameterization-result
    (behavior-call-instance-parameterize-blackboard ?bc-proto ?pbt-scope
                                                    ?op-name))
  (if (not (result-ok ?parameterization-result)) then
    (modify ?bci (state FAILED)
                 (extended-status-proto-id
                   (result-error ?parameterization-result)))
    (return)
  )

  (bind ?parent-span-id ?*TRACING-INVALID-SPAN-ID*)
  (do-for-fact ((?task-node behavior-tree-node))
    (eq ?task-node:behavior-call-instance-uid ?uid)
    (bind ?parent-span-id ?task-node:span-reference-id)
  )
  (bind ?pbt-tree-span
    (tracing-cc-start-tree-span ?pbt-id ?parent-span-id))

  (modify ?pbt (state RUNNING) (span-reference-id ?pbt-tree-span))
  (modify ?bci (state RUNNING))
)

(defrule behavior-call-instance-tree-succeeded
  "Observe pbt state success"
  ?bci <- (behavior-call-instance
            (state RUNNING|SUSPENDING|CANCELLATION-REQUESTED|CANCELING)
            (skill-type BEHAVIOR-TREE)
            (uid ?uid)
            (behavior-call-instance-proto ?bc-proto)
            (parameterizable-tree-id ?pbt-id))
  (behavior-tree (id ?pbt-id) (state SUCCEEDED) (operation-name ?op-name)
                         (blackboard-scope ?pbt-scope)
                         (return-value-expression ?return-expression))
  ?node <- (behavior-tree-node (behavior-call-instance-uid ?uid)
                               (tree-id ?containing-tree-id))
  ?containing-tree <- (behavior-tree (id ?containing-tree-id)
                                     (blackboard-scope ?containing-scope))
 =>
  (printout debug "PBT " (behavior-call-instance-tostring ?uid)
            " has SUCCEEDED" crlf)

  (bind ?return-value-key (pb-get-field ?bc-proto "return_value_name"))
  (bind ?outcome SUCCEEDED)
  (bind ?extended-status-proto-id 0)
  (if (and (neq ?return-expression "") (neq ?return-value-key "")) then
    (bind ?skill-id (pb-get-field ?bc-proto "skill_id"))
    (bind ?return-value-type "")
    (bind ?return-value-pool-id 0)
    (do-for-fact ((?skill-info skill-info))
      (eq ?skill-info:skill-id ?skill-id)
      (bind ?return-value-pool-id ?skill-info:return-value-descriptor-pool-id)
      (bind ?return-value-type ?skill-info:return-value-message-name)
    )
    (bind ?evaluate-return-value-result
      (cel-eval-to-proto-with-type ?return-expression
           ?return-value-type ?return-value-pool-id ?pbt-scope ?op-name))
    (if (result-ok ?evaluate-return-value-result)
      then
        ; This is the actual result of the return value computation:
        ; Put the result evaluated in pbt scope in the scope of the
        ; containing tree.
        (assert (blackboard-update (key ?return-value-key)
                                   (scope ?containing-scope)
                                   (operation-name ?op-name)
                                   (proto-id (result-value
                                     ?evaluate-return-value-result))))
      else
        (bind ?extended-status-evaluation-proto-id
          (result-error ?evaluate-return-value-result))

        (bind ?extended-status-proto-id (extended-status-create 13303 ERROR))
        (extended-status-set-message ?extended-status-proto-id USER
          (str-cat "Failed to evaluate tree return value for key "
                   ?return-value-key))
        (extended-status-add-context ?extended-status-proto-id
                                     ?extended-status-evaluation-proto-id)
        (pb-remove ?extended-status-evaluation-proto-id)
        (extended-status-set-related-to ?extended-status-proto-id
                                        ?containing-tree ?node)
        (bind ?outcome FAILED)
    )
  )

  (blackboard-remove "params" ?pbt-scope ?op-name)

  (modify ?bci (state ?outcome)
               (extended-status-proto-id ?extended-status-proto-id))
)

(defrule behavior-call-instance-tree-failed
  "Observe PBT state for failure"
  ?bci <- (behavior-call-instance
            (state ?state&RUNNING|SUSPENDING|CANCELLATION-REQUESTED|CANCELING)
            (skill-type BEHAVIOR-TREE)
            (uid ?uid)
            (behavior-call-instance-proto ?bc-proto)
            (parameterizable-tree-id ?pbt-id))
  (behavior-tree (id ?pbt-id) (state FAILED) (operation-name ?op-name)
                 (blackboard-scope ?pbt-scope) (root ?root-id))
  (behavior-tree-node (id ?root-id) (tree-id ?pbt-id)
                      (extended-status-proto-id ?es-proto))
 =>
  (blackboard-remove "params" ?pbt-scope ?op-name)

  (bind ?skill-id (pb-get-field ?bc-proto "skill_id"))

  (printout t (str-cat "PBT with skill id '" ?skill-id "' failed") crlf)
  (modify ?bci (state FAILED) (extended-status-proto-id (pb-clone ?es-proto)))
)

(defrule behavior-call-instance-tree-suspend
  "Suspend tree, when this instance is suspending"
  (declare (salience ?*SALIENCE-SUSPENDING*))
  ?bci <- (behavior-call-instance (state SUSPENDING) (skill-type BEHAVIOR-TREE)
                                  (uid ?uid)
                                  (parameterizable-tree-id ?pbt-id))
  (behavior-tree (id ?pbt-id) (state RUNNING))
 =>
  (printout debug "Suspending PBT " (behavior-call-instance-tostring ?uid) crlf)

  (if (not (behavior-tree-suspend ?pbt-id)) then
    (modify ?bci (state FAILED))
  )
)

(defrule behavior-call-instance-tree-suspended
  "If the PBT is suspended, this instance is suspended"
  ?bci <- (behavior-call-instance (state SUSPENDING) (skill-type BEHAVIOR-TREE)
                                  (uid ?uid)
                                  (parameterizable-tree-id ?pbt-id))
  (behavior-tree (id ?pbt-id) (state SUSPENDED))
 =>
  (printout debug "PBT " (behavior-call-instance-tostring ?uid)
            " is SUSPENDED" crlf)

  (modify ?bci (state SUSPENDED))
)

(defrule behavior-call-instance-tree-canceled
  "If the PBT is canceled, this instance was cancelled"
  ?bci <- (behavior-call-instance (state CANCELING) (skill-type BEHAVIOR-TREE)
                                  (uid ?uid) (parameterizable-tree-id ?pbt-id))
  (behavior-tree (id ?pbt-id) (state CANCELED))
 =>
  (printout debug "PBT " (behavior-call-instance-tostring ?uid)
            " was CANCELLED" crlf)

  (modify ?bci (state CANCELED))
)

(defrule behavior-call-instance-tree-resume
  "If the PBT is suspended and this instance is running, resume the PBT"
  ?bci <- (behavior-call-instance (state RUNNING) (skill-type BEHAVIOR-TREE)
                                  (uid ?uid)
                                  (parameterizable-tree-id ?pbt-id))
  ?pbt <- (behavior-tree (id ?pbt-id) (state SUSPENDED))
 =>
  (printout debug "Resuming PBT " (behavior-call-instance-tostring ?uid) crlf)

  (modify ?pbt (state RUNNING))
)

(defrule behavior-call-instance-tree-cancel-no-running-tree
  "Cancels the instance if the PBT is not running yet."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?bci <- (behavior-call-instance (state CANCELLATION-REQUESTED)
                                  (skill-type BEHAVIOR-TREE) (uid ?uid)
                                  (parameterizable-tree-id ?pbt-id))
  (behavior-tree (id ?pbt-id) (state ACCEPTED))
 =>
  (printout debug "Cancelled behavior-call-instance "
                  (behavior-call-instance-tostring ?uid)
                  " before it was run." crlf)
  (modify ?bci (state CANCELED))
)

(defrule behavior-call-instance-tree-cancel-tree
  "Cancels the instance's in-flight PBT."
  (declare (salience ?*SALIENCE-CANCELING*))
  ?bci <- (behavior-call-instance (state CANCELLATION-REQUESTED)
                                  (skill-type BEHAVIOR-TREE) (uid ?uid)
                                  (parameterizable-tree-id ?pbt-id))
  ?pbt <- (behavior-tree (id ?pbt-id) (state RUNNING|SUSPENDING|SUSPENDED))
 =>
  (if (not (behavior-tree-cancel ?pbt-id))
    then
      (modify ?bci (state FAILED))
    else
      (printout debug "Canceling behavior-call-instance "
                      (behavior-call-instance-tostring ?uid) crlf)
      (modify ?bci (state CANCELING))
  )
)
