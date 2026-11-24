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

; Action execution through skills

; This file requires skill_info.clp to be loaded (typically done during
; ClipsSkillDispatcher::Init()).

; ------------------------------- FUNCTIONS -----------------------------------

; Removes any CANCELING skill-status facts for an action.
;
; This is usually called, when an action is cancelling and failing immediately.
; In this case a CANCELING and a FAILED (or possibly SUCCEEDED) skill-status
; are in the facts data base at the same time. The action is then in
; CANCELLATION-PENDING and transistions directly to SUCCEEDED/FAILED, which is
; correct as the intermediate state was skipped. The skill-status can thus be
; cleaned away.
;
; Args:
;   ?action-uid: uid of the action to remove skill-status facts for.
(deffunction action-execution-cleanup-skill-status-cancelling (?action-uid)
  (do-for-fact ((?skill-status skill-status))
    (and (eq ?skill-status:action-id ?action-uid)
         (eq ?skill-status:status CANCELING))
    (retract ?skill-status)
  )
)

; ----------------------------------- RULES -----------------------------------


(defrule action-execution-skill-start
  "Start skill after corresponding action became ready and executable."
  (executive-state (session-log-id ?session-log-id))
  (behavior-tree (id ?tree-id) (plan-id ?plan-id)
                 (operation-name ?operation-name) (state RUNNING)
                 (log-id ?bt-log-id&~0))
  (behavior-tree-node (tree-id ?tree-id) (task-action-uid ?uid)
                      (name ?node-display-name))
  ; Higher salience of projection will cause projection to run first. However,
  ; if projection is not enabled (ruleset not loaded), actions will be selected
  ; when starting execution.
  ?af <- (plan-action (plan-id ?plan-id) (id ?action-id) (uid ?uid)
                      (state READY)
                      (skill-id ?skill-id) (executable TRUE)
                      (behavior-call-proto-id ?behavior-call-proto)
                      (log-id ?action-log-id&~0)
                      (span-reference-id ?parent-span-id))
  (skill-info (skill-id ?skill-id)
              (parameter-descriptor-pool-id ?parameter-descriptor-pool-id))
  (world (id ?world-id))
 =>
  (bind ?sim-mode (operation-get-simulation-mode-by-tree ?tree-id))

  (bind ?display-name
    (if (neq ?node-display-name "") then (str-cat " (" ?node-display-name ")")
                                    else ""))
  (bind ?log-verb "execution")
  (if (eq ?sim-mode PREVIEW) then (bind ?log-verb "preview"))
  (if (eq ?sim-mode FAST-PREVIEW) then (bind ?log-verb "fast preview"))
  (printout t "Starting " ?log-verb " of skill " ?skill-id ?display-name crlf)

  (bind ?parent-log-id (log-find-parent-log-id ?tree-id))
  (bind ?log-context-proto
    (log-context-create ?session-log-id ?bt-log-id ?action-log-id
                        ?parent-log-id ?operation-name))
  (delayed-do-for-all-facts ((?pa plan-action)) (eq ?pa:state PROJECTING)
    (modify ?pa (projection-running-actions
      (append$ ?pa:projection-running-actions ?action-id)))
  )

  (if (<> ?parent-span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?parent-span-id "pending")
  )
  (skill-start ?uid ?world-id ?behavior-call-proto ?log-context-proto
               ?parent-span-id ?parameter-descriptor-pool-id ?sim-mode)
  (pb-remove ?log-context-proto)
  (modify ?af (state PENDING) (execution-start-time (now)))
)

(defrule action-execution-skill-running
  "Observe skill status by ClipsSkillDispatcher for RUNNING"
  (behavior-tree (plan-id ?plan-id)
                 (state RUNNING|SUSPENDING|CANCELING))
  ?af <- (plan-action (id ?action-id) (plan-id ?plan-id) (uid ?uid)
                      (state PENDING) (skill-id ?skill-id))
  ?sf <- (skill-status (action-id ?uid) (status RUNNING))
  (skill-info (skill-id ?skill-id))
 =>
  (printout debug "Action " (plan-action-tostring ?plan-id ?action-id)
                  " (skill " ?skill-id ") is running" crlf)
  (modify ?af (state RUNNING))
  (retract ?sf)
)

(defrule action-execution-skill-cancel
  "Triggers skill cancellation after cancellation of its action was requested."
  ?action <- (plan-action (id ?action-id) (plan-id ?plan-id) (uid ?uid)
                          (skill-id ?skill-id) (state CANCELLATION-REQUESTED))
  (behavior-tree (id ?tree-id) (plan-id ?plan-id))
  (skill-info (skill-id ?skill-id))
  ; Only cancel the skill once all its skill-status reports have been handled.
  ; For example, the skill could have succeeded in the meantime.
  (not (skill-status (action-id ?uid)))
 =>
  (printout debug "Triggering cancellation of action "
                  (plan-action-tostring ?plan-id ?action-id)
                  " (skill " ?skill-id ")." crlf)
  (bind ?sim-mode (operation-get-simulation-mode-by-tree ?tree-id))
  (skill-cancel-async ?uid ?sim-mode)
  (modify ?action (state CANCELLATION-PENDING))
)

(defrule action-execution-skill-canceling
  "Transitions action to CANCELING once the ClipsSkillDispatcher indicates that
  the skill is cancelling."
  (behavior-tree (plan-id ?plan-id))
  ?action <- (plan-action (id ?action-id) (plan-id ?plan-id) (uid ?uid)
                          (skill-id ?skill-id) (state CANCELLATION-PENDING))
  ?sf <- (skill-status (action-id ?uid) (status CANCELING))
  (skill-info (skill-id ?skill-id))
 =>
  (printout debug "Action " (plan-action-tostring ?plan-id ?action-id)
                  " (skill " ?skill-id ") is cancelling." crlf)
  (modify ?action (state CANCELING))
  (retract ?sf)
)

(defrule action-execution-skill-canceled
  "Transitions action to CANCELED once the ClipsSkillDispatcher indicates that
  the skill has been canceled."
  (world (id ?world-id))
  ?action <- (plan-action (id ?action-id) (plan-id ?plan-id) (uid ?uid)
                          (skill-id ?skill-id)
                          (state CANCELING|CANCELLATION-REQUESTED|CANCELLATION-PENDING)
                          (span-reference-id ?trace-span-id))
  ?sf <- (skill-status (action-id ?uid) (status CANCELED))
  (skill-info (skill-id ?skill-id))
 =>
  (printout debug "Action " (plan-action-tostring ?plan-id ?action-id)
                  " (skill " ?skill-id ") has been canceled." crlf)
  (modify ?action (state CANCELED)
          (world-id-execution-after
            (world-clone ?world-id "skill_canceled" ?trace-span-id))
          (execution-end-time (now)))
  (retract ?sf)
)

(defrule action-execution-skill-canceled-timeout
  "Observe skill status by ClipsSkillDispatcher for cancellation after timeout."
  (world (id ?world-id))
  ?af <- (plan-action (uid ?uid) (skill-id ?skill-id)
                      (state ?state&CANCELING-EXECUTION-TIMEOUT)
                      (span-reference-id ?trace-span-id))
  ?sf <- (skill-status (action-id ?uid) (status CANCELED))
  (skill-info (skill-id ?skill-id))
 =>
  (bind ?es-proto (extended-status-create 18002 ERROR))
  (extended-status-set-message ?es-proto USER
    (str-cat "Skill '" ?skill-id "' was canceled after it exceeded its timeout."))
  (modify ?af (state EXECUTION-FAILED)
              (world-id-execution-after
                (world-clone ?world-id "skill_timeout" ?trace-span-id))
              (execution-end-time (now))
              (extended-status-proto-id ?es-proto))
  (retract ?sf)
)

(defrule action-execution-skill-timeout
  "Observe skill status by ClipsSkillDispatcher for timeout"
  ; Slightly higher salience for the case that CANCELING-EXECUTION-TIMEOUT and
  ; FAILED or SUCCEEDED updates are received in the same cycle.
  (declare (salience ?*SALIENCE-HIGH*))
  ?af <- (plan-action (uid ?uid) (skill-id ?skill-id) (state RUNNING))
  ?sf <- (skill-status (action-id ?uid) (status CANCELING-EXECUTION-TIMEOUT))
  (skill-info (skill-id ?skill-id))
 =>
  (modify ?af (state CANCELING-EXECUTION-TIMEOUT))
  (retract ?sf)
)

(defrule action-execution-skill-succeeded
  "Observe skill status by ClipsSkillDispatcher for success"
  (world (id ?world-id))
  ?af <- (plan-action (id ?action-id) (plan-id ?plan-id) (uid ?uid)
                      (skill-id ?skill-id)
                      (return-value-name ?return-value-name)
                      (state ?state&RUNNING|CANCELLATION-REQUESTED|
                                    CANCELLATION-PENDING|CANCELING|
                                    CANCELING-EXECUTION-TIMEOUT)
                      (span-reference-id ?trace-span-id))
  (behavior-tree (plan-id ?plan-id) (blackboard-scope ?bb-scope)
                 (id ?tree-id) (operation-name ?op-name))
  ?sf <- (skill-status (action-id ?uid) (status SUCCEEDED)
                       (return-value-proto-id ?return-value-proto-id))
  ?si <- (skill-info (skill-id ?skill-id)
           (return-value-descriptor-pool-id ?return-value-descriptor-pool-id)
           (return-value-message-name ?return-value-message-name))
 =>
  (bind ?result-state EXECUTION-SUCCEEDED)
  (bind ?extended-status-proto 0)
  (if (neq ?return-value-name "") then
    ; Check if a return value was provided
    (if (<> ?return-value-proto-id 0)
      ; Cast return value from any proto and add to blackboard
      then
        (if (<> ?return-value-descriptor-pool-id 0) then
          (bind ?casted-return-value-result
            (pb-cast-from-any-with-pool ?return-value-proto-id
              ?return-value-descriptor-pool-id ?return-value-message-name))
          (if (result-ok ?casted-return-value-result)
            then
              (bind ?casted-return-value-proto-id
                (result-value ?casted-return-value-result))
              (assert (blackboard-update (proto-id ?casted-return-value-proto-id)
                                         (key ?return-value-name)
                                         (scope ?bb-scope)
                                         (operation-name ?op-name)))
            else
              (bind ?extended-status-proto (extended-status-create 13403 ERROR))
              (extended-status-set-message ?extended-status-proto USER
                (str-cat "Failed to generate return value from " ?skill-id))
              (bind ?es-cast-proto
                (result-error ?casted-return-value-result))
              (extended-status-add-context ?extended-status-proto ?es-cast-proto)
              (pb-remove ?es-cast-proto)

              (bind ?result-state EXECUTION-FAILED)
          )
        )
      else
        ; Check if a return value was expected and fail if it was
        (if (neq ?return-value-message-name "") then
          (bind ?extended-status-proto (extended-status-create 13403 ERROR))
          (extended-status-set-message ?extended-status-proto USER
            (str-cat "Skill " ?skill-id " succeeded without providing a result, "
              "but a result of type " ?return-value-message-name " was expected"))
          (extended-status-set-instructions ?extended-status-proto
            "Make sure the skill always returns a result as it specifies")
          (bind ?result-state EXECUTION-FAILED)
        )
    )
  )
  (if (<> ?return-value-proto-id 0) then
    (pb-remove ?return-value-proto-id)
  )
  (printout debug (str-cat "Action " (plan-action-tostring ?plan-id ?action-id)
            " (skill " ?skill-id ") finished in " ?result-state) crlf)
  (modify ?af (state ?result-state)
              (world-id-execution-after
                (world-clone ?world-id "skill_succeeded" ?trace-span-id))
              (execution-end-time (now))
              (extended-status-proto-id ?extended-status-proto))

  (if (eq ?state CANCELLATION-PENDING) then
    (action-execution-cleanup-skill-status-cancelling ?uid)
  )
  (retract ?sf)
)

(defrule action-execution-skill-failed
  "Observe skill status by ClipsSkillDispatcher for failure"
  ; Note that here, we also expect that skills can go from SELECTED/PENDING
  ; straight to failed. This could occur if the skill is not known or the gRPC
  ; service cannot be reached.
  (world (id ?world-id))
  ?af <- (plan-action (uid ?uid) (skill-id ?skill-id)
                      (state ?state&SELECTED|PENDING|RUNNING|
                             CANCELLATION-REQUESTED|CANCELLATION-PENDING|
                             CANCELING|CANCELING-EXECUTION-TIMEOUT)
                      (span-reference-id ?trace-span-id))
  ?sf <- (skill-status (action-id ?uid) (message ?message)
                       (status FAILED)
                       (extended-status-proto-id ?es-proto))
  (skill-info (skill-id ?skill-id))
 =>
  (modify ?af (state EXECUTION-FAILED)
              (world-id-execution-after
                (world-clone ?world-id "skill_failed" ?trace-span-id))
              (execution-end-time (now))
              (extended-status-proto-id ?es-proto))

  (if (eq ?state CANCELLATION-PENDING) then
    (action-execution-cleanup-skill-status-cancelling ?uid)
  )
  (retract ?sf)
)
