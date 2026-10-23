# AI-use note

## Tools

- **Claude Code** (Anthropic, model Claude Opus 5.5) in the Claude desktop
  app. It ran with access to this repository and a shell: it could edit
  files, compile, run the benchmarks and scripts, and commit.
- Software it used, all locally installed: gcc 16.2.0 and binutils (MSYS2
  UCRT64), Python 3.14 with numpy/pandas/matplotlib, git. `pdfminer` was
  installed only to read the assignment PDF.

## What the AI generated

The work was done in explicit batches. Each one was planned in chat,
reviewed by the student, then implemented, self-tested, committed and pushed
only after its checks passed.

| Batch | AI-generated |
|---|---|
| Step 2 | `reports/HYPOTHESES.md` text and README (content dictated by the student's commit script) |
| 1 | `src/kernels.{c,h}`, `src/bench.c`, `scripts/build.sh`, `scripts/test.sh` |
| 2 | `scripts/verify_vec.py`, `scripts/run_sweeps.sh`, `src/peak.c`, `scripts/analyze.py` |
| 3 | the long-form analysis draft (folded into `reports/REPORT.md` for submission) |
| 4 | plot polish, `reports/REPORT.md`, this file, README |

The student set the project direction and the batch workflow. The student
approved each batch before it started, reviewed the generated files, asked
for status and corrections, and supplied the rubric that the final report
was checked against.

## What was tested, corrected or rejected

These problems were caught by testing and inspection during the project,
not taken on trust from generated code. Each is recorded in the git history;
the full working notes are preserved at the git tag
`pre-submission-refactor`.

1. **The tests were tested.** The correctness suite (1121 checks) was
   validated by building deliberately broken kernels: an off-by-one tail in
   saxpy and dot, and an out-of-bounds write in mul. All were caught. One
   blind spot was identified and documented: a single missing element in a
   1M-element dot product falls inside the summation tolerance, so tail bugs
   are tested at small N.
2. **Scalar baseline trap.** gcc vectorizes `#pragma omp simd` loops even
   under `-fno-tree-vectorize`. `-fopenmp-simd` was removed from the scalar
   build, and an assembly check was added to `test.sh`.
3. **Hypothesis H8's mechanism was wrong.** The prediction was that the plain
   dot product "will not vectorize". The compiler report and assembly showed
   an in-order vectorization (packed multiply, serial adds). The committed
   hypothesis was left unchanged and the deviation reported.
4. **CSV line endings.** Windows text-mode stdout wrote CRLF into the raw
   data. Fixed with binary stdout before the full sweeps ran.
5. **Bandwidth microbenchmark was wrong.** The first L1 read test measured
   207 GB/s, below the 401 GB/s that saxpy itself achieved. A
   "more-passes" fix was tried and *rejected* (no change). The real cause was
   one 512-bit add per load, which saturates the single 512-bit ALU port. The
   rewrite uses one FMA per two loads and measures 429 GB/s.
6. **Roofline plot bugs.** The DRAM points were invisible (hollow markers
   with a surface-colored edge), and two peak labels overlapped. Found by
   viewing every rendered figure, then fixed.
7. **Four factual errors in the first analysis draft,** found by
   cross-checking each cited number against `data/processed/`:
   - the claim that "AVX-512 is never slower than AVX2" (false for the plain
     dot product);
   - the epilogue width (32-byte for avx512, not 16-byte);
   - two numeric ranges.
8. **Wrong explanation of strided code.** The first version said gcc
   "emulates strided access with scalar loads plus inserts". Reading the
   loop that actually runs for stride > 1 showed that gcc versions the loop
   on `stride == 1` and runs plain scalar code otherwise. Corrected in the
   analysis, the notes and the vectorization report.
9. **Git scope.** An early `git add -A` would have staged files outside the
   project folder. Later commits stage only `SIMD-Advantage-Profiling/`.

## What remains the student's responsibility

All measurements were run on the student's laptop. The interpretation in
REPORT.md was written by the AI from the data and checked
against it. The student is responsible for understanding and defending every
claim.
