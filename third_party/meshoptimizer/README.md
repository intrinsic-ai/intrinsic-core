# Vendored Meshoptimizer Simplifier

This package vendors `simplifier.cpp` and `simplifier.h` from [meshoptimizer](https://github.com/zeux/meshoptimizer) (version 1.3), patched with `meshopt_simplifyCustom` to support a per-collapse validation callback.

This can be removed once this upstream issue is resolved and released to the BCR:

* https://github.com/zeux/meshoptimizer/issues/1105
* http://b/573600598
