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

; Generate Graphviz "dot" graphs for Behavior Trees.

; --------------------------------- FUNCTIONS ---------------------------------

; Recursively generate nodes and edges for a sub-tree starting at a given root.
; ?node-id is the ID of the root of the sub-graph. The code generates a dot node
; in the graph output for it, adds an edge from the node's parent to the node,
; (the parent's name is passed in the ?parent-node-name argument),
; and recursively invokes the function for all children. ?str will contain
; the string generated thus far. The function appends to it and returns the
; modified string (similar to what a (bind ?str (str-cat ?str ...)) would do,
; where "..." would be the part generated for the given sub-tree.
(deffunction behavior-tree-node-to-dotgraph (?node-id ?tree-id ?parent-node-name
                                             ?edge-attributes ?str)
  (do-for-fact ((?node behavior-tree-node)) (and (eq ?node:id ?node-id)
                                                 (eq ?node:tree-id ?tree-id))
    (bind ?node-name (str-cat ?node:tree-id "-" ?node:id))
    (bind ?node-label ?node:type)
    (bind ?node-shape "box")
    (bind ?node-color "gray")
    (switch ?node:type
      (case SEQUENCE then (bind ?node-shape "cds"))
      (case PARALLEL then (bind ?node-shape "trapezium"))
      (case SELECTOR then (bind ?node-shape "octagon"))
      (case BRANCH then (bind ?node-shape "diamond"))
      (case RETRY then
        (bind ?node-shape "ellipse")
        (bind ?node-label (str-cat "RETRY " ?node:max-tries))
      )
      (case LOOP then
        (bind ?node-shape "hexagon")
        (bind ?node-label (str-cat "LOOP " ?node:loop-max-times))
      )
      (case TASK then
        (do-for-fact ((?action plan-action))
          (eq ?action:uid ?node:task-action-prototype-uid)
          (bind ?node-label (plan-action-tostring ?action:plan-id ?action:id))
        )
      )
    )
    (switch ?node:state
      (case SELECTED then (bind ?node-color "black"))
      (case RUNNING then (bind ?node-color "orange"))
      (case SUCCEEDED then (bind ?node-color "limegreen"))
      (case FAILED then (bind ?node-color "red"))
    )
    (if (neq ?node:condition-id nil) then
      (if ?node:condition-satisfied then
        (bind ?node-label (str-cat ?node-label " " "[+]"))
       else
        (bind ?node-label (str-cat ?node-label " " "[-]"))
      )
    )
    ; node
    (bind ?str (str-cat ?str
      (format nil "  \"%s\" [label=\"%s\",shape=%s,color=%s];%n"
              ?node-name ?node-label ?node-shape ?node-color)))
    ; node's edge
    (if (neq ?parent-node-name "") then
      (bind ?edge-label "")
      (if (neq ?edge-attributes "") then
        (bind ?edge-label (format nil " [%s]" ?edge-attributes)))
      (bind ?str (str-cat ?str
        (format nil "  \"%s\" -> \"%s\"%s;%n"
                ?parent-node-name ?node-name ?edge-label)))
    )

    (switch ?node:type
      (case BRANCH then
        (do-for-fact
            ((?then behavior-tree-node)) (eq ?then:id ?node:branch-then-id)
          (bind ?str (behavior-tree-node-to-dotgraph ?then:id ?tree-id
                                                     ?node-name
                                                     "label=\"then\"" ?str))
        )
        (do-for-fact
            ((?else behavior-tree-node)) (eq ?else:id ?node:branch-else-id)
          (bind ?str (behavior-tree-node-to-dotgraph ?else:id ?tree-id
                                                     ?node-name
                                                     "label=\"else\"" ?str))
        )
      )
      (default
        ; node's children
        (bind ?children (behavior-tree-sorted-children ?node-id ?tree-id))
        (foreach ?child ?children
          (bind ?child-id (fact-slot-value ?child id))
          (bind ?str (behavior-tree-node-to-dotgraph ?child-id ?tree-id
                                                     ?node-name "" ?str))
        )
      )
    )
  )
  (return ?str)
)

; Generate a Graphviz "dot" graph for a given Behavior Tree.
; Outputs a digraph representing the tree structure suitable to pass for
; rendering to, e.g., "dot -Tx11 <file.dot>" with <file.dot> containing the
; output of this graph.
(deffunction behavior-tree-to-dotgraph (?tree-id)
  (bind ?root-graph "")
  (bind ?tree-name "UNKNOWN")
  (do-for-fact ((?tree behavior-tree)) (eq ?tree:id ?tree-id)
    (bind ?tree-name ?tree:id)
    (bind ?root-graph-str (behavior-tree-node-to-dotgraph ?tree:root ?tree-id
                                                          "" "" ""))
  )
  (return (format nil "strict digraph \"%s\" {%n%s}%n"
                  ?tree-name ?root-graph-str))
)
