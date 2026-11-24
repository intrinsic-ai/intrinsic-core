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

; Times are tuple of (seconds, nanoseconds) since the Unix epoch.
; Cf. implementation of (now) in clips_init.cc.

; --------------------------------- FUNCTIONS ---------------------------------

; Time fact usually asserted at the beginning of an execution loop.
(deftemplate time
  ; The timestamp multislot holds [0]=seconds, [1]=nanoseconds
  (multislot timestamp
    (type INTEGER)
    (cardinality 2 2)
  )
)

; A general timer fact that enables to fire on regular time intervals
; To start the timer assert the fact, e.g.
; (assert (timer (name print-test)))
; Add a timeout rule, e.g.,
; (defrule timeout-print-test
;   ?now-fact <- (time (timestamp $?now-time))
;   ?t <- (timer (name print-test)
;           (start-time $?st&:(timeout ?now-time ?st 5)))
;  =>
;   (modify ?t (start-time ?now-time)))
; )
(deftemplate timer
  (slot name (type SYMBOL))
  (multislot start-time (type INTEGER) (cardinality 2 2)
    (default-dynamic (now)))
)

; Get difference of two times in seconds and nanoseconds
(deffunction time-diff (?t1 ?t2)
  (bind ?sec  (- (nth$ 1 ?t1) (nth$ 1 ?t2)))
  (bind ?nsec (- (nth$ 2 ?t1) (nth$ 2 ?t2)))
  (if (< ?nsec 0) then (bind ?sec (- ?sec 1)) (bind ?nsec (+ 1000000000 ?nsec)))
  (return (create$ ?sec ?nsec))
)

; Difference of two times in seconds as float
(deffunction time-diff-sec (?t1 ?t2)
  (bind ?td (time-diff ?t1 ?t2))
  (return (+ (float (nth$ 1 ?td)) (/ (float (nth$ 2 ?td)) 1000000000.)))
)

; Check if at least ?timeout seconds have passed since ?time given ?now
(deffunction timeout (?now ?time ?timeout)
  (return (> (time-diff-sec ?now ?time) ?timeout))
)

; Compare two times, i.e., is ?t1 > ?t2?
(deffunction time> (?t1 ?t2)
  (bind ?rv FALSE)
  (if (> (nth$ 1 ?t1) (nth$ 1 ?t2)) then (bind ?rv TRUE))
  (if (and (= (nth$ 1 ?t1) (nth$ 1 ?t2)) (> (nth$ 2 ?t1) (nth$ 2 ?t2)))
   then (bind ?rv TRUE))
  (return ?rv)
)

; Truncate nanoseconds to milliseconds
(deffunction time-trunc-ms (?time)
  (bind ?rv ?time)
  (if (= (length$ ?time) 2)
   then
    (bind ?rv (create$ (nth$ 1 ?time)
                       (* (div (nth$ 2 ?time) 1000000) 1000000)))
  )
  (return ?rv)
)

; Converts a time multifield to a ?timestamp proto.
;
; Args:
;   ?time: time multifield to convert
;
; Returns:
;   Proto message ID of google.protobuf.Timestamp proto.
(deffunction time-to-timestamp-proto ($?time)
  (bind ?timestamp-proto (pb-create "google.protobuf.Timestamp"))
  (pb-set-field ?timestamp-proto "seconds" (nth$ 1 ?time))
  (pb-set-field ?timestamp-proto "nanos" (nth$ 2 ?time))
  (return ?timestamp-proto)
)

; ----------------------------------- RULES -----------------------------------

; Retract the time with the lowest possible salience
(defrule time-retract
  ; Lowest possible priority for retracting time, i.e., the last in a loop
  ; is cleaning up the time fact.
  (declare (salience ?*SALIENCE-LAST*))
  ?f <- (time)
 =>
  (retract ?f)
)
