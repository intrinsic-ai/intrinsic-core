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
              (projection-running-actions ?running-actions))
)

(defrule action-projection-skill-succeeded
  "Skill projection by ClipsSkillDispatcher has completed."
  ?af <- (plan-action (id ?id) (plan-id ?plan-id) (uid ?uid) (state PROJECTING)
                      (world-id-projection ?world-id-projection)
                      (skill-id ?skill-id)
                      (behavior-call-proto-id ?behavior-call-proto))
  ?sf <- (skill-status (action-id ?uid)
                       (status PROJECTED))
  (skill-info (skill-id ?skill-id))
 =>
  (printout debug "Skill projection for action "
                  (plan-action-tostring ?plan-id ?id)
                  " (skill " ?skill-id ") has succeeded" crlf)
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

(defrule action-projection-skill-failed
  "Observe skill status by ClipsSkillDispatcher for projection failure"
  ; Note that here, we also expect that skills can go from SELECTED/PENDING
  ; straight to failed. This could occur if the skill is not known or the gRPC
  ; service cannot be reached.
  ?af <- (plan-action (uid ?uid) (state ?state&PROJECTING) (skill-id ?skill-id)
                      (projection-running-actions $?running-actions)
                      (world-id-projection ?world-id-projection))
  ?sf <- (skill-status (action-id ?uid) (status FAILED) (message ?message)
                       (extended-status-proto-id ?es-proto))
  (skill-info (skill-id ?skill-id))
 =>
  ; Some actions were running and thus footprints locked, so try again after
  ; those actions completed
  ; TODO(b/236694783): Re-enable retrying projection on conflict in BTs
  (if (neq ?world-id-projection "") then
    (assert (world-request (type DELETE) (world-id ?world-id-projection))))
  (modify ?af (state EXECUTION-FAILED) (projection-end-time (now))
              (world-id-projection "")
              (extended-status-proto-id ?es-proto))
  (retract ?sf)
)
