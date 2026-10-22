# Safety setup (data-loss prevention)

The laptop has a single SSD with a single usable partition (C:, the system
drive). Following the assignment's data-loss warning, **no raw device or
partition is ever opened**. All tests run against **disposable test files
with direct I/O**, which the assignment explicitly allows.

Rules, enforced by the scripts (Batch 1 adds checks for each):
1. **Test files live only in `C:\fio-test\`.** That folder is created for this
   project, outside the repository, OneDrive and user documents. A script
   refuses any `filename` outside it.
2. **Never `\\.\PhysicalDrive*` or a drive letter** as an fio target (refused).
3. **Direct I/O always** (`direct=1`), except the one deliberate buffered-I/O
   pitfall run, which is read-only on the test file.
4. **Free-space floor.** A run refuses to start if creating or extending a
   test file would leave less than 100 GB free on C: (350 GB free at
   Step 1).
5. **Test-file size:** 16 GiB for the main sweeps, far beyond any cache
   (Windows page cache bypassed by direct I/O; no DRAM on the drive). The
   device-state test writes a separate 16 GiB file.
6. **Cleanup** of `C:\fio-test\` is done only on the student's
   instruction, and the files are disposable by definition.

Wear: planned total host writes are at most ~163 GB (~0.05% of a ~300 TBW
endurance rating). Write volume was deliberately reduced after Batch 1
(student request; NOTES "wear policy"): main file and SLC test 16 GiB,
write-side sweep runs 5 s, tail writes 30 s, steady state 120 s. Actual
totals are reported from fio's io_bytes.
