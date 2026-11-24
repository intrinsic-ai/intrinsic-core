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

; World related functions and rules that depend on WorldService availability

; --------------------------------- TEMPLATES ---------------------------------

; Fact representing the current Intrinsic world proto.
(deftemplate world-snapshot
  (slot world-id (type STRING) (default ?NONE))
  (slot from-world-id (type STRING) (default ?NONE))
  (multislot time (type INTEGER) (cardinality 2 2) (default-dynamic (now)))
)

; --------------------------------- FUNCTIONS ---------------------------------

(deffunction world-get-object-world-proto ()
  (return (world-download-object-world (world-get-id)))
)

; ----------------------------------- RULES -----------------------------------
; NOTE:
; Each and every modification to the world fact shall only be made from rules
; defined in this very file.

(defrule world-delete-request
  "If a plan action finished, update world if the action's one is more recent."
  ?wr <- (world-request (world-id ?world-id) (type DELETE) (time $?req-time))
  ; There is no other preceeding request which should be processed before
  (not
   (world-request (time $?other-req-time&:(time> ?req-time ?other-req-time))))
 =>
  (world-delete ?world-id)
  (retract ?wr)
)

(defrule world-service-set-world-id
  "Restore the world from the snapshot on executive reset"
  (executive-init)
  (not (world))
  (flag (name world-id) (type STRING) (value ?world-id))
 =>
  (printout debug "Using world '" ?world-id "'" crlf)
  (assert (world (id ?world-id)))
)
