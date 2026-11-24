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

; CLIPS utilities

(defglobal
  ?*PROTO-DESCRIPTOR-POOL-GENERATED* = 1
  ?*PROTO-DESCRIPTOR-POOL-STANDARD-MESSAGES* = 3
)

; Append the elements ?items to the multifield ?list
(deffunction append$ (?list $?items)
  (insert$ ?list (+ (length$ ?list) 1) ?items)
)

; Check if a list is empty
(deffunction empty$ ($?list)
  (return (= (length$ ?list) 0))
)

; Check if a list is non-empty
(deffunction non-empty$ ($?list)
  (return (> (length$ ?list) 0))
)

; Shortcut for (member$) that only ever returns TRUE/FALSE.
;
; Args:
;   ?needle item to check for existence of
;   ?haystack list to search for ?needle
;
; Returns:
;   TRUE if ?needle is in ?haystack, FALSE otherwise. This does NOT return
;   the index of the element if found!
(deffunction in$ (?needle $?haystack)
  (if (member$ ?needle ?haystack)
   then (return TRUE)
   else (return FALSE)
  )
)


; Determines if the result of a function is valid.
; This assumes the common pattern of returning a multifield, where the first
; entry is TRUE/FALSE signaling success or failure of the operation.
;
; Returns:
;   TRUE or FALSE if the ?result is valid or not.
(deffunction result-ok (?result)
  (if (empty$ ?result) then
    (return FALSE)
  )
  (if (eq (nth$ 1 ?result) TRUE) then
    (return TRUE)
  )
  (return FALSE)
)

; Creates a result compatible with result-ok/result-value/result-error.
;
; Args:
;   ?ok: Either TRUE or FALSE if the result is OK or not.
;   ?value-or-error:  If ?ok is TRUE this contains the value of the result,
;                     otherwise an error value, e.g., an error message
;
; Returns:
;   A multifield suitable to pass to result-* functions.
(deffunction result-create (?ok $?value-or-error)
  (if (eq (member$ ?ok (create$ TRUE FALSE)) FALSE) then
    (printout error
      (str-cat "result-create called with invalid ?ok value: " ?ok) crlf)
  )
  (return (create$ ?ok ?value-or-error))
)

(deffunction str-capitalize (?str)
  "Capitalize the given string ?str, i.e., hello -> Hello."
  (bind ?len (str-length ?str))
  (if (= ?len 0) then
    (return ?str)
  )
  (return
    (str-cat (upcase (sub-string 1 1 ?str)) (sub-string 2 ?len ?str)))
)

; Join a list of elements to a string with a given separator.
(deffunction str-join (?separator $?list)
  (bind ?rv "")
  (bind ?first TRUE)
  (foreach ?s ?list
    (if ?first then (bind ?first FALSE) else (bind ?rv (str-cat ?rv ?separator)))
    (bind ?rv (str-cat ?rv ?s))
  )
  (return ?rv)
)

; Joins elements of a proto path with ".".
;
; Args:
;   ?root: Root path to join with. May be "".
;   ?list: List of additional path elements to join.
;
; Returns:
;   The joined path.
(deffunction proto-path-join (?root $?list)
  (if (eq ?root "") then (return (str-join "." ?list)))
  (return (str-join "." ?root ?list))
)

; Returns the value from a function result.
; This assumes the common pattern of returning a multifield, where the first
; entry is TRUE/FALSE signaling success or failure of the operation and the
; second part is the actual value returned on success.
; If more then one value is returned, the caller needs to handle this manually.
(deffunction result-value (?result)
  (if (not (result-ok ?result)) then
    (printout error
      (str-cat "Called (result-value (create$ " (str-join " " ?result)
        ")), but result is not OK."
        " Use result-ok before calling result-value.") crlf)
    (return nil)
  )
  (if (<> (length$ ?result) 2) then
    (printout error
      (str-cat "Called (result-value (create$ " (str-join " " ?result)
        ")) with " (length$ ?result) " values, expected 2."
        " Cannot return result value.") crlf)
    (return nil)
  )
  (return (nth$ 2 ?result))
)

; Returns the error message from a function result.
; This assumes the common pattern of returning a multifield, where the first
; entry is TRUE/FALSE signaling success or failure of the operation and the
; second part is the error message on failure.
(deffunction result-error (?result)
  (if (result-ok ?result) then
    (printout error
      (str-cat "Called (result-error (create$ " (str-join " " ?result)
        ")), but result is OK."
        " Use result-ok before calling result-error.") crlf)
    (return nil)
  )
  (if (<> (length$ ?result) 2) then
    (printout error
      (str-cat "Called (result-error (create$ " (str-join " " ?result)
        ")) with " (length$ ?result) " values, expected 2."
        " Cannot return result error.") crlf)
    (return nil)
  )
  (return (nth$ 2 ?result))
)

; Check if a list is unique, i.e., no element occurs twice.
; Return TRUE if list is unique, FALSE otherwise.
(deffunction uniquep ($?list)
  (bind ?copy (create$))
  (foreach ?l ?list
    (if (member$ ?l ?copy) then (return FALSE))
    (bind ?copy (append$ ?copy ?l))
  )
  (return TRUE)
)

; Check if given ?item is element of ?list.
; For example, a condition like (or (eq ?state PENDING) (eq ?state RUNNING)) can
; be rewritten as (isoneof ?state PENDING RUNNING).
; Returns TRUE if ?item is element of ?list, FALSE otherwise.
(deffunction isoneof (?item $?list)
  (return (neq (member$ ?item ?list) FALSE))
)

; Check if a list is non-unique, i.e., at least one element occurs twice.
; This is a shorthand for (not (unique$ ?list)).
; Return TRUE if list is non-unique, FALSE otherwise.
(deffunction non-uniquep ($?list)
  (return (not (uniquep ?list)))
)

; Remove all nil values from list
(deffunction remove-non-nil$ ($?list)
  (bind ?new-list (create$))
  (foreach ?l ?list
    (if (neq ?l nil) then (bind ?new-list (append$ ?new-list ?l)))
  )
  (return ?new-list)
)

; Get last element of a list.
(deffunction last$ ($?list)
  (return (nth$ (length$ ?list) ?list))
)

; Set equality, i.e. a in b and b in a.
(deffunction set-eq (?a ?b)
  (return (and (subsetp ?a ?b) (subsetp ?b ?a)))
)

; Get set difference ?a \ ?b, i.e. elements which exist in ?a but not in ?b
(deffunction set-diff (?a ?b)
  (bind ?rv (create$))
  (foreach ?e ?a (if (not (member$ ?e ?b)) then (bind ?rv (append$ ?rv ?e))))
  (return ?rv)
)

; Check if ?set contains at least one of the given ?values.
(deffunction set-contains-any (?set $?values)
  (foreach ?value ?values
    (if (member$ ?value ?set) then (return TRUE))
  )
  (return FALSE)
)

; Map functions
;
; A map is represented as a multifield with pairwise entries of ?key ?values
; For example:
; 'foo' 42 'bar' 39 'x' 99
;
; The following functions are convenience accessor to the map.
;
; Note that this is not an efficient data structure, but rather a set of linear
; runtime convenience wrappers.

; Returns TRUE iff the map has an even number of elements that can be
; interpreted as key value pairs.
(deffunction map-valid (?map)
  (if (= (mod (length$ ?map) 2) 0) then
    (return TRUE)
  )
  (return FALSE)
)

; Returns:
;   INVALID-MAP if ?map is not a valid map.
;   Otherwise the number of entries in the map.
(deffunction map-length (?map)
  (if (not (map-valid ?map)) then
    (return INVALID-MAP)
  )
  (return (div (length$ ?map) 2))
)

; Retrieves the mapped value for key.
;
; Args:
;   ?map: A multifield map.
;   ?key: The key to look up.
;
; Returns:
;   INVALID-MAP if ?map is not a valid map.
;   The value in the map or the symbol MISSING-FIELD if ?key is not in map.
(deffunction map-value (?map ?key)
  (if (not (map-valid ?map)) then
    (return INVALID-MAP)
  )
  (loop-for-count (?i (map-length ?map))
    (bind ?map-key-index (- (* ?i 2) 1))  ; 1 -> 1, 2 -> 3, 3 -> 5, etc.
    (if (eq ?key (nth$ ?map-key-index ?map)) then
      (return (nth$ (+ ?map-key-index 1) ?map))
    )
  )
  (return MISSING-FIELD)
)

; Replace substring ?search with ?replace in ?s
(deffunction str-replace (?s ?search ?replace)
  (bind ?i (str-index ?search ?s))
  (bind ?l (str-length ?search))
  (if (eq ?i FALSE)
   then (return ?s)
   else
    (return (str-cat (sub-string 1 (- ?i 1) ?s) ?replace
                     (sub-string (+ ?i ?l) (str-length ?s) ?s)))
  )
)

; Replace all occurrences of substring ?search with ?replace in ?s
(deffunction str-replace-all (?s ?search ?replace)
  (bind ?i (str-index ?search ?s))
  (bind ?l (str-length ?search))
  (while (neq ?i FALSE)
    (bind ?s (str-cat (sub-string 1 (- ?i 1) ?s) ?replace
                      (sub-string (+ ?i ?l) (str-length ?s) ?s)))
    (bind ?i (str-index ?search ?s))
  )
  (return ?s)
)

; Determines if a strings begins with a particular sub-string.
;
; Args:
;   ?string: string to search in (haystack)
;   ?substring: string to check if ?string starts with (needle)
;
; Returns:
;   TRUE if ?substring is contained at the beginning of ?string, FALSE otherwise
(deffunction str-startswith (?string ?substring)
  (bind ?idx (str-index ?substring ?string))
  (if (eq ?idx FALSE) then (return FALSE))
  (return (= ?idx 1))
)

; Return TRUE if the input string ends with the given substring,
; otherwise return FALSE.
(deffunction str-endswith (?string ?substring)
  (bind ?first-index-of-substring-in-string (str-index ?substring ?string))
  (if ?first-index-of-substring-in-string
     then (if (= (- (str-length ?string)
                   (str-length ?substring)
                   (str-index ?substring ?string))
                -1)
             then TRUE
             else (str-endswith (str-replace ?string ?substring "") ?substring)
          )
  )
)

; Converts a proto-style field or oneof name to a CLIPS-style symbol.
;
; The ?field-name is usually lowercase separated with underscores, while the
; CLIPS style prefers symbols to be uppercase separated with dashes.
; For example: my_option -> MY-OPTION.
;
; Args:
;   ?field-name: can be a symbol or string
;
; Returns:
;   Symbol for the field name in upper cases with dashes instead of underscores.
(deffunction proto-field-to-symbol (?field-name)
  (bind ?name-str (str-cat ?field-name))
  (bind ?name-str (str-replace-all ?name-str "_" "-"))
  (bind ?name-str (upcase ?name-str))
  (return (sym-cat ?name-str))
)

; Generate a new symbol with the given prefix and a unique generated symbol.
; For example, Calling (gensymx PREF) might yield the symbol PREF-gen4 (number
; can be anything). The function does *not* guarantee that the symbol does not
; exist, yet. However, if symbols with the given prefix are only ever created
; with gensymx uniqueness is given.
(deffunction gensymx (?prefix)
  (return (sym-cat ?prefix - (gensym*)))
)
