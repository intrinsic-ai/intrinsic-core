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

; Action projection through skills

; This file requires skill_info.clp to be loaded (typically done during
; ClipsSkillDispatcher::Init()).

; ----------------------------------- RULES -----------------------------------

(defrule action-projection-skill-start
  "Start skill projection  after corresponding action was selected."
  (declare (salience ?*SALIENCE-ACTION-PROJECTION*))
  (world (time $?world-time) (id ?world-id))
  (executive-state (session-log-id ?session-log-id))
  (behavior-tree (id ?bt-id) (plan-id ?plan-id) (operation-name ?operation-name)
                 (state RUNNING) (log-id ?bt-log-id&~0))
  (behavior-tree-node (tree-id ?tree-id) (task-action-uid ?uid)
                      (name ?node-display-name))
  ?af <- (plan-action (plan-id ?plan-id) (id ?id) (uid ?uid) (state SELECTED)
                      (skill-id ?skill-id) (executable TRUE)
                      (behavior-call-proto-id ?behavior-call-proto)
                      (log-id ?action-log-id&~0)
                      (span-reference-id ?parent-span-id))
  (skill-info (skill-id ?skill-id)
              (parameter-descriptor-pool-id ?parameter-descriptor-pool-id))
 =>
  (bind ?display-name
    (if (neq ?node-display-name "") then (str-cat " (" ?node-display-name ")")
                                    else ""))
  (printout t "Starting projection of skill " ?skill-id ?display-name crlf)

  (bind ?message "")
  (bind ?parent-log-id (log-find-parent-log-id ?bt-id))
  (bind ?log-context-proto
    (log-context-create ?session-log-id ?bt-log-id ?action-log-id
                        ?parent-log-id ?operation-name))

  ; Snapshot conservative lower (action-uids-running-at-projection) and upper
  ; (action-uids-with-volumes-at-projection) bounds on actions with active
  ; footprint volumes at world-clone. Used by REPROJECT-ON-VOLUME conflict
  ; handling mode. See plan.clp for details.
  (bind ?action-uids-running-at-projection (create$))
  (bind ?action-uids-with-volumes-at-projection (create$))
  (do-for-all-facts ((?pa plan-action))
      (isoneof ?pa:state PENDING RUNNING
               CANCELLATION-REQUESTED CANCELLATION-PENDING
               CANCELING CANCELING-EXECUTION-TIMEOUT)
    (if (eq ?pa:state RUNNING) then
      (bind ?action-uids-running-at-projection
        (append$ ?action-uids-running-at-projection ?pa:uid))
    )
    (if (and (<> ?pa:footprint-proto 0)
             (> (pb-get-repeated-length ?pa:footprint-proto "volume") 0)) then
      (bind ?action-uids-with-volumes-at-projection
        (append$ ?action-uids-with-volumes-at-projection ?pa:uid))
    )
  )
  (bind ?world-id-projection (world-clone ?world-id "project" ?parent-span-id))

  (skill-project-async ?uid
                       ?world-id-projection
                       ?behavior-call-proto
                       ?log-context-proto
                       ?parent-span-id
                       ?parameter-descriptor-pool-id)

  (bind ?deployment-id (pb-get-field ?log-context-proto "deployment_id"))
  (if (neq ?deployment-id MISSING-FIELD) then
    (span-add-attribute ?parent-span-id "deployment_id" ?deployment-id)
  )

  (pb-remove ?log-context-proto)
  (bind ?running-actions (create$))
  (do-for-all-facts ((?pa plan-action)) (isoneof ?pa:state PENDING RUNNING)
    (bind ?running-actions (append$ ?running-actions ?pa:id))
  )
  (modify ?af (state PROJECTING)
              (world-id-projection ?world-id-projection)
              (projection-start-time (now))
              (projection-running-actions ?running-actions)
              (action-uids-running-at-projection
                ?action-uids-running-at-projection)
              (action-uids-with-volumes-at-projection
                ?action-uids-with-volumes-at-projection))
)

(defrule action-projection-skill-succeeded
  "Skill projection by ClipsSkillDispatcher has completed."
  ?af <- (plan-action (id ?id) (plan-id ?plan-id) (uid ?uid) (state PROJECTING)
                      (world-id-projection ?world-id-projection)
                      (skill-id ?skill-id)
                      (behavior-call-proto-id ?behavior-call-proto)
                      (span-reference-id ?span-id))
  ?sf <- (skill-status (action-id ?uid)
                       (status PROJECTED))
  (skill-info (skill-id ?skill-id))
 =>
  (printout debug "Skill projection for action "
                  (plan-action-tostring ?plan-id ?id)
                  " (skill " ?skill-id ") has succeeded" crlf)
  (if (<> ?span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?span-id "projected")
  )
  ; TODO(b/204540881): if the flag is set, override whatever footprint the skill
  ; generated and assume it requires the entire universe. This will prevent and
  ; sequentialize concurrent skills and is a temporary workaround.
  (if (any-factp ((?flag flag))
    (and (eq ?flag:name assume_all_skills_lock_world_universe)
         (eq ?flag:type BOOL) (eq ?flag:value TRUE)))
   then
    (bind ?universe-footprint-proto
      (pb-create "intrinsic_proto.skills.Footprint"))
    (pb-set-field ?universe-footprint-proto "lock_the_universe" TRUE)
    (pb-set-field ?behavior-call-proto "skill_execution_data.footprint"
      ?universe-footprint-proto)
    (pb-remove ?universe-footprint-proto)
  )
  (bind ?footprint-proto 0)
  (if (pb-has-field ?behavior-call-proto "skill_execution_data.footprint") then
    (bind ?footprint-proto
      (pb-get-field ?behavior-call-proto "skill_execution_data.footprint"))
  )
  (if (neq ?world-id-projection "") then
    (assert (world-request (type DELETE) (world-id ?world-id-projection))))
  (modify ?af (state PROJECTED)
              (projection-end-time (now))
              (footprint-proto ?footprint-proto)
              (world-id-projection ""))
  (retract ?sf)
)

(defrule action-projection-skill-failed-wait
  "For skill in REPROJECT-ON-VOLUME conflict handling mode only: Gracefully
  handle projection failures in case other actions' footprint volumes were
  potentially present in the world. Transition to CONFLICT-WAITING and wait
  until those actions finish before re-projecting."
  ?af <- (plan-action (uid ?uid) (state PROJECTING) (skill-id ?skill-id)
                      (conflict-handling-mode REPROJECT-ON-VOLUME)
                      (action-uids-with-volumes-at-projection
                        $?volumes-at-projection&:(non-empty$ ?volumes-at-projection))
                      (world-id-projection ?world-id-projection)
                      (span-reference-id ?span-id))
  ?sf <- (skill-status (action-id ?uid) (status FAILED) (message ?message)
                       (extended-status-proto-id ?es-proto))
  (skill-info (skill-id ?skill-id))
 =>
  (printout t "Projection of " ?skill-id " (" ?uid ") failed, waiting for "
              (implode$ ?volumes-at-projection) " to finish: " ?message crlf)
  (if (neq ?world-id-projection "") then
    (assert (world-request (type DELETE) (world-id ?world-id-projection))))
  (if (<> ?es-proto 0) then
    (pb-remove ?es-proto))
  (foreach ?blocker ?volumes-at-projection
    (assert (action-conflict-dependency (waiting-action-uid ?uid)
                                        (blocker-action-uid ?blocker)
                                        (wait-reason PROJECTION-FAILED)))
  )
  (if (<> ?span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?span-id "projection_failed_waiting")
  )
  (modify ?af (state CONFLICT-WAITING)
              (projection-end-time (now))
              (world-id-projection ""))
  (retract ?sf)
)

(defrule action-projection-skill-failed
  "Observe skill status by ClipsSkillDispatcher for projection failure"
  ; Note that here, we also expect that skills can go from SELECTED/PENDING
  ; straight to failed. This could occur if the skill is not known or the gRPC
  ; service cannot be reached.
  ?af <- (plan-action (uid ?uid) (state ?state&PROJECTING) (skill-id ?skill-id)
                      (conflict-handling-mode ?ch-mode)
                      (action-uids-with-volumes-at-projection
                        $?volumes-at-projection)
                      (projection-running-actions $?running-actions)
                      (world-id-projection ?world-id-projection))
  (test (or (neq ?ch-mode REPROJECT-ON-VOLUME)
            (empty$ ?volumes-at-projection)))
  ?sf <- (skill-status (action-id ?uid) (status FAILED) (message ?message)
                       (extended-status-proto-id ?es-proto))
  (skill-info (skill-id ?skill-id))
 =>
  (if (neq ?world-id-projection "") then
    (assert (world-request (type DELETE) (world-id ?world-id-projection))))
  (modify ?af (state EXECUTION-FAILED) (projection-end-time (now))
              (world-id-projection "")
              (extended-status-proto-id ?es-proto))
  (retract ?sf)
)
