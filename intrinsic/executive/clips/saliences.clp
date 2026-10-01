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

; Global salience registry
;
; Saliences are priorities of rules during conflict resolution if on the agenda
; at the same time (cf. Sec 5.3 Conflict Resolution Strategies in the Basic
; Programming Manual, we are using the default depth strategy).

; Salience should be used rarely and only if necessary, e.g., when resolving a
; conflict within the same rule area. Whenever possible, use actual rule
; conditions to disambiguate activation order. Only as a last resort use
; saliences.

; Avoid two-way inter-area synchronization, i.e., have a strict partial order
; among rule areas (a rule area roughly is one file).

; If saliences are necessary, use generic ones. Avoid adding saliences as much
; as possible and do so only as a last resort. If you think you need to add a
; new one, create a suitable CL and ask timdn@ for review.

; ---------------------------------- GLOBALS -----------------------------------
(defglobal
 ?*SALIENCE-FIRST*                          =  10000
 ?*SALIENCE-HIGHER*                         =   5000
 ?*SALIENCE-HIGH*                           =   2000

 ; Salience to ensure that we can capture the START state of a condition
 ; before it is started to start a tracing span. Must be higher than
 ; SALIENCE-BEHAVIOR-TREE-CONDITION.
 ?*SALIENCE-TRACING-CONDITIONS*             =   1600

 ; Skill updates must be completed before executing a behavior tree.
 ?*SALIENCE-SKILL-UPDATES*                  =    1000

 ; Selective node execution must trigger before breakpoints
 ?*SALIENCE-SELECTIVE-EXECUTION-UPDATE*     =    970
 ?*SALIENCE-SELECTIVE-EXECUTION*            =    960

 ; Breakpoint rules must trigger before BT execution rules
 ?*SALIENCE-BREAKPOINT-UPDATE*              =    950
 ?*SALIENCE-BREAKPOINT*                     =    900

 ; Behavior Tree saliences to ensure conditions have been fully evaluated
 ; before making a choice. Normal conditions must be evaluated before
 ; compounds.
 ?*SALIENCE-BEHAVIOR-TREE-CONDITION*        =    850
 ?*SALIENCE-BEHAVIOR-TREE-COND-COMPOUND*    =    820

 ; Logging must be performed before execution to ensure a correct context
 ?*SALIENCE-LOGGING*                        =    500

 ; Rules that trigger suspension or cancellation, in particular the ones that
 ; affect a sub tree must fire in time, so that execution in a sub tree does
 ; not continue although the parent is suspended
 ?*SALIENCE-CANCELING*                     =    410
 ?*SALIENCE-SUSPENDING*                     =    400

 ; Projection must be performed before execution
 ?*SALIENCE-ACTION-PROJECTION*              =    300

 ; Execution monitoring must be able to preempt outcome determination
 ; to be able to initiate a recovery if possible
 ?*SALIENCE-EXECMON-PREEMPT*                =    200

 ; this is the value of no salience is given, only as information
 ; ?*SALIENCE-NORMAL*                       =      0
 ?*SALIENCE-LOW*                            =  -2000
 ?*SALIENCE-LOWER*                          =  -5000
 ?*SALIENCE-COLLECT-EVENTS*                 =  -8000
 ?*SALIENCE-LAST*                           = -10000
)
