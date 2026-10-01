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

; Consistency checks for execution tracing.

; This file checks whether those assumptions are satisfied.

; ----------------------------------- RULES -----------------------------------

(defrule skill-info-ambiguous-ids
  "There should only be one skill-info per skill-id."
  ?s1 <- (skill-info (skill-id ?skill-id))
  ?s2 <- (skill-info (skill-id ?skill-id))
  (test (neq ?s1 ?s2))
  (test (< (fact-index ?s1) (fact-index ?s2)))

  (not (error (name SKILL-INFO-AMBIGUOUS-IDS) (data SKILL-ID ?skill-id)))
 =>
  (assert (error (type FATAL)
                 (name SKILL-INFO-AMBIGUOUS-IDS)
                 (data SKILL-ID ?skill-id)
                 (message (str-cat
                   "Found two skill-infos with the same skill-id " ?skill-id))))
)
