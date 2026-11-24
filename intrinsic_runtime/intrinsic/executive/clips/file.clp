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

; Handling of loading files from a given set of search dirs.

; Global variable to hold paths to search.
; Can be appended to using (file-load-add-search-dir)
(defglobal
  ?*FILE-SEARCH-DIRS* = (create$)
)

(deftemplate file-info
  "Information about files which have been attempted to be loaded.
   Can be used to verify success or communicate failure."
  (slot filename (type STRING))
  (slot path (type STRING))
  (slot loaded (type SYMBOL) (allowed-values FALSE TRUE))
  (slot error-msg (type STRING))
)

(deffunction file-load-add-search-dir (?dir)
  (bind ?*FILE-SEARCH-DIRS* (append$ ?*FILE-SEARCH-DIRS* ?dir))
)

(deffunction file-load-path-string ()
  (bind ?rv "")
  (foreach ?d ?*FILE-SEARCH-DIRS*
    (if (> ?d-index 1) then (bind ?rv (str-cat ?rv ":")))
    (bind ?rv (str-cat ?rv ?d))
  )
  (return ?rv)
)

(deffunction file-load-search (?file)
  (foreach ?d ?*FILE-SEARCH-DIRS*
    (bind ?fn (str-cat ?d ?file))
    (if (open ?fn file-clips-tmp) then
      (close file-clips-tmp)
      (return ?fn)
    )
  )
  (return FALSE)
)

(deffunction file-load (?file)
  (bind ?f (file-load-search ?file))
  (if ?f
   then
     (bind ?loaded (load* ?f))
     (bind ?emsg "")
     (if (not ?loaded) then
       (bind ?emsg (str-cat "Failed to open file " ?file " (from " ?f "), "
                            "see log for details"))
     )
     (assert (file-info (loaded ?loaded) (filename ?file) (path ?f)
                        (error-msg ?emsg)))
     (return ?loaded)
   else
     (bind ?emsg (str-cat "Cannot load file " ?file
                          " (file not found in " (file-load-path-string) ")"))
     (assert (file-info (loaded FALSE) (filename ?file) (error-msg ?emsg)))
     (return FALSE)
  )
)
