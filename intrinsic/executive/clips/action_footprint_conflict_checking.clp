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

; Check action footprint for conflicts

; This file requires world.clp to be loaded.

; ----------------------------------- RULES -----------------------------------

(defrule footprint-conflict-detection
  "Start conflict detection after corresponding action was projected."
  (world (id ?world-id))
  (behavior-tree (plan-id ?plan-id) (state RUNNING))
  ?af <- (plan-action (plan-id ?plan-id) (id ?id) (uid ?uid)
                      (state SELECTED|PROJECTED)
                      (skill-id ?skill-id) (executable TRUE)
                      (log-id ~0)
                      (footprint-proto ?footprint-proto)
                      (span-reference-id ?span-id))
  (skill-info (skill-id ?skill-id))
  ; Crucial: Sequentialize footprint conflict checking, so that multiple
  ; potentially conflicting skills cannot result in no-conflict as they are not
  ; running, yet.
  (not (exists (plan-action (plan-id ?plan-id) (state FOOTPRINT-CHECKING))))
 =>
  (printout debug "Checking for conflicts " (plan-action-tostring ?plan-id ?id)
                  " (skill " ?skill-id ")" crlf)
  (bind ?running-action-uids (create$))
  (bind ?running-footprint-ids (create$))
  (do-for-all-facts ((?pa plan-action))
      (and (isoneof ?pa:state PENDING READY RUNNING
                      CANCELLATION-REQUESTED CANCELLATION-PENDING CANCELING)
           (<> ?pa:footprint-proto 0))

    (bind ?running-action-uids
      (append$ ?running-action-uids ?pa:uid))
    (bind ?running-footprint-ids
      (append$ ?running-footprint-ids ?pa:footprint-proto))
  )
  (if (<> ?span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?span-id "footprint_checking")
  )
  (world-footprint-conflict ?world-id ?footprint-proto ?uid
    ?running-action-uids ?running-footprint-ids)
  (modify ?af (state FOOTPRINT-CHECKING))
)

(defrule footprint-conflict-detection-no-conflicts
  "Footprint conflict detection did not find any conflicts so
  action is ready for execution."
  ?af <- (plan-action (plan-id ?plan-id) (id ?id) (uid ?uid)
                      (state FOOTPRINT-CHECKING) (skill-id ?skill-id)
                      (span-reference-id ?span-id))
  (skill-info (skill-id ?skill-id))
  ?wu <- (world-update (action-uid ?uid) (status NO-CONFLICT))
 =>
  (if (<> ?span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?span-id "ready")
  )
  (modify ?af (state READY))
  (retract ?wu)
)

(defrule footprint-conflict-detection-conflicts-found-fail
  "Footprint conflict detection did find at least one conflict so action
  needs to be rescheduled at a later time (UNSPECIFIED or FAIL mode)."
  ?af <- (plan-action (id ?id) (plan-id ?plan-id) (uid ?uid)
                      (state FOOTPRINT-CHECKING) (skill-id ?skill-id)
                      (conflict-handling-mode ?ch-mode)
                      (footprint-proto ?footprint-proto))
  (test (not (plan-action-should-wait-on-conflict ?ch-mode)))
  (skill-info (skill-id ?skill-id))
  ?wu <- (world-update (action-uid ?uid) (status FOOTPRINT-CONFLICT)
                       (footprint-conflict-proto-ids $?deps))
 =>
  (bind ?conflicts (create$))
  (bind ?conflict-actions (create$))
  (foreach ?dep ?deps
    (do-for-fact ((?pa plan-action)) (eq ?pa:footprint-proto ?dep)
      (bind ?conflict-actions (append$ ?conflict-actions (str-cat ?pa:id)))
      (bind ?conflicts (str-cat (plan-action-tostring ?pa:plan-id ?pa:id)
                                ": Footprint proto '" (pb-tostring ?dep) "'"))
    )
  )
  (bind ?es-proto (extended-status-create 18201 ERROR))
  (extended-status-set-message ?es-proto USER
    (str-cat "Found conflicts in " (plan-action-tostring ?plan-id ?id)
                                   ": Footprint proto '"
                                   (pb-tostring ?footprint-proto) "'. "
                                   "Conflicting with "
                                   (str-join ", " ?conflicts)))
  (extended-status-set-message ?es-proto DEBUG (str-cat "Internal action id: "
    ?id ", conflicting action ids: " (str-join ", " ?conflict-actions)))

  (modify ?af (state EXECUTION-FAILED) (extended-status-proto-id ?es-proto))
  (retract ?wu)
)

(defrule footprint-conflict-detection-conflicts-found-wait
  "Footprint conflict detection found conflicts in WAIT or REPROJECT-ON-VOLUME
  mode. Transition action to FOOTPRINT-CONFLICT-WAITING, assert blocker
  dependency facts, and set trace event."
  ?af <- (plan-action (uid ?uid) (state FOOTPRINT-CHECKING)
                      (conflict-handling-mode ?ch-mode)
                      (span-reference-id ?span-id))
  (test (plan-action-should-wait-on-conflict ?ch-mode))
  ?wu <- (world-update (action-uid ?uid) (status FOOTPRINT-CONFLICT)
                       (conflicting-action-uids $?blockers))
 =>
  ; Footprint waiting doesn't do anything with the conflict, but just inserts
  ; the dependency fact to be resolved by other rules and then goes into waiting.
  (foreach ?blocker ?blockers
    (assert (action-footprint-dependency (waiting-action-uid ?uid)
                                         (blocker-action-uid ?blocker)))
  )
  (if (<> ?span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?span-id "footprint_conflict_waiting")
  )
  (modify ?af (state FOOTPRINT-CONFLICT-WAITING))
  (retract ?wu)
)

(defrule footprint-dependency-cleanup-inactive-blocker
  "Retract dependency when blocker action is no longer active."
  ?dep <- (action-footprint-dependency (blocker-action-uid ?blocker-uid))
  ; Note that this *must not* capture positive facts, e.g., a blocker
  ; plan-action being SUCCEEDED or FAILED. If the same CLIPS run would reset
  ; these other actions, e.g., as part of a loop node, then the SUCCEEDED facts
  ; are gone and would never be picked up.
  ; The negative facts logic also makes sense semantically: If there is no more
  ; active blocking action (either because it's gone completely or if it's not
  ; blocking any more [succeeded]), then we can remove the dependency.
  (not (plan-action (uid ?blocker-uid)
                    (state
                      PENDING|READY|RUNNING|
                      CANCELLATION-REQUESTED|CANCELLATION-PENDING|CANCELING|
                      CANCELING-EXECUTION-TIMEOUT)))
 =>
  (retract ?dep)
)

(defrule footprint-waiting-auto-resume
  "Auto-resume waiting action when all blocker dependencies have cleared."
  ?af <- (plan-action (uid ?waiting-uid) (state FOOTPRINT-CONFLICT-WAITING))
  (not (action-footprint-dependency (waiting-action-uid ?waiting-uid)))
 =>
  (plan-action-reset-for-reprojection ?af)
)

(defrule footprint-reproject-on-volume
  "REPROJECT-ON-VOLUME: A blocker of a waiting action is running and has added
  footprint volumes to the world which were not visible to the waiting action's
  last projection. Re-project right away so it can try to plan around them."
  ?af <- (plan-action (uid ?waiting-uid) (state FOOTPRINT-CONFLICT-WAITING)
                      (conflict-handling-mode REPROJECT-ON-VOLUME)
                      (action-uids-running-at-projection $?running-at-projection)
                      (span-reference-id ?span-id))
  (action-footprint-dependency (waiting-action-uid ?waiting-uid)
                               (blocker-action-uid ?blocker-uid))
  ; ?blocker-uid was not running when ?waiting-uid started projecting, so its
  ; footprint volumes were likely not in the world snapshot used by that
  ; projection.
  (test (not (member$ ?blocker-uid ?running-at-projection)))
  ; Check whether ?blocker-uid has now transitioned to RUNNING (=at this point
  ; swept volumes must have been added to the world).
  (plan-action (uid ?blocker-uid) (state RUNNING)
               (footprint-proto ?blocker-footprint&~0))
  ; We only care for blockers which added volumes. Equipment/object-only
  ; footprints add nothing to the world that re-projection could plan around.
  (test (> (pb-get-repeated-length ?blocker-footprint "volume") 0))
 =>
  (if (<> ?span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?span-id "footprint_conflict_reprojecting")
  )
  (plan-action-reset-for-reprojection ?af)
)

(defrule footprint-dependency-cleanup-orphan-waiting-action
  "Retract dependency if waiting action is no longer in FOOTPRINT-CONFLICT-WAITING state."
  ?dep <- (action-footprint-dependency (waiting-action-uid ?waiting-uid))
  (not (plan-action (uid ?waiting-uid) (state FOOTPRINT-CONFLICT-WAITING)))
 =>
  (printout warning "Orphaned action-footprint-dependency found for action " ?waiting-uid ". Cleaning up." crlf)
  (retract ?dep)
)

(defrule footprint-checking-skill-failed
  "Observe skill status by ClipsSkillDispatcher for footprint checking failure"
  (world (id ?world-id))
  ?af <- (plan-action (uid ?uid) (state FOOTPRINT-CHECKING) (skill-id ?skill-id))
  ?sf <- (skill-status (action-id ?uid) (status FAILED) (message ?message)
                       (extended-status-proto-id ?es-proto))
  (skill-info (skill-id ?skill-id))
 =>
  (modify ?af (state EXECUTION-FAILED)
              (projection-end-time (now))
              (extended-status-proto-id ?es-proto))
  (retract ?sf)
)
