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

; Templates, functions and rules that are used to manage long-running operations
; in the Conductor service.
;
; Conductor exposes an LRO API to prepare the backend services for running a new
; executive operation. When an executive operation is started, the conductor is
; requested to make necessary preparations and a CLIPS fact is created to
; represent the pending conductor operation. The Conductor service is
; periodically polled from CLIPS to check if the conductor operation is done.

; --------------------------------- TEMPLATES ---------------------------------

; Template representing a conductor client operation to prepare the backend
; services for running a new executive operation.
(deftemplate conductor-preparation-client-operation
  ; Name of the executive operation for which the conductor is preparing.
  (slot operation-name (type STRING) (default ?NONE))

  ; Name of the conductor client operation.
  (slot conductor-operation-name (type STRING))

  ; Indicates whether the conductor operation is done.
  (slot is-done (type SYMBOL) (allowed-values FALSE TRUE) (default FALSE))

  ; Indicates whether the conductor operation failed.
  (slot has-error (type SYMBOL) (allowed-values FALSE TRUE) (default FALSE))

  ; The extended status proto describing the error if has-error is TRUE.
  (slot extended-status-proto-id (type INTEGER))

  ; Span ID for conductor preparation.
  (slot span-reference-id (type INTEGER) (default ?*TRACING-INVALID-SPAN-ID*))

  ; The last time when the conductor was polled for the status of the operation.
  (multislot last-checked (type INTEGER) (cardinality 2 2)
                          (default-dynamic (now)))

  ; The last time when the status of the operation was updated.
  (multislot last-updated (type INTEGER) (cardinality 2 2)
                          (default-dynamic (now)))

  ; The scene id to be associated with the executive operation once the client
  ; operation completes. Default is empty string.
  (slot scene-id (type STRING))
)

; Template representing an update on a pending conductor client operation from
; the conductor service.
(deftemplate conductor-update-client-operation
  (slot conductor-operation-name (type STRING) (default ?NONE))
  (slot is-done (type SYMBOL) (allowed-values FALSE TRUE) (default FALSE))
  (slot has-error (type SYMBOL) (allowed-values FALSE TRUE) (default FALSE))

  ; The extended status proto describing the error if has-error is TRUE.
  (slot extended-status-proto-id (type INTEGER))

  ; Belief world id returned by the conductor. Will be empty if is-done is FALSE
  ; or has-error is TRUE.
  (slot world-id (type STRING))
)

; Template representing an external request to modify a pending conductor client
; operation.
(deftemplate conductor-modify-client-operation
  (slot action (type SYMBOL) (allowed-values STOP-PREPARATION) (default ?NONE))
  (slot conductor-operation-name (type STRING) (default ?NONE))
)

; --------------------------------- FUNCTIONS ---------------------------------

; Deletes the conductor preparation fact for a given operation name.
;
; This function deletes the fact and calls the conductor to cancel the client
; operation if it is still pending. This function also deletes the extended
; status proto associated with the fact if it was set. Callers should ensure to
; copy over the extended status proto from the fact if it was set before calling
; this function.
;
; Args:
;   ?operation-name: the operation whose associated conductor preparation
;     client operation fact is to be deleted.
(deffunction conductor-preparation-client-operation-delete (?operation-name)
  "Deletes the conductor preparation fact for a given operation name."
  (do-for-fact ((?cf conductor-preparation-client-operation))
               (eq ?cf:operation-name ?operation-name)
    (if (not ?cf:is-done) then
      (conductor-cancel-preparation-async
        ?cf:conductor-operation-name ?cf:span-reference-id)
    )
    (span-end ?cf:span-reference-id)
    (pb-remove ?cf:extended-status-proto-id)
    (retract ?cf)
  )
)

; Calls PrepareProcessResume RPC on the conductor service with tracing.
;
; Args:
;   ?parent-span-id: the span id under which the rpc span will be nested.
;
; Returns:
;   A multifield-pair with TRUE/FALSE and as the second value an optional
;   extended status proto id if the RPC failed.
(deffunction conductor-prepare-process-resume-with-tracing (?parent-span-id)
  "Calls PrepareProcessResume RPC on the conductor service with tracing."
  (bind ?conductor-notify-span
    (span-start "Notify conductor process resuming" ?parent-span-id
      "conductor-prepare-process-resume"))
  (bind ?conductor-result
    (conductor-prepare-process-resume ?conductor-notify-span))
  (span-end ?conductor-notify-span)

  (if (not (result-ok ?conductor-result)) then
    (bind ?es-proto-id (extended-status-create 21110 ERROR))
    (extended-status-set-message ?es-proto-id USER
      "Internal error, please submit a support ticket.")
    (extended-status-set-message ?es-proto-id DEBUG
      (result-error ?conductor-result))
    (return (result-create FALSE ?es-proto-id))
  )
  (return (result-create TRUE 0))
)

; --------------------------------- RULES ---------------------------------

(defrule conductor-check-preparation-status
  "Check periodically if the conductor finished preparations."
  (flag (name conductor-client-operation-poll-interval) (type FLOAT)
        (value ?interval))
  (time (timestamp $?now))
  ?cf <- (conductor-preparation-client-operation
           (is-done FALSE) (conductor-operation-name ?conductor-operation-name)
           (span-reference-id ?span-id)
           (last-checked $?last-checked&:(timeout ?now ?last-checked
                                                  ?interval))
           ; Don't trigger check if the operation status was updated in this
           ; loop.
           (last-updated $?last-updated&:(time> ?now ?last-updated)))
  ; Don't trigger the check if there is a pending update.
  (not (conductor-update-client-operation
         (conductor-operation-name ?conductor-operation-name)))
 =>
  ; Start an async check that the conductor operation is done. Once a response
  ; is received, the conductor-client-operation-update rule will be triggered if
  ; the operation is done.
  (conductor-check-preparation-is-done-async ?conductor-operation-name ?span-id)
  (modify ?cf (last-checked ?now))
)

(defrule conductor-client-operation-update
  "Update the state of a conductor client operation."
  ?uf <- (conductor-update-client-operation
           (conductor-operation-name ?conductor-op-name) (is-done ?is-done)
           (has-error ?has-error) (world-id ?target-world-id)
           (extended-status-proto-id ?es-proto-id))
  ?cf <- (conductor-preparation-client-operation
           (conductor-operation-name ?conductor-op-name)
           (span-reference-id ?span-id))
  (time (timestamp $?now))
 =>
  (if ?is-done
   then
    (if (not ?has-error) then
      (bind ?executive-world-id (world-get-id))
      (if (neq ?target-world-id ?executive-world-id) then
        ; TODO(b/380644764) Use the returned belief world id for the current
        ; executive operation.
        (bind ?es-proto-id (extended-status-create 21101 ERROR))
        (extended-status-set-message ?es-proto-id USER
          (str-cat "Internal error: conductor response is invalid. Please "
                   "submit a support ticket."))
        (extended-status-set-message ?es-proto-id DEBUG
          (str-cat "Conductor returned a different belief world id '"
                   ?target-world-id "' than the configured world id '"
                   ?executive-world-id "'."))
        (bind ?has-error TRUE)
      )
    )

    ; End the span and reset the span id below.
    (span-end ?span-id)

    ; Only update the is-done and has-error slots of the conductor preparation
    ; fact if the operation is done. This is to prevent accidentally setting the
    ; operation to not done again if multiple updates are received and asserted
    ; out of order.
    ; The es-proto-id is transferred from the update fact to the conductor
    ; preparation fact. So the proto does not need to be cleaned up here. It
    ; will be cleaned up in conductor-preparation-client-operation-delete.
    (modify ?cf (is-done TRUE) (has-error ?has-error) (last-updated ?now)
                (extended-status-proto-id ?es-proto-id)
                (span-reference-id ?*TRACING-INVALID-SPAN-ID*))
   else
    ; If the operation is not done, only update the last-updated slot.
    (modify ?cf (last-updated ?now))
  )

  (retract ?uf)
)

(defrule conductor-cleanup-stale-client-operation-update
  "Retract stale update fact in case the client operation was already deleted."
  ?uf <- (conductor-update-client-operation
           (conductor-operation-name ?conductor-op-name))
  (not (conductor-preparation-client-operation
         (conductor-operation-name ?conductor-op-name)))
 =>
  ; This can happen if conductor-preparation-client-operation-delete is called
  ; while conductor-check-preparation-is-done-async is still running in the
  ; background. Then the conductor-update-client-operation fact will be added
  ; after the conductor-preparation-client-operation fact has been retracted.
  (retract ?uf)
)

(defrule conductor-cleanup-stale-modify-client-operation
  "Retract stale modify client operation fact."
  ?mf <- (conductor-modify-client-operation
           (conductor-operation-name ?conductor-op-name))
  (not (conductor-preparation-client-operation
         (conductor-operation-name ?conductor-op-name)))
 =>
  ; Retract modify client operation fact if the client operation fact was
  ; already retracted. This can happen if the executive operation failed to
  ; start and was subsequently deleted, causing the client operation fact to be
  ; retracted as well.
  (retract ?mf)
)
