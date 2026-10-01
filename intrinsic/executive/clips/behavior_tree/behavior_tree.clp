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

; A Behavior Tree models the behavior of a robot in a tree structure. A node in
; the tree represents a specific behavior that may be built by composing
; sub-behaviors. A leaf node is a task that executes a single action. Inner
; nodes describe the composition of behaviors, such as sequential execution,
; parallel execution, and selecting one behavior from several options.
; We adopt a version of Behavior Trees similar to the Unreal Engine [1] with
; some inspiration from academic literature [2], with the following
; differences:
; - If a node is running, the control always stays within that loop, i.e.,
;   there are no cross-tree jumps to other nodes that are not ancestors of
;   a currently running node. This is similar to memory nodes in academic
;   literature [2]. While this reduces the reactivity of the system, it
;   provides a stricter control flow and avoids inadvertent re-execution of
;   parts of the tree that have already succeeded.
; - Conditions are not leaf nodes but always attached to a node, similar to
;   decorators in the Unreal Engine [1].
; - A task always executes a plan-action. A task fails
;   if the execution of the action fails.
;
;
; Requires:
; - behavior_tree/tracing.clp: Functions to start and end node traces.

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate behavior-tree
  (slot id (type SYMBOL) (default ?NONE))
  (slot name (type STRING))

  ; The ID of the plan that is created by executing this behavior tree
  (slot plan-id (type SYMBOL) (default ?NONE))

  ; The operation this behavior tree is associated to
  (slot operation-name (type STRING) (default ?NONE))

  ; The ID of a parent (enclosing) Behavior Tree. For example, for a BT that is
  ; a sub-tree, the slot is set to the ID of the tree of which the sub-tree is
  ; a part of. The slot is nil iff the fact is for a top-level BT, i.e., the
  ; process or system tree.
  (slot parent-behavior-tree-id (type SYMBOL))

  ; ACCEPTED:   The initial state of the behavior tree
  ; RUNNING:    The behavior tree is currently running, i.e., its root node has
  ;             been started and has not completed yet. A behavior tree in this
  ;             state may be canceled.
  ; CANCELING:  Cancellation of the tree's root node has been triggered; waiting
  ;             for the root node to complete.
  ; SUSPENDING: Waiting for the RUNNING nodes to finish to pause execution. A
  ;             behavior tree in this state may be canceled.
  ; SUSPENDED:  Tree is not executed further until resumed (transition to
  ;             RUNNING). A behavior tree in this state may be canceled.
  ; SUCCEEDED:  The execution of the tree's root node has succeeded.
  ; FAILED:     The execution of the tree's root node has failed.
  ; CANCELED:   The behavior tree has completed user's cancellation request.
  (slot state (type SYMBOL) (allowed-values ACCEPTED RUNNING CANCELING
                                            SUSPENDING SUSPENDED
                                            ; terminal states
                                            SUCCEEDED FAILED CANCELED)
                            (default ACCEPTED))

  ; Path to the BehaviorTree proto in the RunMetadata proto to update state
  ; information. Always updated. Example values:
  ;  - "behavior_tree" for top-level tree (with parent-behavior-tree-id set to nil)
  ;  - "behavior_tree.root.task.called_tree_state"
  (slot run-metadata-proto-path (type STRING))

  ; This tracks the state slot and updates the run metadata proto if they get out of
  ; sync, i.e., when the state changes.
  (slot run-metadata-proto-state (type SYMBOL) (default ACCEPTED))

  (slot root (type INTEGER) (default ?NONE))

  ; The node id of a node in the tree that the tree is started with. It is the
  ; start of the execution of the tree and determines its state. Usually this
  ; equals root, but can be set differently for debugging.
  (slot start-node-id (type INTEGER) (default ?NONE))

  ; The scope that any blackboard key in this tree refers to.
  (slot blackboard-scope (type STRING) (default ?NONE))

  ; For parameterizable behavior trees: The CEL expression string to evaluate to
  ; determine its return value.
  (slot return-value-expression (type STRING))

  ; This Log ID for related log-context generation.
  ; 0 means that the behavior tree has not yet been logged.
  (slot log-id (type INTEGER))

  ; The span-reference-id for behavior tree tracing of this tree.
  (slot span-reference-id (type INTEGER))
)

(deftemplate behavior-tree-condition
  (slot id (type SYMBOL) (default ?NONE))
  (slot parent-id (type SYMBOL))

  ; The id of the behavior tree this node belongs to.
  (slot tree-id (type SYMBOL) (default ?NONE))
  (slot node-id (type INTEGER) (default ?NONE))
  ; When part of a of a compound condition this notes the index in the list of
  ; conditions; must be unique within a compound condition.
  ; Indexing starts at 1, 0 means the value is unset.
  (slot compound-condition-index (type INTEGER) (default 0))

  (slot type (type SYMBOL) (allowed-values AND OR NOT SUB-TREE
                                           BLACKBOARD-CEL-EXPRESSION
                                           EXTENDED-STATUS-MATCH))

  ; A condition goes through a linear sequence of states. A condition
  ; implementation is free to leave out states which are not necessary, but they
  ; may not be traversed in a different order. A condition MUST only commence
  ; when a condition is in the START state and MUST end in either the FINISHED
  ; or the ERROR state. In the FINISHED state satisfied MUST be either TRUE, or
  ; FALSE; it CANNOT be UNKNOWN.
  ;
  ; - ACCEPTED: importer has recognized the condition
  ; - START: the condition has to start evaluation (e.g., for a decorator
  ;          condition when the node has been selected).
  ; - EVALUATING: the condition implementation has started evaluation
  ; - SUSPENDING: the condition is suspending.
  ; - SUSPENDED: the condition has been suspended, e.g., a subtree condition's
  ;              tree has suspended. Will be resumed when enclosing tree
  ;              resumes.
  ; - CLEANUP: Evaluation is done, the condition implementation may still
  ;            perform cleanup operations.
  ; - FINISHED: condition evaluation has completed, the satisfaction result can
  ;             be read safely.
  ; - ERROR: some error occurred that prevents an accurate satisfaction
  ;          assessment. This error requires that the BT is aborted.
  ; - CANCELING: the condition has been requested to cancel operations.
  ; - CANCELED: condition confirmed that cancellation has been finished.
  (slot state (type SYMBOL)
        (allowed-values ACCEPTED START EVALUATING SUSPENDING SUSPENDED CLEANUP
                        FINISHED ERROR CANCELING CANCELED))

  ; Path to the BehaviorTree.Condition proto in the RunMetadata proto to update
  ; state information. If empty, the proto will not be updated. Example value:
  ; "behavior_tree.root.selector.branches[0].condition".
  (slot run-metadata-proto-path (type STRING))

  ; This tracks the state slot and updates the run metadata proto if they get out of
  ; sync, i.e., when the state changes.
  (slot run-metadata-proto-state (type SYMBOL) (default ACCEPTED))

  (slot sub-tree-id (type SYMBOL))
  (slot blackboard-key (type STRING))
  (slot blackboard-cel-expression (type STRING))
  (slot blackboard-cel-expression-id (type INTEGER))

  ; Configuration for EXTENDED-STATUS-MATCH condition type:
  ; - blackboard-key: expect ExtendedStatus proto in this blackboard item
  ; - component: if non-empty match component of status
  ; - code: if non-zero match code of status
  (slot extended-status-match-blackboard-key (type STRING))
  (slot extended-status-match-component (type STRING))
  (slot extended-status-match-code (type INTEGER))

  (slot satisfied (type SYMBOL)
        (allowed-values UNKNOWN FALSE TRUE) (default UNKNOWN))

  ; The span-reference-id for tracing of this condition.
  (slot span-reference-id (type INTEGER))

  ; An actual ExtendedStatus proto created and propagated on failure.
  ; This is propagated if the condition finishes in ERROR. If the condition
  ; ends in FINISHED but unsatisfied, the extended status MAY be used if the
  ; enclosing node (from decorator or node-type specific field) fails.
  (slot extended-status-proto-id (type INTEGER))
)

(deftemplate behavior-tree-node
  ; The id of the node, must be unique within the tree.
  ; This must be a uint32 (to simplify handling in frontend).
  (slot id (type INTEGER) (default ?NONE))

  ; The id of the parent. Must be 0 if this is the root of the tree.
  (slot parent-id (type INTEGER))

  ; The id of the behavior tree this node belongs to.
  (slot tree-id (type SYMBOL) (default ?NONE))

  ; The index of the node if the parent has multiple children, must be unique
  ; within the parent context. 0 means the value is unset.
  (slot index (type INTEGER) (default 0))

  ; TASK:     Tasks are the leaf nodes of a BT, they specify an action to
  ;           execute.
  ; FAIL:     Fail nodes are leaf nodes that can be used to throw an execution
  ;           failure. They are typically used in combination with failure
  ;           handling constructs.
  ; DEBUG:    Debug operations for the tree.
  ; SEQUENCE: Children are specified as an ordered list. They are executed
  ;           exactly in the specified order. Any failure of a child node will
  ;           propagate and cause the sequence to fail, successful completion
  ;           of all children makes the sequence succeed.
  ; PARALLEL: All children are executed in parallel. The failure of any node
  ;           fails the root (after waiting for all other still running
  ;           children to complete [TODO(b/170385511)]). If all children
  ;           complete successfully, the root succeeds.
  ; SELECTOR: Children are specified as an ordered list. They are tried in the
  ;           given order and the node succeeds as soon as any child succeeds.
  ;           The root fails if any child fails to execute, or once all children
  ;           failed on the condition. Effectively, the node advances to the
  ;           next node only on a condition failure.
  ; FALLBACK: Children are specified as an ordered list. They are tried in the
  ;           given order and the node succeeds as soon as any child succeeds.
  ;           The root fails once all children failed.
  ; RETRY:    This node has a single child which is tried up to a maximum
  ;           number of times. If the child succeeds, the root succeeds. If the
  ;           child fails for the maximum number of tries the root fails.
  ; BRANCH:   Determine the 'if' condition, if is satisfied, execute the THEN
  ;           child, else execute the ELSE child.
  ; LOOP:     In a loop, execute the DO child for as long as the WHILE condition
  ;           is satisfied. This can happen at most max-times times. If
  ;           max-times is set to 0, the counter is ignored. If the condition is
  ;           not set, assumed to always be satisfied, such that the DO child is
  ;           always executed. To implement an infinite loop, set max-times to 0
  ;           and omit the DO child (the loop will still exit when the child
  ;           fails).
  ; SUB-TREE: Execute the sub-tree, the outcome of the node is that of the tree.
  ; DATA: Create, updare, or remove data in the blackboard.
  (slot type (type SYMBOL)
             (allowed-values TASK FAIL SEQUENCE PARALLEL SELECTOR FALLBACK RETRY
                             BRANCH LOOP SUB-TREE DATA DEBUG)
             (default ?NONE))

  ; Optional: (display) name of the node
  (slot name (type STRING))

  ; These states need to be kept in sync with the BehaviorTree.Node.State proto
  ; enum.
  ; ACCEPTED:  The initial state of the node, the node can be selected
  ; SELECTED:  The node has been selected for execution and needs to be
  ;            processed depending on its type. For a TASK node, this triggers
  ;            action execution. If a node is selected and its condition is not
  ;            satisfied, it immediately switches to FAILED.
  ; EVALUATING-CONDITION: The node has a condition decorator which is currently
  ;            being evaluated for its truth value.
  ; CANCELING-CONDITION: The node has a condition decorator which is currently
  ;            being canceled.
  ; READY:     Condition (if any) has been evaluated and was satisfied, node is
  ;            ready to be executed.
  ; RUNNING:   The node is currently being executed. For all composite node
  ;            types, this means at least one child has been selected and may be
  ;            running.  For a task node, this means a plan-action has been
  ;            selected and may be running. A node in this state may be
  ;            canceled.
  ; CANCELING: The node is currently being canceled. The node transitions from
  ;            this state to FAILED if cancellation finishes, or either FAILED
  ;            or SUCCEEDED if node execution completes before the cancellation
  ;            finishes.
  ; SUSPENDED: Sub-tree nodes can enter this state when the parent tree is being
  ;            suspended. A node in this state may be canceled.
  ; SUCCEEDED: Execution of the node has terminated successfully.
  ; FAILED:    Execution failed, e.g., because the execution of a plan-action
  ;            failed.
  ; CANCELED:  The behavior tree node has completed user's cancellation request.
  (slot state (type SYMBOL) (default ACCEPTED)
        (allowed-values ACCEPTED SELECTED READY RUNNING
                        EVALUATING-CONDITION CANCELING-CONDITION
                        CANCELING SUSPENDED SUCCEEDED FAILED CANCELED))

  ; Path to the BehaviorTree.Node proto in the RunMetadata proto to update
  ; state information. If empty, the proto will not be updated. Example values:
  ;  - "behavior_tree.root"
  ;  - "behavior_tree.root.sequence.children[0]"
  ;  - "behavior_tree.root.task.called_tree_state.root"
  (slot run-metadata-proto-path (type STRING))

  ; This tracks the state slot and updates the run metadata proto if they get out of
  ; sync, i.e., when the state changes.
  (slot run-metadata-proto-state (type SYMBOL) (default ACCEPTED))
  ; State that this node was set to as part of a recovery operation. This is
  ; typically NONE when the node state wasn't set by recovery, but by normal
  ; execution.
  (slot run-metadata-proto-recovered-state (type SYMBOL) (default NONE)
        (allowed-values ACCEPTED SELECTED READY RUNNING SUCCEEDED FAILED NONE))
  ; This tracks the iteration counts for loop or retry nodes
  ; (loop-num-times/retry-num-tries) and updates the run metadata proto if they
  ; get out of sync, i.e., when the num-* slots change.
  (slot run-metadata-proto-num-iterations (type INTEGER) (default 0))

  ; When suspending, record from which state the node suspended in order to
  ; resume from there.
  (slot suspended-from-state (type SYMBOL))

  ; The general failure reason, i.e.:
  ; - CONDITION: the condition decorator was not satisfied
  ; - EXECUTION: the failure for the node matched
  ; - UNKNOWN: when failure has not been determined (yet)
  (slot failure-reason (type SYMBOL)
        (allowed-values UNKNOWN CONDITION EXECUTION) (default UNKNOWN))

  ; Optional: failure message to be used in the FAIL node.
  (slot failure-message (type STRING))

  ; State for step-wise execution:
  ; - NONE: nothing has been done for step-wise execution (not enabled or node
  ;         not reached, yet).
  ; - PROCESSED: step-wise execution is or was enabled and suspend has been
  ;              initiated at this specific node
  ; - STEP-THROUGH: node has been marked not to stop there are STEP-WISE
  ;                 execution, i.e., the resume mode NEXT was chosen at this
  ;                 node or some parent node (direct or indirect).
  (slot stepwise-state (type SYMBOL)
                       (allowed-values NONE PROCESSED STEP-THROUGH)
                       (default NONE))

  (slot breakpoint-type (type SYMBOL) (allowed-values NONE BEFORE AFTER))
  (slot run-metadata-proto-breakpoint-type (type SYMBOL) (allowed-values NONE BEFORE AFTER)
                                    (default NONE))
  (slot breakpoint-triggered (type SYMBOL) (allowed-values FALSE TRUE))

  ; If disabled, a node will not be executed, but transitions to the state given
  ; in execution-mode-result-state.
  (slot execution-mode (type SYMBOL) (allowed-values NORMAL DISABLED)
                       (default NORMAL))
  (slot run-metadata-proto-execution-mode (type SYMBOL) (allowed-values NORMAL DISABLED)
                                   (default NORMAL))
  ; Resulting state for DISABLED nodes. If AUTO the state is automatically
  ; determined based on a node's parent to transition the node to a state that
  ; would make it appear as if the node was skipped, e.g., SUCCEEDED in a
  ; sequence or FAILED in a fallback.
  (slot execution-mode-result-state (type SYMBOL)
                                    (allowed-values AUTO SUCCEEDED FAILED)
                                    (default AUTO))
  (slot run-metadata-proto-execution-mode-result-state (type SYMBOL)
                                                (allowed-values AUTO SUCCEEDED FAILED)
                                                (default AUTO))

  ; Optional: for a node of type SUB_TREE (which corresponds to a Proto BT
  ; node_type sub_tree), the name of the subtree is stored in this slot.
  (slot sub-tree-id (type SYMBOL))      ; references behavior-tree

  ; Optional: ID of a behavior-tree-condition that must be satisfied for this
  ; node to succeed. This is the decorator condition.
  (slot condition-id (type SYMBOL))     ; references behavior-tree-condition

  ; Reference to the behavior-tree-condition representing "if" in a branch node
  (slot branch-if-id (type SYMBOL))     ; references behavior-tree-condition
  (slot branch-then-id (type INTEGER))  ; references behavior-tree-node
  (slot branch-else-id (type INTEGER))  ; references behavior-tree-node

  ; Slots specific to DEBUG nodes.
  (slot debug-suspend (type SYMBOL) (allowed-values FALSE TRUE))
  (slot debug-resume-state (type SYMBOL) (allowed-values SUCCEEDED FAILED))

  ; Specific fields for LOOP nodes.
  ; A loop node can be a count loop, iterating up to loop-max-times, a while
  ; loop iterating as long as loop-while-id holds, or a foreach loop, which
  ; iterates over either
  ; - given fixed loop-for-each-input-protos that are available during the
  ; lifetime of the loop node in loop-for-each-loop protos as unpacked Anys.
  ; - dynamically created loop protos via loop-for-each-generator-expression. In
  ; this case for each execution of the loop node the loop-for-each-loop-protos
  ; are regenerated from the expression before the first iteration.
  (slot loop-mode (type SYMBOL) (default COUNT)
                  (allowed-values COUNT WHILE FOR-EACH))

  ; Slots relevant for while loops
  ; Reference to the behavior-tree-condition representing "while" in a loop node
  (slot loop-while-id (type SYMBOL))
  ; Maximum number of execution times for count and while loops.
  (slot loop-max-times (type INTEGER))

  ; Slots relevant for foreach loops
  ; The multifield of protos to loop over. These are the actual unpacked
  ; (non-Any) protos.
  (multislot loop-for-each-loop-protos (type INTEGER))
  ; The blackboard key that the currently looped over proto will be put in
  (slot loop-for-each-value-blackboard-key (type STRING))
  ; The user-specified list of any protos to loop over. These will be unpacked
  ; into loop-for-each-loop-protos.
  (multislot loop-for-each-input-protos (type INTEGER))
  ; The generator expression that is used to create loop-for-each-loop-protos.
  ; This can either point to a list of protos or to an AnyList proto. In the
  ; latter case upon generation the AnyList's items are unpacked individually
  ; into loop-for-each-loop-protos.
  (slot loop-for-each-generator-expression (type STRING))
  (slot loop-for-each-generator-expression-id (type INTEGER))

  ; The child node to loop over
  (slot loop-do-id (type INTEGER))
  ; Current number of execution times.
  (slot loop-num-times (type INTEGER))
  ; blackboard key defining where to write the loop counter
  (slot loop-counter-blackboard-key (type STRING))

  ; For a RETRY node the current and maximum number of tries.
  (slot retry-num-tries (type INTEGER) (default 0))
  (slot retry-max-tries (type INTEGER) (default 0))
  ; blackboard key defining where to write the tries counter
  (slot retry-counter-blackboard-key (type STRING))
  (slot retry-child-id (type INTEGER))  ; references behavior-tree-node
  (slot retry-recovery-id (type INTEGER))  ; references behavior-tree-node

  ; For allowed-values see task_type options in TaskNode proto.
  (slot task-type (type SYMBOL) (allowed-values NOT-SET
                                                CALL-BEHAVIOR EXECUTE-CODE)
                  (default NOT-SET))
  ; For a TASK node, the UID of the prototype action. When the node is
  ; selected, this action will be copied and added to the plan.
  ; Ignored if the node type is not TASK.
  (slot task-action-prototype-uid (type SYMBOL))  ; references plan-action
  ; The actual action being executed
  (slot task-action-uid (type SYMBOL))            ; references plan-action
  ; The behavior call associated with a TASK node
  (slot behavior-call-instance-uid (type SYMBOL)) ; references
                                                  ; behavior-call-instance

  (slot data-blackboard-key (type STRING))
  (slot data-operation (type SYMBOL)
        (allowed-values NONE CREATE-OR-UPDATE REMOVE))
  (slot data-create-update-proto (type INTEGER))

  ; The span-reference-id for behavior tree tracing of this node.
  (slot span-reference-id (type INTEGER))

  ; Span for canceling node (how long did cancellation take?)
  (slot canceling-span-reference-id (type INTEGER))

  ; Span for suspended node
  (slot suspend-span-reference-id (type INTEGER))

  ; sub-spans for a node that has multiple iterations in itself
  ; Only set for LOOP and RETRY nodes
  (slot iteration-span-reference-id (type INTEGER))

  ; sub-span that is active, when this node is evaluating a decorator condition
  (slot condition-span-reference-id (type INTEGER))

  ; References an extended-status fact that is to be emitted on failure
  ; Configuration from decorator for an (optional) static extended status to
  ; emit on failure and an (optional) blackboard key where to store such
  ; information. If not stored it's just used for propagation.
  (slot on-failure-emit-extended-status-proto-id (type INTEGER))
  (slot on-failure-emit-extended-status-blackboard-key (type STRING))

  ; An actual ExtendedStatus proto created and propagated on failure.
  (slot extended-status-proto-id (type INTEGER))
)

; ---------------------------- FORWARD DECLARATIONS ----------------------------

(deffunction behavior-tree-node-reset (?node ?operation-name))
(deffunction behavior-tree-node-reset-internal (?node ?reset-child-nodes ?operation-name ?keep-counters))
(deffunction behavior-tree-delete (?id))
(deffunction behavior-tree-reset (?tree-id ?keep-counters))

; Defined in behavior_tree/loop_node.clp
(deffunction behavior-tree-loop-node-update-counter (?node ?blackboard-counter-value))
; Defined in behavior_tree/retry_node.clp
(deffunction behavior-tree-retry-node-update-counter (?node ?blackboard-counter-value))

; Defined in behavior_tree/tracing.clp
(deffunction tracing-end-canceling-span (?node))
(deffunction tracing-end-canceling-span-failure (?node ?status-code ?status-msg))
(deffunction tracing-start-canceling-span (?node))

; Defined in behavior-tree/behavior_call_instance.clp
(deffunction behavior-call-instance-reset (?uid ?keep-counters))

; Defined in behavior_tree/state_proto_update.clp
(deffunction run-metadata-proto-update-field
  (?run-metadata-proto-path ?value ?operation-name))
(deffunction run-metadata-proto-clear-field
  (?run-metadata-proto-path ?operation-name))
(deffunction run-metadata-proto-set-map-value
  (?run-metadata-proto-path ?key ?value ?operation-name))
(deffunction run-metadata-proto-reset-task-node-execution-info
  (?run-metadata-proto-path ?task-type ?operation-name))

; Defined in behavior_tree/behavior_call_instance.clp
(deffunction behavior-call-instance-remove (?uid))

; Defined in behavior-tree/code_execution_instance.clp
(deffunction code-execution-instance-reset (?operation-name ?tree-id ?node-id))
(deffunction code-execution-instance-remove (?operation-name ?tree-id ?node-id))

; --------------------------------- FUNCTIONS ---------------------------------

(deffunction behavior-tree-get-log-id (?tree-id)
  (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?tree-id) (return ?bt:log-id))
  (return 0)
)

(deffunction behavior-tree-update-counters
  (?key ?scope ?operation-name ?blackboard-counter-value
   ?source-type ?source-tree-id ?source-node-id)
  (if (< ?blackboard-counter-value 0) then
    (return)
  )

  (do-for-all-facts ((?tree behavior-tree))
    (eq ?tree:operation-name ?operation-name)
    (delayed-do-for-all-facts ((?node behavior-tree-node))
      (and
        (eq ?node:tree-id ?tree:id)
        (eq ?tree:blackboard-scope ?scope)
        (and (eq ?node:type LOOP)
             (eq ?node:loop-counter-blackboard-key ?key))
      )
      ; Only issue counter updates for a node if the update source is not the
      ; node itself to prevent cycles
      (if (not (and (eq ?source-type NODE)
                  (eq ?source-tree-id ?node:tree-id)
                  (eq ?source-node-id ?node:id))) then
        (behavior-tree-loop-node-update-counter ?node (+ ?blackboard-counter-value 1))
      )
    )
    (delayed-do-for-all-facts ((?node behavior-tree-node))
      (and
        (eq ?node:tree-id ?tree:id)
        (eq ?tree:blackboard-scope ?scope)
        (and (eq ?node:type RETRY)
             (eq ?node:retry-counter-blackboard-key ?key))
      )
      ; Only issue counter updates for a node if the update source is not the
      ; node itself to prevent cycles
      (if (not (and (eq ?source-type NODE)
                  (eq ?source-tree-id ?node:tree-id)
                  (eq ?source-node-id ?node:id))) then
        (behavior-tree-retry-node-update-counter ?node (+ ?blackboard-counter-value 1))
      )
    )
  )
)

(deffunction behavior-tree-suspend (?tree-id)
  "Suspend the given behavior tree.
   Returns TRUE when successful, FALSE otherwise."
  (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?tree-id)
    (if (neq ?bt:state RUNNING) then
      (printout warn "Tried to suspend tree " ?bt:id " in state " ?bt:state
                     " (only allowed when RUNNING)" crlf)
      (return FALSE)
    )
    (modify ?bt (state SUSPENDING))
    (return TRUE)
  )
  (return FALSE)
)

; Cancels the specified behavior tree.
;
; Args:
;   ?tree-id: The ID of the behavior tree to cancel.
;
; Returns:
;   TRUE if successful, FALSE otherwise.
(deffunction behavior-tree-cancel (?tree-id)
  (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?tree-id)
    (if (and (neq ?bt:state RUNNING) (neq ?bt:state SUSPENDING)
             (neq ?bt:state SUSPENDED)) then
      (printout warn "Tried to cancel tree " ?bt:id " in state " ?bt:state
                     " (only allowed when RUNNING, SUSPENDING, or SUSPENDED)."
                     crlf)
      (return FALSE)
    )
    (modify ?bt (state CANCELING))
    (return TRUE)
  )
  (return FALSE)
)

; Creates an extended status to be set on a node from multiple context statuses
;
; The typical use case is that there are multiple context protos from a failure
; (e.g., all children of a fallback node failed) and these need to be collected
; under one extended status for the failed node. To this end the caller provides
; a template proto (e.g., "Fallback node failed") that the contexts are added
; to. The result becomes the extended status of the failed node.
;
; This function handles that and all additionally all node-specific special
; cases, so that a caller can just provide a template + the contexts.
; These are:
; - contexts can be 0 (i.e. no extended status)
; - there could be only 0 or 1 context extended status
; - there can be an emit extended status specified for the node
;
; Their intended behavior is as follows:
; - if there are 0 contexts, the node has no extended status
; - if there is 1 context, this is propagated directly without the intermediate
;   template
; - if the user specified an emit proto this is always the extended status of
;   the node instead of the template. Given context protos are attached to the
;   emit proto in that case.
;
; The function always creates a new extended status that is usually set as the
; extended status of ?node. If the node already has an extended-status-proto-id
; it is deleted.
;
; Args:
;   ?node: fact address of node to set extended status of. It is used read-only.
;   ?es-template-proto: extended status proto to collect contexts under. This
;                       function does take ownership of the ?es-template-proto.
;   ?es-context-protos: list of passed in context extended status protos. These
;     can be 0. The function does not take ownership of any passed in protos.
;
; Returns:
;   Proto message ID of extended status proto.
(deffunction behavior-tree-node-create-extended-status-from-contexts
  (?node ?es-template-proto $?es-context-protos)
  (bind ?tree-id (fact-slot-value ?node tree-id))
  (bind ?node-id (fact-slot-value ?node id))

  (bind ?current-es-proto (fact-slot-value ?node extended-status-proto-id))
  (if (<> ?current-es-proto 0) then
    (printout error (str-cat "Node " ?node-id " in " ?tree-id " already had an "
      "ExtendedStatus set when creating an extended status from contexts: "
      (pb-tostring ?current-es-proto)))
    (pb-remove ?current-es-proto)
  )

  (bind ?valid-es-context-protos (create$))
  (foreach ?es-context-proto ?es-context-protos
    (if (<> ?es-context-proto 0) then
      (bind ?valid-es-context-protos
        (append$ ?valid-es-context-protos ?es-context-proto)))
  )

  (bind ?emit-es-proto
    (fact-slot-value ?node on-failure-emit-extended-status-proto-id))
  (if (<> ?emit-es-proto 0) then
    ; If an emit proto was specified always create the node's extended status
    ; from that instead of the template
    (bind ?node-es-proto (pb-clone ?emit-es-proto))
    (pb-remove ?es-template-proto)
    (do-for-fact ((?tree behavior-tree)) (eq ?tree:id ?tree-id)
      (extended-status-set-related-to ?node-es-proto ?tree ?node)
    )

    (foreach ?esc-proto ?valid-es-context-protos
      (extended-status-add-context ?node-es-proto ?esc-proto)
    )

    (return ?node-es-proto)
  )

  (if (= (length$ ?valid-es-context-protos) 0) then
    ; Without any context there is nothing to report.
    (pb-remove ?es-template-proto)
    (return 0)
  )

  (if (= (length$ ?valid-es-context-protos) 1) then
    ; With a single context propagate directly without the intermediate
    ; template
    (pb-remove ?es-template-proto)
    (bind ?node-es-proto (pb-clone (nth$ 1 ?valid-es-context-protos)))
    (return ?node-es-proto)
  )

  ; Default case: Use the template to attach valid contexts to.
  (foreach ?esc-proto ?valid-es-context-protos
    (extended-status-add-context ?es-template-proto ?esc-proto)
  )
  (return ?es-template-proto)
)

; Collect a context extended status on a specific node
;
; This function is intended to be called incrementally adding context extended
; statuses from repeated failures of a child (e.g., for a retry node).
; It automatically detects if there is already an extended status on the node
; and in that case adds the context proto to that extended status. If there is
; no extended status, yet, an extended status is created for the node: Either
; from the emit proto if that is defined or using the provided code.
;
; The ?es-context-proto can be 0. The intermediate extended status will always
; be generated.
;
; Args:
;   ?node: fact address of node to set extended status of. It is used read-only.
;   ?es-intermediate-code: Status code to create for this node, where
;                          ?es-context-proto will be attached to.
;   ?es-context-proto: The proto to collect in the context. The
;                      function does not take ownership of this proto.
;
; Returns:
;   Proto message ID of extended status proto.
(deffunction behavior-tree-node-collect-extended-status
  (?node ?es-intermediate-code ?es-context-proto)
  (bind ?tree-id (fact-slot-value ?node tree-id))

  (bind ?es-proto (fact-slot-value ?node extended-status-proto-id))
  (if (= ?es-proto 0) then
    (bind ?emit-es-proto
      (fact-slot-value ?node on-failure-emit-extended-status-proto-id))
    (if (<> ?emit-es-proto 0)
     then
      (bind ?es-proto (pb-clone ?emit-es-proto))
     else
      (bind ?es-proto (extended-status-create ?es-intermediate-code ERROR))
    )
    (do-for-fact ((?tree behavior-tree)) (eq ?tree:id ?tree-id)
      (extended-status-set-related-to ?es-proto ?tree ?node)
    )
  )

  (if (<> ?es-context-proto 0) then
    (extended-status-add-context ?es-proto ?es-context-proto)
  )

  (return ?es-proto)
)

; Set an extended status for a specific node
;
; The function always creates a new extended status that is usually set as the
; extended status of ?node with the exception that the node already has an
; extended-status-proto-id set. In this case no new extended status is created.
; The extended status on the node might be modified though.
;
; Args:
;   ?node: fact address of node to set extended status of. It is used read-only.
;   ?es-context-proto: Context proto to attach to the node's status or use as
;     the node's extended status. The function does not take ownership of any
;     passed in protos.
;
; Returns:
;   Proto message ID of extended status proto.
(deffunction behavior-tree-node-attach-or-propagate-extended-status
  (?node ?es-context-proto)
  (bind ?tree-id (fact-slot-value ?node tree-id))
  (bind ?node-id (fact-slot-value ?node id))

  (bind ?current-es-proto (fact-slot-value ?node extended-status-proto-id))
  (if (<> ?current-es-proto 0) then
    (printout error (str-cat "Node " ?node-id " in " ?tree-id " already had an "
      "ExtendedStatus set when creating an extended status from contexts: "
      (pb-tostring ?current-es-proto)))
    (pb-remove ?current-es-proto)
  )

  (bind ?es-proto 0)
  (bind ?emit-es-proto
    (fact-slot-value ?node on-failure-emit-extended-status-proto-id))
  (if (<> ?emit-es-proto 0) then
    (bind ?es-proto (pb-clone ?emit-es-proto))
    (do-for-fact ((?tree behavior-tree)) (eq ?tree:id ?tree-id)
      (extended-status-set-related-to ?es-proto ?tree ?node)
    )
  )

  (if (<> ?es-context-proto 0) then
    (if (= ?es-proto 0)
     then
      (bind ?es-proto (pb-clone ?es-context-proto))
     else
      (extended-status-add-context ?es-proto ?es-context-proto)
    )
  )

  (return ?es-proto)
)

; Emits state conditionally to blackboard.
;
; This checks whether the node is configured to emit extended state to the
; blackboard on failure and if there is an ES proto. Only then is that proto
; cloned and emitted to the blackboard.
;
; Args:
;   ?node: node to emit extended state info for
;   ?es-proto: proto to emit, maybe 0 in which case nothing is emitted.
(deffunction behavior-tree-node-maybe-emit-extended-status (?node ?es-proto)
  ; If requested, emit ExtendedStatus proto to blackboard
  (bind ?on-failure-emit-es-to-blackboard-key
    (fact-slot-value ?node on-failure-emit-extended-status-blackboard-key))
  (bind ?tree-id (fact-slot-value ?node tree-id))
  (if (and (<> ?es-proto 0) (neq ?on-failure-emit-es-to-blackboard-key "")) then
    (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?tree-id)
      (assert (blackboard-update (key ?on-failure-emit-es-to-blackboard-key)
                                 (scope ?bt:blackboard-scope)
                                 (operation-name ?bt:operation-name)
                                 (proto-id (pb-clone ?es-proto))))
    )
  )
)

; Select the specific node for execution.
;
; Args:
;   ?node: fact addresses of node to start. Will be invalidated by this function
(deffunction behavior-tree-select-node (?node)
  (bind ?new-span-id
    (tracing-cc-start-node-span (fact-slot-value ?node tree-id)
                                (fact-slot-value ?node id)))

  (modify ?node (state SELECTED) (span-reference-id ?new-span-id))
)

; Mark the given ?node from the behavior tree ?tree as succeeded.
; The parameter ?node must be a fact address. It will become invalid after this
; function completes.
(deffunction behavior-tree-set-node-succeeded (?node)
  (bind ?node-span-reference-id (fact-slot-value ?node span-reference-id))
  (span-end ?node-span-reference-id)

  (if (eq (fact-slot-value ?node state) CANCELING)
   then (tracing-end-canceling-span ?node))
  (bind ?es-proto (fact-slot-value ?node extended-status-proto-id))
  (pb-remove ?es-proto)

  (modify ?node (state SUCCEEDED)
          (span-reference-id ?*TRACING-INVALID-SPAN-ID*)
          (extended-status-proto-id 0))
)

; Mark the given ?node from the behavior tree ?tree as canceled.
; The parameter ?node must be a fact address. It will become invalid after this
; function completes.
(deffunction behavior-tree-set-node-canceled (?node)
  (tracing-end-canceling-span ?node)
  (bind ?node-span-reference-id (fact-slot-value ?node span-reference-id))
  (span-end-failure ?node-span-reference-id
                                 ?*TRACING-STATUS-CANCELLED* "Node canceled")
  (bind ?es-proto (fact-slot-value ?node extended-status-proto-id))
  (pb-remove ?es-proto)
  (modify ?node (state CANCELED)
                (canceling-span-reference-id ?*TRACING-INVALID-SPAN-ID*)
                (span-reference-id ?*TRACING-INVALID-SPAN-ID*)
                (extended-status-proto-id 0))
)


; Mark the given ?node from the behavior tree ?tree as failed.
;
; Args:
;   ?node: fact address of node to mark failed. It will become invalid after
;          this function completes.
;   ?reason:  must be one of the allowed values (but not UNKNOWN) of the
;             failure-reason slot in behavior-tree-node.
;   ?message: string giving additional information about nature of the failure
;   ?options: Additional options that may or may not be set, see below.
;
; Options are:
;   - EXTENDED-STATUS-CONTEXT-PROTO-ID ?es-proto-id: There is an additional
;       proto available to set as the node's extended status or add as context
;       to it, the ?es-proto-id must be a proto ID of an ExtendedStatus proto.
;   - EXTENDED-STATUS-SET ?es-proto-id: Set ?es-proto-id as the extended status
;       of the failed node. Takes ownership of ?es-proto-id. Must not be
;       combined with other options.
(deffunction behavior-tree-set-node-failed (?node ?reason ?message $?options)
  (bind ?allowed-reasons
    (set-diff (deftemplate-slot-allowed-values behavior-tree-node failure-reason)
              (create$ UNKNOWN)))
  (bind ?set-fail-succeeded TRUE)
  (if (not (member$ ?reason ?allowed-reasons)) then
    (printout error (str-cat
                       "behavior-tree-set-node-failed: Expected a reason from "
                       (str-join ", " ?allowed-reasons) ", got: " ?reason
                       "from node type: " (fact-slot-value ?node type)) crlf)
    (bind ?set-fail-succeeded FALSE)
  )

  (bind ?es-proto (fact-slot-value ?node extended-status-proto-id))
  (if (in$ EXTENDED-STATUS-SET ?options)
    then
      (bind ?es-proto
        (nth$ (+ (member$ EXTENDED-STATUS-SET ?options) 1) ?options))
    else
      (if (in$ EXTENDED-STATUS-CONTEXT-PROTO-ID ?options) then
        (bind ?es-context-proto
          (nth$ (+ (member$ EXTENDED-STATUS-CONTEXT-PROTO-ID ?options) 1)
                ?options))
        (bind ?es-proto
          (behavior-tree-node-attach-or-propagate-extended-status
            ?node ?es-context-proto))
      else
        ; Call this with no context proto in case there is an emit status on
        ; this node that would be generated
        (bind ?es-proto
          (behavior-tree-node-attach-or-propagate-extended-status ?node 0))
      )
  )

  (if (= ?es-proto 0) then
    (bind ?es-proto (extended-status-create 31010))
    (extended-status-set-message ?es-proto USER (str-cat "Node of type "
      (fact-slot-value ?node type) " failed without providing ExtendedStatus "
      "error information or its failing child did not provide ExtendedStatus "
      "error information. Reported message: "  ?message))
  )

  ; Add relations to this node if this is the first node this propagates to
  ; (e.g., when propagated up from a skill failure)
  (if (not (pb-has-field ?es-proto "related_to.behavior_tree_node")) then
    (extended-status-set-related-to-by-ids ?es-proto
      (fact-slot-value ?node tree-id)
      (fact-slot-value ?node id))
  )

  (behavior-tree-node-maybe-emit-extended-status ?node ?es-proto)

  (bind ?span-status ?*TRACING-STATUS-ABORTED*)
  (if (eq ?reason CONDITION) then
    (bind ?span-status ?*TRACING-STATUS-FAILED-PRECONDITION*)
  )
  (if (eq (fact-slot-value ?node state) CANCELING) then
    (tracing-end-canceling-span-failure ?node ?span-status
                                        "Failed during cancellation"))
  (bind ?node-span-reference-id (fact-slot-value ?node span-reference-id))
  (span-end-failure ?node-span-reference-id ?span-status ?message)
  (modify ?node (state FAILED)
                (failure-reason ?reason)
                (failure-message ?message)
                (span-reference-id ?*TRACING-INVALID-SPAN-ID*)
                (extended-status-proto-id ?es-proto))
  (return ?set-fail-succeeded)
)

; Recursively reset a condition.
;
; Args:
;   ?condition-id: ID of the condition to reset
;   ?operation-name: Name of the operation
;   ?keep-counters: True if counters should not be reset
(deffunction behavior-tree-condition-reset (?condition-id ?operation-name ?keep-counters)
  "Recursively reset behavior tree condition to UNKNOWN satisfaction."
  (do-for-fact ((?condition behavior-tree-condition))
      (eq ?condition:id ?condition-id)

    (pb-remove ?condition:extended-status-proto-id)

    (bind ?run-metadata-proto-path ?condition:run-metadata-proto-path)
    (if (neq ?run-metadata-proto-path "") then
      (bind ?s-state-path (proto-path-join ?run-metadata-proto-path "state"))
      (bind ?s-satisfied-path (proto-path-join ?run-metadata-proto-path "satisfied"))
      (run-metadata-proto-update-field ?s-state-path ACCEPTED ?operation-name)
      (run-metadata-proto-clear-field ?s-satisfied-path ?operation-name)
    )

    (delayed-do-for-all-facts ((?child behavior-tree-condition))
        (and (eq ?child:tree-id ?condition:tree-id)
             (eq ?child:node-id ?condition:node-id)
             (eq ?child:parent-id ?condition:id))
      (behavior-tree-condition-reset ?child:id ?operation-name ?keep-counters)
    )

    (if (eq ?condition:type SUB-TREE) then
      (behavior-tree-reset ?condition:sub-tree-id ?keep-counters)
    )

    (modify ?condition (satisfied UNKNOWN) (state ACCEPTED))
  )
)

; Recursively reset a node (and its children) to ACCEPTED state.
; This will recursively reset the given ?node and its children to
; the ACCEPTED state and reset conditions satisfaction to unknown.
;
; Args:
;   ?node: fact-address of the node to reset
;   ?operation-name: Name of the operation
;   ?keep-counters: True if counters should not be reset
(deffunction behavior-tree-node-reset-internal (?node ?reset-child-nodes ?operation-name ?keep-counters)
  "Recursively reset a node (and its children) to ACCEPTED state."
  (if (eq (fact-slot-value ?node state) ACCEPTED) then
    (return)
  )

  (bind ?node-id (fact-slot-value ?node id))
  (bind ?node-type (fact-slot-value ?node type))
  (bind ?tree-id (fact-slot-value ?node tree-id))
  (bind ?condition-id (fact-slot-value ?node condition-id))
  (bind ?branch-if-id (fact-slot-value ?node branch-if-id))
  (bind ?loop-while-id (fact-slot-value ?node loop-while-id))
  (bind ?loop-for-each-generator-expression
    (fact-slot-value ?node loop-for-each-generator-expression))
  (bind ?run-metadata-proto-path (fact-slot-value ?node run-metadata-proto-path))
  (bind ?es-proto-id (fact-slot-value ?node extended-status-proto-id))
  (bind ?on-failure-emit-es-to-blackboard-key
    (fact-slot-value ?node on-failure-emit-extended-status-blackboard-key))

  (if (neq ?condition-id nil) then
    (behavior-tree-condition-reset ?condition-id ?operation-name ?keep-counters))
  (if (neq ?branch-if-id nil) then
    (behavior-tree-condition-reset ?branch-if-id ?operation-name ?keep-counters))
  (if (neq ?loop-while-id nil) then
    (behavior-tree-condition-reset ?loop-while-id ?operation-name ?keep-counters))

  ; Remove and clear ExtendedStatus data
  (pb-remove ?es-proto-id)
  ; If an ES may have been omitted with the given key remove it from blackboard
  (if (neq ?on-failure-emit-es-to-blackboard-key "") then
    (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?tree-id)
      (blackboard-remove ?on-failure-emit-es-to-blackboard-key
                         ?bt:blackboard-scope
                         ?bt:operation-name)
    )
  )

  (if (eq ?node-type TASK) then
    (if (neq ?run-metadata-proto-path "") then
        (bind ?s-task-path (proto-path-join ?run-metadata-proto-path "task.state"))
        (run-metadata-proto-update-field ?s-task-path ACCEPTED ?operation-name)

        (bind ?node-task-type (fact-slot-value ?node task-type))
        (run-metadata-proto-reset-task-node-execution-info
          ?run-metadata-proto-path ?node-task-type ?operation-name)
    )
    (if (neq (fact-slot-value ?node task-action-uid) nil) then
      (plan-action-remove (fact-slot-value ?node task-action-uid)))
    (if (neq (fact-slot-value ?node behavior-call-instance-uid) nil) then
      (behavior-call-instance-reset
        (fact-slot-value ?node behavior-call-instance-uid) ?keep-counters))

    (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?tree-id)
      (code-execution-instance-reset ?bt:operation-name ?tree-id ?node-id)
    )
  )

  ; If a loop node defines a generator expression, then the loop-protos are to
  ; be regenerated every cycle. Thus removed these as part of a reset.
  (if (neq ?loop-for-each-generator-expression "") then
    (foreach ?loop-proto (fact-slot-value ?node loop-for-each-loop-protos)
      (pb-remove ?loop-proto))
  )

  (if (neq (fact-slot-value ?node sub-tree-id) nil) then
    (behavior-tree-reset (fact-slot-value ?node sub-tree-id) ?keep-counters))

  (if (neq ?run-metadata-proto-path "") then
    (bind ?s-path (proto-path-join ?run-metadata-proto-path "state"))
    (run-metadata-proto-update-field ?s-path ACCEPTED ?operation-name)
    (bind ?s-path (proto-path-join ?run-metadata-proto-path "recovered"))
    (run-metadata-proto-update-field ?s-path FALSE ?operation-name)
  )

  (bind ?retry-num-tries 0)
  (bind ?loop-num-times 0)
  (if ?keep-counters then
    (bind ?retry-num-tries (fact-slot-value ?node retry-num-tries))
    (bind ?loop-num-times (fact-slot-value ?node loop-num-times))
  )

  (modify ?node (state ACCEPTED)
                (run-metadata-proto-state ACCEPTED)
                (run-metadata-proto-recovered-state NONE)
                (retry-num-tries ?retry-num-tries)
                (loop-num-times ?loop-num-times)
                (task-action-uid nil)
                (breakpoint-triggered FALSE)
                (stepwise-state NONE)
                (extended-status-proto-id 0))

  (if ?reset-child-nodes then
    (delayed-do-for-all-facts ((?child behavior-tree-node))
      (and (eq ?child:tree-id ?tree-id) (eq ?child:parent-id ?node-id))
      (behavior-tree-node-reset-internal ?child TRUE ?operation-name ?keep-counters)
    )
  )
)

(deffunction behavior-tree-node-reset (?node ?operation-name)
  (bind ?reset-child-nodes TRUE)
  (behavior-tree-node-reset-internal ?node ?reset-child-nodes ?operation-name FALSE)
)

(deffunction behavior-tree-reset (?tree-id ?keep-counters)
  (bind ?operation-name "")
  ; Reset the tree itself
  (do-for-fact ((?tree behavior-tree)) (eq ?tree:id ?tree-id)
    (if (eq ?tree:state ACCEPTED) then
      (return)
    )

    (bind ?operation-name ?tree:operation-name)
    ; Either we do have a non-empty path (which then is assumed valid),
    ; or we could be at the top-level tree in which case the path would
    ; be empty but valid nevertheless.
    (if (or (neq ?tree:run-metadata-proto-path "")
            (eq ?tree:parent-behavior-tree-id nil))
     then
       (bind ?s-path (proto-path-join ?tree:run-metadata-proto-path "state"))
       (run-metadata-proto-update-field ?s-path ACCEPTED ?tree:operation-name)
    )
    (modify ?tree (state ACCEPTED) (run-metadata-proto-state ACCEPTED))
  )
  ; Reset all nodes in the tree.
  ; These do not need to reset all their children as that is already part of
  ; this loop.
  (delayed-do-for-all-facts ((?node behavior-tree-node))
    (eq ?node:tree-id ?tree-id)
    (behavior-tree-node-reset-internal ?node FALSE ?operation-name ?keep-counters)
  )
)

(deffunction behavior-tree-index-sort-comp (?n1 ?n2)
  (return (> (fact-slot-value ?n1 index) (fact-slot-value ?n2 index)))
)

(deffunction behavior-tree-sorted-children (?node-id ?tree-id)
  (do-for-fact ((?node behavior-tree-node))
      (and (eq ?node:id ?node-id)
           (eq ?node:tree-id ?tree-id))

    (bind ?children
      (find-all-facts ((?c behavior-tree-node))
                      (and (eq ?c:parent-id ?node:id)
                           (eq ?c:tree-id ?node:tree-id))))
    (return (sort behavior-tree-index-sort-comp ?children))
  )
  (return FALSE)
)

; This removes a given condition and associated data.
; Unfortunately there is a cyclic dependency, behavior-tree-delete calls this
; function, which potentially again needs to remove sub-trees. We resolve this
; by returning a list of sub-trees that the caller is required to remove.
(deffunction behavior-tree-condition-remove (?id)
  (do-for-fact ((?c behavior-tree-condition)) (eq ?c:id ?id)
    (pb-remove ?c:extended-status-proto-id)
    (switch ?c:type
      (case AND then
        (delayed-do-for-all-facts ((?c2 behavior-tree-condition))
          (eq ?c2:parent-id ?c:id)

          (behavior-tree-condition-remove ?c2:id)
        )
      )
      (case OR then
        (delayed-do-for-all-facts ((?c2 behavior-tree-condition))
          (eq ?c2:parent-id ?c:id)

          (behavior-tree-condition-remove ?c2:id)
        )
      )
      (case NOT then
        (delayed-do-for-all-facts ((?c2 behavior-tree-condition))
          (eq ?c2:parent-id ?c:id)

          (behavior-tree-condition-remove ?c2:id)
        )
      )
      (case SUB-TREE then
        (behavior-tree-delete ?c:sub-tree-id)
      )
      (case BLACKBOARD-CEL-EXPRESSION then
        (cel-remove ?c:blackboard-cel-expression-id)
      )
    )
    (retract ?c)
  )
)


(deffunction behavior-tree-get-top-level-tree-id (?tree-id)
  "Get the tree ID of the top-level tree ID.

   The top-level tree ID is the one which has no further parent. Sub-trees
   have a parent tree.

   The recursive search walks up from the given tree ?tree-id to the
   top tree (until there is no further parent tree).
  "
  (do-for-fact ((?tree behavior-tree)) (eq ?tree:id ?tree-id)
    (if (eq ?tree:parent-behavior-tree-id nil)
     then
       (return ?tree-id)
     else
       (return
        (behavior-tree-get-top-level-tree-id ?tree:parent-behavior-tree-id)))
  )
  (return INVALID-TREE-ID)
)


(deffunction behavior-tree-delete (?id)
  "Remove behavior tree with given ID and related facts."

  (bind ?operation-name "")
  (do-for-fact ((?bt behavior-tree)) (eq ?bt:id ?id)
    (plan-remove (sym-cat ?bt:id "-action-prototypes"))
    (plan-remove ?bt:plan-id)
    (blackboard-flush ?bt:blackboard-scope  ?bt:operation-name)
    (bind ?operation-name ?bt:operation-name)
    (retract ?bt)
  )

  (delayed-do-for-all-facts ((?node behavior-tree-node)) (eq ?node:tree-id ?id)
    (if (neq ?node:task-action-prototype-uid nil) then
      (plan-action-remove ?node:task-action-prototype-uid))
    (if (neq ?node:task-action-uid nil) then
      (plan-action-remove ?node:task-action-uid))
    (if (neq ?node:behavior-call-instance-uid nil) then
      (behavior-call-instance-remove ?node:behavior-call-instance-uid))
    (code-execution-instance-remove ?operation-name ?id ?node:id)

    (foreach ?loop-input-proto ?node:loop-for-each-input-protos
      (pb-remove ?loop-input-proto))
    (foreach ?loop-proto ?node:loop-for-each-loop-protos
      (pb-remove ?loop-proto))
    (cel-remove ?node:loop-for-each-generator-expression-id)

    (if (neq ?node:branch-if-id nil) then
      (behavior-tree-condition-remove ?node:branch-if-id))
    (if (neq ?node:loop-while-id nil) then
      (behavior-tree-condition-remove ?node:loop-while-id))
    (if (neq ?node:condition-id nil) then
      (behavior-tree-condition-remove ?node:condition-id))

    (if (<> ?node:on-failure-emit-extended-status-proto-id 0) then
      (pb-remove ?node:on-failure-emit-extended-status-proto-id))
    (if (<> ?node:extended-status-proto-id 0) then
      (pb-remove ?node:extended-status-proto-id))

    (if (neq ?node:sub-tree-id nil) then
      (behavior-tree-delete ?node:sub-tree-id))

    (retract ?node)
  )
)

(deffunction behavior-tree-get-contained-blackboard-scopes (?tree-id)
  "Retrieves all blackboard scopes in ?tree-id and any of its contained trees."
  (bind ?scopes (create$))
  (do-for-fact ((?tree behavior-tree)) (eq ?tree:id ?tree-id)
    (bind ?scopes (append$ ?scopes ?tree:blackboard-scope))

    (do-for-fact ((?child-tree behavior-tree))
      (eq ?child-tree:parent-behavior-tree-id ?tree-id)
      (bind ?child-scopes
        (behavior-tree-get-contained-blackboard-scopes ?child-tree:id))

      (foreach ?child-scope ?child-scopes
        ; Only add ?child-scope, if it is not yet in ?scopes, so that entries in
        ; ?scopes are unique.
        (if (eq (member$ ?child-scope ?scopes) FALSE) then
          (bind ?scopes (append$ ?scopes ?child-scope))
        )
      )
    )
  )
  (return ?scopes)
)

; ----------------------------------- RULES -----------------------------------

(defrule behavior-tree-finished
  ?tree <- (behavior-tree (id ?tree-id) (plan-id ?plan-id)
                          (start-node-id ?start-node)
                          (state ?tree-state&RUNNING|CANCELING|SUSPENDING)
                          (span-reference-id ?tree-span))
  (behavior-tree-node (id ?start-node) (tree-id ?tree-id)
                      (state ?state&SUCCEEDED|FAILED|CANCELED))
 =>
  (printout t "Behavior Tree " ?tree-id " has " ?state crlf)

  (switch ?state
    (case SUCCEEDED then
      (span-end ?tree-span)
    )
    (case FAILED then
      (span-end-failure ?tree-span ?*TRACING-STATUS-ABORTED* "Tree failed")
    )
    (case CANCELED then
      (span-end-failure ?tree-span ?*TRACING-STATUS-CANCELLED* "Tree canceled")
    )
  )

  (modify ?tree (state ?state) (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
)

(defrule behavior-tree-suspended
  ?tree <- (behavior-tree (id ?tree-id) (state SUSPENDING)
                          (start-node-id ?start-node))
  (not (behavior-tree-node (id ?start-node) (tree-id ?tree-id)
                           (state SUCCEEDED|FAILED)))
  (not (behavior-tree-node (tree-id ?tree-id)
                           (state EVALUATING-CONDITION|RUNNING|CANCELING)))
  (forall (behavior-tree-condition (id ?cond-id) (tree-id ?tree-id))
          (behavior-tree-condition (id ?cond-id) (tree-id ?tree-id)
                                   (state ACCEPTED|SUSPENDED|FINISHED)))
 =>
  (modify ?tree (state SUSPENDED))
  (printout t "Behavior Tree " ?tree-id " is SUSPENDED" crlf)
)

(defrule behavior-tree-select-start-node
  "Selects the start node when the behavior tree is RUNNING."
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state RUNNING))
  ?start-node <- (behavior-tree-node (id ?start-node-id) (tree-id ?tree-id)
                               (state ACCEPTED))
 =>
  (behavior-tree-select-node ?start-node)
)

(defrule behavior-tree-cancel-start-node
  "Cancels the start node when the behavior tree is canceling."
  (declare (salience ?*SALIENCE-CANCELING*))
  (behavior-tree (id ?tree-id) (start-node-id ?start-node-id) (state CANCELING))
  ?start-node <- (behavior-tree-node (id ?start-node-id) (tree-id ?tree-id)
                                     (state SELECTED|READY|RUNNING|SUSPENDED))
 =>
  (modify ?start-node (state CANCELING)
          (canceling-span-reference-id
            (tracing-start-canceling-span ?start-node)))
)
