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

; Conflict-waiting lifecycle and early re-projection rules for plan-actions.

; This file requires plan.clp to be loaded.

; ----------------------------------- RULES -----------------------------------

(defrule conflict-wait-dependency-cleanup-inactive-blocker
  "Retract dependency when blocker action is no longer active."
  ?dep <- (action-conflict-dependency (blocker-action-uid ?blocker-uid))
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

(defrule conflict-wait-auto-resume
  "Auto-resume waiting action when all blocker dependencies have cleared."
  ?af <- (plan-action (uid ?waiting-uid) (state CONFLICT-WAITING))
  (not (action-conflict-dependency (waiting-action-uid ?waiting-uid)))
 =>
  (plan-action-reset-for-reprojection ?af)
)

(defrule conflict-wait-reproject-on-volume
  "REPROJECT-ON-VOLUME: A blocker of a waiting action is running and has added
  footprint volumes to the world which were not visible to the waiting action's
  last projection. Re-project right away so it can try to plan around them."
  ?af <- (plan-action (uid ?waiting-uid) (state CONFLICT-WAITING)
                      (conflict-handling-mode REPROJECT-ON-VOLUME)
                      (action-uids-running-at-projection $?running-at-projection)
                      (span-reference-id ?span-id))
  ; Only FOOTPRINT-CONFLICT dependencies are eligible for early re-projection.
  ; PROJECTION-FAILED dependencies must wait until blockers finish.
  (action-conflict-dependency (waiting-action-uid ?waiting-uid)
                              (blocker-action-uid ?blocker-uid)
                              (wait-reason FOOTPRINT-CONFLICT))
  ; ?blocker-uid was not running when ?waiting-uid started projecting, so its
  ; footprint volumes were likely not in the world snapshot used by that
  ; projection.
  (test (not (member$ ?blocker-uid ?running-at-projection)))
  ; Check whether ?blocker-uid has now transitioned to RUNNING (=at this point
  ; swept volumes must have been added to the world).
  (plan-action (uid ?blocker-uid) (state RUNNING)
               (footprint-proto ?blocker-footprint&~0))
  ; The REPROJECT-ON-VOLUME mode targets footprint conflicts with volumes
  ; involved which were not visible during the projection phase. The intention
  ; is that a re-projection could solve the conflict as the skill was previously
  ; not aware of said volumes. Therefore, we ignore blockers which only added
  ; equipment/object-only footprints.
  (test (> (pb-get-repeated-length ?blocker-footprint "volume") 0))
 =>
  (if (<> ?span-id ?*TRACING-INVALID-SPAN-ID*) then
    (span-add-event-annotation ?span-id "conflict_reprojecting")
  )
  (plan-action-reset-for-reprojection ?af)
)

(defrule conflict-wait-cleanup-orphan-dependencies
  "Retract dependency if waiting action is no longer in CONFLICT-WAITING state."
  ?dep <- (action-conflict-dependency (waiting-action-uid ?waiting-uid))
  (not (plan-action (uid ?waiting-uid) (state CONFLICT-WAITING)))
 =>
  (printout warning "Orphaned action-conflict-dependency found for action "
                    ?waiting-uid ". Cleaning up." crlf)
  (retract ?dep)
)
