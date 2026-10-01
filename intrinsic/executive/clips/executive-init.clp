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

; Main CLIPS Executive initialization file
; Additions can be made through custom initialization files.
; This init.clp file will always be loaded before others.

(defglobal
  ; Files to load during startup of the executive. There are three principal
  ; levels of files:
  ; - Core files which are mandatory for any invocation of the executive.
  ;   These files are listed in clips_init.cc.
  ; - Executive files which are independent of the particular app and required
  ;   for basic operation. These files go below into the CLIPS-FILES list.
  ; - Application-specific extension files which are only relevant for one
  ;   or at least few apps (not universally in all). These go into application
  ;   init files which are then given as additional init file to load to
  ;   the executive's --clips-files parameter. This can even be used to override
  ;   executive-init.clp and thus the files listed below.
  ?*CLIPS-FILES* =
  (create$
   "state_export.clp"
   "logging.clp"
   "world_service.clp"
   "action_projection_skills.clp"
   "action_execution_skills.clp"
   "action_execution_noop.clp"
   "action_execution_no_handlers.clp"
   "action_footprint_conflict_checking.clp"
   "blackboard_check.clp"
   "skill_reset.clp"
  )
)

(defrule executive-init-load-base-files
  (declare (salience ?*SALIENCE-HIGHER*))
  (executive-init)
 =>
  (foreach ?f ?*CLIPS-FILES*
    (printout internal "Loading " ?f crlf)
    (file-load ?f)
  )
  (printout internal "Executive base files loaded" crlf)
)

(defrule executive-init-load-user-files
  (declare (salience ?*SALIENCE-HIGH*))
  (executive-init)
  (flag (name init-files) (type STRING) (values $?init-files))
 =>
  (foreach ?f ?init-files
    (printout internal "Loading user file " ?f crlf)
    (file-load ?f)
  )
  (printout internal "Executive user files loaded" crlf)
)

(defrule executive-init-print-flags
  "Print configured flags on startup"
  (executive-init)
  (exists (flag))
 =>
  (do-for-all-facts ((?f flag)) TRUE
    (printout internal "Configured flag " ?f:name " = "
              (if (and (neq ?f:value nil) (eq ?f:type STRING)) then "\"" else "")
              (if (neq ?f:value nil) then ?f:value else "")
              (if (and (neq ?f:value nil) (eq ?f:type STRING)) then "\"" else "")
              (if (non-empty$ ?f:values)
               then (str-cat "[" (str-join ", " ?f:values) "]") else "")
              " (type " ?f:type ")" crlf)
  )
)

(defrule executive-init-file-load-failed
  "If any intended file fails to load, post a fatal error."
  (file-info (loaded FALSE) (filename ?file) (error-msg ?error-message))
 =>
  (assert (error (type FATAL) (name INIT-FILE-LOAD-FAILED)
                 (message ?error-message) (privileged TRUE)))
)
