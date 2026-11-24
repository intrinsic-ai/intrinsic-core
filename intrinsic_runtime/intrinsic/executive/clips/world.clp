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

; --------------------------------- TEMPLATES ---------------------------------

; Fact representing the current Intrinsic world proto.
; There can only be a single fact of this type.
(deftemplate world
  (slot id (type STRING) (default ?NONE))
  (multislot time (type INTEGER) (cardinality 2 2) (default-dynamic (now)))
)

(deftemplate world-request
  (slot type (type SYMBOL) (allowed-values DELETE))
  (slot world-id (type STRING))
  (multislot time (type INTEGER) (cardinality 2 2) (default-dynamic (now)))
)

; This template is used to communicate from the ClipsWorld.
(deftemplate world-update
  ; UID for action for which conflicts have been checked
  (slot action-uid (type SYMBOL))
  ; Conflict status of action
  (slot status (type SYMBOL)
        (allowed-values NOT-CHECKED FOOTPRINT-CONFLICT NO-CONFLICT)
        (default NOT-CHECKED))
  ; list of footprint ids which are in conflict
  (multislot footprint-conflict-proto-ids (type INTEGER))
  ; list of action uids which are in conflict
  (multislot conflicting-action-uids (type SYMBOL))
)
; --------------------------------- FUNCTIONS ---------------------------------

; Provided through C++ implementation (clips_world.{h,cc}):
; - world-download

(deffunction world-get-id ()
  (do-for-fact ((?w world)) TRUE (return ?w:id))
  (return "--unknown--")
)

; Resets the world.
;
; This removes the world fact and expects world-service-try-restore-snapshot to
; re-create it.
(deffunction world-reset ()
  (do-for-fact ((?w world)) TRUE (retract ?w))
)

; ----------------------------------- RULES -----------------------------------
; NOTE:
; Each and every modification to the world fact shall only be made from rules
; defined in this very file.

(defrule world-ambiguous
  "Print a warning if we have two differing worlds at the same time"
  ?w1 <- (world)
  ?w2 <- (world)
  (test (neq ?w1 ?w2))
 =>
  (assert (error (name WORLD-AMBIGUOUS) (type FATAL)
                 (message "Multiple worlds in the fact base")))
)

(defrule world-fake-world
  "Use the default world id 'world' when no other world is specified"
  (not (world))
  (not (flag (name world-initial-id)))
 =>
  (assert (world (id "world")))
)
