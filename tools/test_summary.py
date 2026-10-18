"""Summarise an Unreal automation log of the SSX.* tests: total, passed (ran fully), skipped, failed.

Usage:  python tools\test_summary.py <log file>
A test that logged an [SSX-SKIP] line (tests/TestSkip.h: reference data, recordings or SSX_ORIGINAL_REPLAY missing)
counts as skipped, not passed. Exit code 1 if any test failed or no test ran, else 0.
"""
import re
import sys

COMPLETED = re.compile(r"Test Completed\. Result=\{(?P<result>[^}]*)\} Name=\{[^}]*\} Path=\{(?P<path>[^}]*)\}")
BEGIN = re.compile(r"LogAutomationController: BeginEvents: (?P<path>\S+)")
END = re.compile(r"LogAutomationController: EndEvents: (?P<path>\S+)")
MARK = "[SSX-SKIP] "


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    results = {}      # path -> result
    skips = {}        # path -> [reason]
    errors = {}       # path -> first error line
    current = None
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = COMPLETED.search(line)
            if m:
                results[m.group("path")] = m.group("result")
                continue
            m = BEGIN.search(line)
            if m:
                current = m.group("path")
                continue
            if END.search(line):
                current = None
                continue
            if current is None:
                continue
            body = line.split("LogAutomationController: ", 1)[-1]
            if MARK in body:
                skips.setdefault(current, []).append(body.split(MARK, 1)[1].strip())
            elif body.startswith("Error: ") and current not in errors:
                errors[current] = body[len("Error: "):].strip()

    failed = [p for p, r in results.items() if r != "Success"]
    skipped = [p for p in results if p in skips and p not in failed]
    passed = len(results) - len(failed) - len(skipped)

    print("SSX test summary")
    print("  total   %d" % len(results))
    print("  passed  %d (ran fully)" % passed)
    print("  skipped %d (missing reference data / recordings / replay)" % len(skipped))
    print("  failed  %d" % len(failed))
    if skipped:
        print("\nSkipped:")
        for p in sorted(skipped):
            print("  %s: %s" % (p, skips[p][0]) + ("  (+%d more)" % (len(skips[p]) - 1) if len(skips[p]) > 1 else ""))
    if failed:
        print("\nFailed:")
        for p in sorted(failed):
            print("  %s: %s" % (p, errors.get(p, results[p])))
    return 1 if failed or not results else 0


if __name__ == "__main__":
    sys.exit(main())
