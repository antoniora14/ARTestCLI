"""Test-only launcher that maps Windows Ctrl+Break to cooperative interruption."""

import runpy
import signal
import sys


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: test-stage4-cancel.py <project.py> <arguments...>")
    project_tool = sys.argv[1]
    sys.argv = [project_tool, *sys.argv[2:]]
    if sys.platform == "win32":
        signal.signal(signal.SIGBREAK, signal.default_int_handler)
    runpy.run_path(project_tool, run_name="__main__")


if __name__ == "__main__":
    main()
