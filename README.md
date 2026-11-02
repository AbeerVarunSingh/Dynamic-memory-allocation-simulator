# Dynamic-memory-allocation-simulator
OS mini project: First Fit, Best Fit and Worst Fit memory allocation simulator in C

 *  Simulates variable (dynamic) partitioning with three placement policies:
 *      1. First Fit  - first hole that is big enough
 *      2. Best Fit   - smallest hole that is big enough
 *      3. Worst Fit  - largest hole
 *
 *  Features
 *      - Linked-list memory map (holes + allocated blocks)
 *      - Block splitting on allocation
 *      - Coalescing of adjacent free holes on deallocation
 *      - Compaction (removes external fragmentation)
 *      - Memory map table + ASCII bar visualisation
 *      - Statistics: utilisation, holes, largest hole, external fragmentation
 *      - COMPARE mode: replays the same request log on all 3 strategies
 *      - Built-in sample workload for a quick demo
