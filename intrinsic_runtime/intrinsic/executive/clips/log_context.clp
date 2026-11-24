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

; Function to generate a logger Context proto.

; --------------------------------- FUNCTIONS ---------------------------------
; Creates a Context proto filled with the given IDs.
(deffunction log-context-create (?session-log-id ?plan-log-id
                                 ?plan-action-log-id ?parent-log-id
                                 ?operation-name)
  (bind ?p (pb-create "intrinsic_proto.data_logger.Context"))
  (if (neq ?session-log-id 0) then
    (pb-set-field ?p "executive_session_id" ?session-log-id)
  )
  (if (neq ?plan-log-id 0) then
    (pb-set-field ?p "executive_plan_id" ?plan-log-id)
  )
  (if (neq ?plan-action-log-id 0) then
    (pb-set-field ?p "executive_plan_action_id" ?plan-action-log-id)
  )
  (if (neq ?parent-log-id 0) then
    (pb-set-field ?p "parent_skill_id" ?parent-log-id)
  )
  (if (neq ?operation-name "") then
    (pb-set-map-value ?p "labels" "executive_operation_name" ?operation-name)
  )
  (do-for-fact ((?flag flag)) (and (eq ?flag:name deployment_id)
                                   (eq ?flag:type STRING))
    (pb-set-field ?p "deployment_id" ?flag:value)
  )
  (return ?p)
)
