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

; CLIPS Flag handling

; This enables to pass simple configuration information into the CLIPS
; environment.

; --------------------------------- TEMPLATES ---------------------------------

(deftemplate flag
  "A flag is mapping a name and type to a value."
  (slot name (type SYMBOL))
  (slot type (type SYMBOL) (allowed-values STRING INTEGER FLOAT BOOL SYMBOL))
  ; Value or list of values of adhering to the type specification above.
  (slot value)
  (multislot values)
)

; Retrieves the boolean value of a flag.
;
; The flag is represented by a fact of type 'flag' with its type set to BOOL and
; its value to TRUE/FALSE.
;
; Args:
;   ?flag-name: Name of the flag as a symbol.
;
; Returns:
;   TRUE or FALSE depending on the value of the flag. FALSE if the flag isn't
;   set.
(deffunction get-flag-value (?flag-name)
  (if (any-factp ((?flag flag))
                   (and (eq ?flag:name ?flag-name)
                        (eq ?flag:type BOOL) (eq ?flag:value TRUE))) then
    (return TRUE))

  (return FALSE)
)
