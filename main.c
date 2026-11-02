/*
 * ==========================================================================
 *  DYNAMIC MEMORY ALLOCATION SIMULATOR
 *  Operating Systems Mini Project
 *
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
 *
 * ==========================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FREE_PID   -1
#define MAX_OPS    500
#define BAR_WIDTH  64

typedef enum { FIRST_FIT = 1, BEST_FIT = 2, WORST_FIT = 3 } Strategy;

static const char *strategy_name(Strategy s)
{
    switch (s) {
        case FIRST_FIT: return "First Fit";
        case BEST_FIT:  return "Best Fit";
        case WORST_FIT: return "Worst Fit";
    }
    return "Unknown";
}

/* ------------------------------ data types ------------------------------ */

typedef struct Block {
    int start;              /* starting address            */
    int size;               /* size of the block           */
    int pid;                /* FREE_PID if this is a hole  */
    struct Block *next;
} Block;

typedef struct {
    Block *head;
    int total;
    long search_steps;      /* blocks examined while searching (efficiency) */
} Memory;

typedef struct {
    char type;              /* 'A' = allocate, 'D' = deallocate */
    int  pid;
    int  size;
} Operation;

typedef struct {
    int allocs_ok, allocs_failed;
    int used, free_mem, holes, largest_hole;
    double ext_frag;        /* percentage */
    long steps;
} Stats;

/* ----------------------------- memory helpers --------------------------- */

static Block *new_block(int start, int size, int pid)
{
    Block *b = (Block *)malloc(sizeof(Block));
    if (!b) { fprintf(stderr, "Out of host memory!\n"); exit(1); }
    b->start = start;
    b->size  = size;
    b->pid   = pid;
    b->next  = NULL;
    return b;
}

static void mem_init(Memory *m, int total)
{
    m->total = total;
    m->head  = new_block(0, total, FREE_PID);
    m->search_steps = 0;
}

static void mem_destroy(Memory *m)
{
    Block *cur = m->head;
    while (cur) {
        Block *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    m->head = NULL;
}

static int pid_exists(const Memory *m, int pid)
{
    for (Block *b = m->head; b; b = b->next)
        if (b->pid == pid) return 1;
    return 0;
}

/* ------------------------- placement algorithms ------------------------- */

/* Returns the chosen hole according to the strategy, or NULL if none fits. */
static Block *find_hole(Memory *m, int size, Strategy s)
{
    Block *chosen = NULL;

    for (Block *b = m->head; b; b = b->next) {
        if (b->pid != FREE_PID) continue;
        m->search_steps++;
        if (b->size < size) continue;

        switch (s) {
            case FIRST_FIT:
                return b;                           /* stop at first match */
            case BEST_FIT:
                if (!chosen || b->size < chosen->size) chosen = b;
                break;
            case WORST_FIT:
                if (!chosen || b->size > chosen->size) chosen = b;
                break;
        }
    }
    return chosen;
}

/* Allocate 'size' units to process 'pid'. Returns 1 on success, 0 on failure. */
static int mem_allocate(Memory *m, int pid, int size, Strategy s)
{
    Block *hole = find_hole(m, size, s);
    if (!hole) return 0;

    if (hole->size > size) {                        /* split the hole */
        Block *rest = new_block(hole->start + size, hole->size - size, FREE_PID);
        rest->next = hole->next;
        hole->next = rest;
        hole->size = size;
    }
    hole->pid = pid;
    return 1;
}

/* Merge neighbouring free holes into one. */
static void mem_coalesce(Memory *m)
{
    Block *b = m->head;
    while (b && b->next) {
        if (b->pid == FREE_PID && b->next->pid == FREE_PID) {
            Block *dead = b->next;
            b->size += dead->size;
            b->next  = dead->next;
            free(dead);
        } else {
            b = b->next;
        }
    }
}

/* Free the block owned by 'pid'. Returns 1 on success, 0 if pid not found. */
static int mem_deallocate(Memory *m, int pid)
{
    for (Block *b = m->head; b; b = b->next) {
        if (b->pid == pid) {
            b->pid = FREE_PID;
            mem_coalesce(m);
            return 1;
        }
    }
    return 0;
}

/* Slide all allocated blocks to the low end, leaving one big hole. */
static void mem_compact(Memory *m)
{
    Block *newhead = NULL, *tail = NULL;
    int addr = 0;

    Block *b = m->head;
    while (b) {
        Block *next = b->next;
        if (b->pid != FREE_PID) {
            b->start = addr;
            addr += b->size;
            b->next = NULL;
            if (!newhead) newhead = b; else tail->next = b;
            tail = b;
        } else {
            free(b);
        }
        b = next;
    }
    if (addr < m->total) {
        Block *hole = new_block(addr, m->total - addr, FREE_PID);
        if (!newhead) newhead = hole; else tail->next = hole;
    }
    m->head = newhead;
}

/* ------------------------------ statistics ------------------------------ */

static Stats mem_stats(const Memory *m)
{
    Stats st;
    memset(&st, 0, sizeof st);

    for (Block *b = m->head; b; b = b->next) {
        if (b->pid == FREE_PID) {
            st.free_mem += b->size;
            st.holes++;
            if (b->size > st.largest_hole) st.largest_hole = b->size;
        } else {
            st.used += b->size;
        }
    }
    if (st.free_mem > 0)
        st.ext_frag = (1.0 - (double)st.largest_hole / st.free_mem) * 100.0;
    st.steps = m->search_steps;
    return st;
}

/* ------------------------------- display -------------------------------- */

static void print_line(char c, int n)
{
    while (n-- > 0) putchar(c);
    putchar('\n');
}

static void mem_display(const Memory *m)
{
    printf("\n  MEMORY MAP  (total = %d units)\n", m->total);
    print_line('-', 52);
    printf("  %-6s | %-8s | %-8s | %-8s | %-10s\n",
           "Block", "Start", "End", "Size", "Status");
    print_line('-', 52);

    int i = 1;
    for (Block *b = m->head; b; b = b->next, i++) {
        char status[24];
        if (b->pid == FREE_PID) strcpy(status, "FREE (hole)");
        else sprintf(status, "P%d", b->pid);
        printf("  %-6d | %-8d | %-8d | %-8d | %-10s\n",
               i, b->start, b->start + b->size - 1, b->size, status);
    }
    print_line('-', 52);

    /* ASCII bar: '#' = allocated (with pid digit), '.' = free */
    printf("\n  [");
    int printed = 0;
    for (Block *b = m->head; b; b = b->next) {
        int w = (int)((long)b->size * BAR_WIDTH / m->total);
        if (w < 1) w = 1;
        if (b->pid == FREE_PID) {
            for (int k = 0; k < w; k++) putchar('.');
        } else {
            char label[16];
            sprintf(label, "P%d", b->pid);
            int L = (int)strlen(label);
            if (w >= L + 2) {
                putchar('|');
                printf("%s", label);
                for (int k = 0; k < w - L - 1; k++) putchar('#');
            } else {
                for (int k = 0; k < w; k++) putchar('#');
            }
        }
        printed += w;
    }
    printf("]\n  ('#' = allocated, '.' = free)\n");
    (void)printed;
}

static void stats_display(const Memory *m, Strategy s)
{
    Stats st = mem_stats(m);
    printf("\n  STATISTICS  [%s]\n", strategy_name(s));
    print_line('-', 52);
    printf("  Total memory           : %d\n", m->total);
    printf("  Used memory            : %d (%.1f%%)\n", st.used,
           100.0 * st.used / m->total);
    printf("  Free memory            : %d (%.1f%%)\n", st.free_mem,
           100.0 * st.free_mem / m->total);
    printf("  Number of holes        : %d\n", st.holes);
    printf("  Largest hole           : %d\n", st.largest_hole);
    printf("  External fragmentation : %.2f%%\n", st.ext_frag);
    printf("  Search steps so far    : %ld\n", st.steps);
    print_line('-', 52);
}

/* ---------------------------- operation log ----------------------------- */

static Operation oplog[MAX_OPS];
static int nops = 0;

static void log_op(char type, int pid, int size)
{
    if (nops < MAX_OPS) {
        oplog[nops].type = type;
        oplog[nops].pid  = pid;
        oplog[nops].size = size;
        nops++;
    }
}

/* Replay the log on a fresh memory using strategy s. */
static Stats replay(int total, Strategy s, Memory *out)
{
    Memory m;
    int ok = 0, fail = 0;
    mem_init(&m, total);

    for (int i = 0; i < nops; i++) {
        if (oplog[i].type == 'A') {
            if (mem_allocate(&m, oplog[i].pid, oplog[i].size, s)) ok++;
            else fail++;
        } else {
            mem_deallocate(&m, oplog[i].pid);
        }
    }
    Stats st = mem_stats(&m);
    st.allocs_ok = ok;
    st.allocs_failed = fail;

    if (out) *out = m; else mem_destroy(&m);
    return st;
}

static void compare_all(int total)
{
    if (nops == 0) {
        printf("\n  No operations recorded yet. Allocate something first\n"
               "  (or load the sample workload from the menu).\n");
        return;
    }

    printf("\n  COMPARISON  (same %d operations replayed on each strategy)\n", nops);
    print_line('=', 78);
    printf("  %-10s | %-5s | %-6s | %-6s | %-8s | %-8s | %-9s | %-6s\n",
           "Strategy", "OK", "Failed", "Holes", "Free", "Largest", "Ext.Frag%", "Steps");
    print_line('-', 78);

    Stats best_stats[3];
    for (int s = 1; s <= 3; s++) {
        Memory m;
        Stats st = replay(total, (Strategy)s, &m);
        best_stats[s - 1] = st;
        printf("  %-10s | %-5d | %-6d | %-6d | %-8d | %-8d | %-9.2f | %-6ld\n",
               strategy_name((Strategy)s), st.allocs_ok, st.allocs_failed,
               st.holes, st.free_mem, st.largest_hole, st.ext_frag, st.steps);
        mem_destroy(&m);
    }
    print_line('=', 78);

    /* Simple verdict: most successful allocations, then least fragmentation */
    int winner = 0;
    for (int i = 1; i < 3; i++) {
        if (best_stats[i].allocs_ok > best_stats[winner].allocs_ok ||
            (best_stats[i].allocs_ok == best_stats[winner].allocs_ok &&
             best_stats[i].ext_frag < best_stats[winner].ext_frag))
            winner = i;
    }
    printf("  Verdict for this workload: %s performed best\n"
           "  (most successful allocations, then lowest fragmentation).\n",
           strategy_name((Strategy)(winner + 1)));
}

static void compare_maps(int total)
{
    for (int s = 1; s <= 3; s++) {
        Memory m;
        replay(total, (Strategy)s, &m);
        printf("\n  ===== Final memory map using %s =====", strategy_name((Strategy)s));
        mem_display(&m);
        mem_destroy(&m);
    }
}

/* ------------------------------ input helpers --------------------------- */

static int read_int(const char *prompt, int min, int max)
{
    char line[64];
    int v;
    for (;;) {
        printf("%s", prompt);
        if (!fgets(line, sizeof line, stdin)) exit(0);       /* EOF */
        if (sscanf(line, "%d", &v) == 1 && v >= min && v <= max) return v;
        printf("  Invalid input. Enter a number between %d and %d.\n", min, max);
    }
}

/* ------------------------------ sample data ----------------------------- */

static void load_sample(Memory *m, Strategy s, int *total)
{
    /* Classic textbook-style scenario on 1000 units of memory */
    mem_destroy(m);
    *total = 1000;
    mem_init(m, *total);
    nops = 0;

    struct { char t; int pid, size; } script[] = {
        {'A', 1, 200}, {'A', 2, 100}, {'A', 3, 250}, {'A', 4, 150},
        {'A', 5,  80}, {'D', 2, 0  }, {'D', 4, 0  }, {'A', 6,  90},
        {'A', 7, 140}, {'D', 1, 0  }, {'A', 8, 120}, {'A', 9, 160},
    };
    int n = (int)(sizeof script / sizeof script[0]);

    printf("\n  Loading sample workload (memory = %d units):\n", *total);
    for (int i = 0; i < n; i++) {
        if (script[i].t == 'A') {
            int ok = mem_allocate(m, script[i].pid, script[i].size, s);
            printf("    Allocate P%-2d (%3d) -> %s\n", script[i].pid,
                   script[i].size, ok ? "OK" : "FAILED");
            log_op('A', script[i].pid, script[i].size);
        } else {
            mem_deallocate(m, script[i].pid);
            printf("    Free     P%-2d       -> done\n", script[i].pid);
            log_op('D', script[i].pid, 0);
        }
    }
}

/* --------------------------------- main --------------------------------- */

static void banner(void)
{
    printf("\n");
    print_line('=', 60);
    printf("   DYNAMIC MEMORY ALLOCATION SIMULATOR\n");
    printf("   First Fit  |  Best Fit  |  Worst Fit\n");
    print_line('=', 60);
}

static void menu(Strategy s)
{
    printf("\n  Current strategy: %s\n", strategy_name(s));
    print_line('-', 44);
    printf("   1. Allocate memory to a process\n");
    printf("   2. Deallocate a process\n");
    printf("   3. Display memory map\n");
    printf("   4. Show statistics\n");
    printf("   5. Change allocation strategy\n");
    printf("   6. Compact memory\n");
    printf("   7. Compare all strategies (replay log)\n");
    printf("   8. Show final maps for all strategies\n");
    printf("   9. Load sample workload\n");
    printf("  10. Reset memory\n");
    printf("   0. Exit\n");
    print_line('-', 44);
}

int main(void)
{
    Memory mem;
    Strategy strat = FIRST_FIT;
    int total;

    banner();
    total = read_int("\n  Enter total memory size (units, 10-100000): ", 10, 100000);
    mem_init(&mem, total);

    for (;;) {
        menu(strat);
        int ch = read_int("  Enter choice: ", 0, 10);

        switch (ch) {
        case 1: {
            int pid  = read_int("  Process ID (1-9999): ", 1, 9999);
            if (pid_exists(&mem, pid)) {
                printf("  Process P%d already holds memory.\n", pid);
                break;
            }
            int size = read_int("  Memory required: ", 1, total);
            if (mem_allocate(&mem, pid, size, strat)) {
                printf("  [OK] P%d allocated %d units using %s.\n",
                       pid, size, strategy_name(strat));
            } else {
                Stats st = mem_stats(&mem);
                printf("  [FAILED] No single hole can hold %d units.\n", size);
                if (st.free_mem >= size)
                    printf("  Note: total free memory is %d, but it is fragmented\n"
                           "  (largest hole = %d). Try 'Compact memory'.\n",
                           st.free_mem, st.largest_hole);
            }
            log_op('A', pid, size);
            mem_display(&mem);
            break;
        }
        case 2: {
            int pid = read_int("  Process ID to free: ", 1, 9999);
            if (mem_deallocate(&mem, pid)) {
                printf("  [OK] P%d freed (adjacent holes merged).\n", pid);
                log_op('D', pid, 0);
                mem_display(&mem);
            } else {
                printf("  Process P%d not found in memory.\n", pid);
            }
            break;
        }
        case 3:
            mem_display(&mem);
            break;
        case 4:
            stats_display(&mem, strat);
            break;
        case 5:
            printf("\n   1. First Fit\n   2. Best Fit\n   3. Worst Fit\n");
            strat = (Strategy)read_int("  Select strategy: ", 1, 3);
            printf("  Strategy changed to %s.\n"
                   "  (Note: existing blocks stay where they are.)\n",
                   strategy_name(strat));
            break;
        case 6:
            mem_compact(&mem);
            printf("  Memory compacted. All free space is now one hole.\n");
            mem_display(&mem);
            break;
        case 7:
            compare_all(total);
            break;
        case 8:
            if (nops == 0) printf("\n  No operations recorded yet.\n");
            else compare_maps(total);
            break;
        case 9:
            load_sample(&mem, strat, &total);
            mem_display(&mem);
            printf("\n  Tip: choose option 7 to compare all three strategies.\n");
            break;
        case 10:
            mem_destroy(&mem);
            total = read_int("  Enter new total memory size: ", 10, 100000);
            mem_init(&mem, total);
            nops = 0;
            printf("  Memory reset.\n");
            break;
        case 0:
            mem_destroy(&mem);
            printf("\n  Goodbye!\n");
            return 0;
        }
    }
}
