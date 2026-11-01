# Templates/

Before/after C++ snippets used as the "after" pattern for each optimization pass.
A pass loads a template, fills its {{ARR}} and {{TARGET}} tokens with the names it
detected in the user code, and emits the result as the suggested rewrite. Keeping
the text here means suggestion wording changes without recompiling the engine.

One template per pass:
- pair_sum_hash.cpp.tmpl    -> PairSumToHash
- linear_to_binary.cpp.tmpl -> LinearToBinarySearch
