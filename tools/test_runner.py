#!/usr/bin/env python3
"""
Interactive, colorized test runner for MAVLinkLab.
Provides live step tracking, test case execution status, elapsed timers,
and clean visual summary tables with robust ANSI-aware border alignment.
"""

import sys
import os
import re
import time
import subprocess
import argparse
from typing import List, Dict, Optional, Tuple


def strip_ansi(text: str) -> str:
    """Strip ANSI escape sequences from string to measure true visible width."""
    return re.sub(r'\033\[[0-9;]*m', '', text)


def pad_str(text: str, width: int, align: str = '<') -> str:
    """Pad string to target visible width taking ANSI escapes into account."""
    vis_len = len(strip_ansi(text))
    pad = max(0, width - vis_len)
    if align == '>':
        return (' ' * pad) + text
    elif align == '^':
        left = pad // 2
        right = pad - left
        return (' ' * left) + text + (' ' * right)
    else:
        return text + (' ' * pad)


class Colors:
    """ANSI color codes with automatic TTY detection."""
    def __init__(self, enabled: bool = True):
        self.enabled = enabled and sys.stdout.isatty()
        self.RESET = "\033[0m" if self.enabled else ""
        self.BOLD = "\033[1m" if self.enabled else ""
        self.DIM = "\033[2m" if self.enabled else ""
        self.RED = "\033[1;31m" if self.enabled else ""
        self.GREEN = "\033[1;32m" if self.enabled else ""
        self.YELLOW = "\033[1;33m" if self.enabled else ""
        self.BLUE = "\033[1;34m" if self.enabled else ""
        self.MAGENTA = "\033[1;35m" if self.enabled else ""
        self.CYAN = "\033[1;36m" if self.enabled else ""
        self.WHITE = "\033[1;37m" if self.enabled else ""
        self.GRAY = "\033[0;90m" if self.enabled else ""


IGNORABLE_WARNINGS = [
    "This plugin does not support setting window masks",
    "This plugin does not support raise()",
    "This plugin does not support grabbing the keyboard",
    "This plugin does not support propagateSizeHints()",
    "edit: editing failed",
]


class TestCaseResult:
    def __init__(self, name: str, passed: bool, duration: float, error: str = ""):
        self.name = name
        self.passed = passed
        self.duration = duration
        self.error = error


class SuiteResult:
    def __init__(self, name: str, step_num: int, total_steps: int):
        self.name = name
        self.step_num = step_num
        self.total_steps = total_steps
        self.test_cases: List[TestCaseResult] = []
        self.duration = 0.0
        self.passed = True
        self.error_output = ""

    @property
    def total_count(self) -> int:
        return len(self.test_cases)

    @property
    def pass_count(self) -> int:
        return sum(1 for tc in self.test_cases if tc.passed)

    @property
    def fail_count(self) -> int:
        return sum(1 for tc in self.test_cases if not tc.passed)


class VisualTestRunner:
    def __init__(self, build_dir: str, colors: Colors):
        self.build_dir = os.path.abspath(build_dir)
        self.colors = colors
        self.suites: List[SuiteResult] = []

    def print_banner(self):
        c = self.colors
        width = 82
        print()
        print(f"{c.CYAN}{'═' * width}{c.RESET}")
        title = "MAVLinkLab Test Workbench Suite"
        subtitle = "Rugged Serial Telemetry Verification & Regression Testing"
        print(f"{c.BOLD}{c.WHITE}{title.center(width)}{c.RESET}")
        print(f"{c.GRAY}{subtitle.center(width)}{c.RESET}")
        print(f"{c.CYAN}{'═' * width}{c.RESET}")
        print(f"{c.DIM}Build Directory : {self.build_dir}{c.RESET}")
        print(f"{c.DIM}Platform        : Linux x86_64 (Qt 6 Offscreen QPA){c.RESET}")
        print(f"{c.CYAN}{'─' * width}{c.RESET}\n")

    def run_command_live(self, binary_path: str, args: List[str], suite: SuiteResult, is_qttest: bool = True):
        c = self.colors
        print(f"{c.BOLD}{c.BLUE}▶ Step {suite.step_num}/{suite.total_steps}:{c.RESET} {c.BOLD}{c.WHITE}{suite.name}{c.RESET}")

        start_time = time.time()
        cmd = [binary_path] + args
        env = os.environ.copy()
        env["QT_QPA_PLATFORM"] = "offscreen"

        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=env,
            cwd=self.build_dir
        )

        current_test = ""
        test_start_time = time.time()
        active_error_lines = []
        is_tty = sys.stdout.isatty()

        if is_qttest:
            for raw_line in iter(process.stdout.readline, ""):
                line = raw_line.strip()
                if not line:
                    continue

                # Filter out noisy QPA warnings
                if any(warn in line for warn in IGNORABLE_WARNINGS):
                    continue

                # Detect test case start
                match_enter = re.match(r"^INFO\s*:\s*\w+::(\w+)\(\)\s*entering", line)
                if match_enter:
                    test_name = match_enter.group(1)
                    if test_name not in ("initTestCase", "cleanupTestCase", "init", "cleanup"):
                        current_test = test_name
                        test_start_time = time.time()
                        active_error_lines = []
                        idx = len(suite.test_cases) + 1
                        if is_tty:
                            sys.stdout.write(f"\r{c.YELLOW}  ● [RUNNING]{c.RESET} {c.GRAY}[{idx:2d}]{c.RESET} {c.WHITE}{current_test}(){c.RESET}...")
                            sys.stdout.flush()
                        else:
                            print(f"  {c.YELLOW}● [RUNNING]{c.RESET} {c.GRAY}[{idx:2d}]{c.RESET} {c.WHITE}{current_test}(){c.RESET}...")
                    continue

                # Detect test case pass
                match_pass = re.match(r"^PASS\s*:\s*\w+::(\w+)\((.*?)\)", line) or re.match(r"^PASS\s*:\s*\w+::(\w+)\(\)", line)
                if match_pass:
                    test_name = match_pass.group(1)
                    tag = match_pass.group(2) if match_pass.lastindex >= 2 else ""
                    if test_name not in ("initTestCase", "cleanupTestCase", "init", "cleanup"):
                        duration = time.time() - test_start_time
                        display_name = f"{test_name}({tag})" if tag else f"{test_name}()"
                        suite.test_cases.append(TestCaseResult(display_name, True, duration))
                        idx = len(suite.test_cases)
                        if is_tty:
                            sys.stdout.write(f"\r\033[K")
                        print(f"  {c.GREEN}✔ [PASS]{c.RESET}    {c.GRAY}[{idx:2d}]{c.RESET} {c.WHITE}{display_name:<44}{c.RESET} {c.DIM}({duration:5.2f}s){c.RESET}")
                        current_test = ""
                    continue

                # Detect test case failure
                match_fail = re.match(r"^FAIL!\s*:\s*\w+::(\w+)\((.*?)\)", line) or re.match(r"^FAIL!\s*:\s*\w+::(\w+)\(\)", line)
                if match_fail:
                    test_name = match_fail.group(1)
                    tag = match_fail.group(2) if match_fail.lastindex >= 2 else ""
                    duration = time.time() - test_start_time
                    display_name = f"{test_name}({tag})" if tag else f"{test_name}()"
                    error_msg = line
                    suite.test_cases.append(TestCaseResult(display_name, False, duration, error_msg))
                    suite.passed = False
                    idx = len(suite.test_cases)
                    if is_tty:
                        sys.stdout.write(f"\r\033[K")
                    print(f"  {c.RED}✖ [FAIL]{c.RESET}    {c.GRAY}[{idx:2d}]{c.RESET} {c.BOLD}{c.RED}{display_name:<44}{c.RESET} {c.DIM}({duration:5.2f}s){c.RESET}")
                    active_error_lines.append(line)
                    continue

                # Collect failure context
                if active_error_lines or "Loc: [" in line:
                    active_error_lines.append(line)
                    print(f"    {c.RED}│{c.RESET} {c.GRAY}{line}{c.RESET}")

        else:
            # Non-Qt unit test runner with sub-test reporting (core-tests, profile-tests)
            sub_start_time = time.time()
            for raw_line in iter(process.stdout.readline, ""):
                line = raw_line.strip()
                if not line or any(warn in line for warn in IGNORABLE_WARNINGS):
                    continue

                # Match sub-test PASS e.g. "PASS: [01/12] SubTest Name"
                match_sub = re.match(r"^PASS:\s*(\[\d+/\d+\].+)$", line)
                if match_sub:
                    sub_name = match_sub.group(1).strip()
                    sub_duration = time.time() - sub_start_time
                    sub_start_time = time.time()
                    suite.test_cases.append(TestCaseResult(sub_name, True, sub_duration))
                    idx = len(suite.test_cases)
                    print(f"  {c.GREEN}✔ [PASS]{c.RESET}    {c.GRAY}[{idx:2d}]{c.RESET} {c.WHITE}{sub_name:<44}{c.RESET} {c.DIM}({sub_duration:5.2f}s){c.RESET}")
                elif "ALL CORE TESTS PASSED" in line or "ALL TESTS PASSED" in line or "PASS:" in line:
                    print(f"  {c.GREEN}✔ [PASS]{c.RESET}    {line}")
                elif line.startswith("FAIL:") or "FAILED" in line:
                    error_msg = line
                    suite.test_cases.append(TestCaseResult(line, False, 0.0, error_msg))
                    suite.passed = False
                    print(f"  {c.RED}✖ [FAIL]{c.RESET}    {c.BOLD}{c.RED}{line}{c.RESET}")
                else:
                    print(f"  {c.DIM}·{c.RESET} {line}")

        process.wait()
        suite.duration = time.time() - start_time
        if process.returncode != 0:
            suite.passed = False

        # Fallback for simple tests without sub-tests
        if not is_qttest and len(suite.test_cases) == 0:
            suite.test_cases.append(TestCaseResult(suite.name, suite.passed, suite.duration))

        status_str = f"{c.GREEN}PASS{c.RESET}" if suite.passed else f"{c.RED}FAILED (exit {process.returncode}){c.RESET}"
        print(f"  {c.DIM}└─ Suite {suite.step_num} Result:{c.RESET} {status_str} {c.DIM}in {suite.duration:.2f}s ({suite.pass_count}/{suite.total_count} passed){c.RESET}\n")

    def print_summary(self):
        c = self.colors
        w1, w2, w3 = 44, 8, 20
        c1, c2, c3 = w1 + 2, w2 + 2, w3 + 2
        total_inner = c1 + 1 + c2 + 1 + c3  # 46 + 1 + 10 + 1 + 22 = 80 chars inside frame

        total_time = sum(s.duration for s in self.suites)
        total_tests = sum(s.total_count for s in self.suites)
        passed_tests = sum(s.pass_count for s in self.suites)
        failed_tests = sum(s.fail_count for s in self.suites)
        all_passed = (failed_tests == 0) and all(s.passed for s in self.suites)

        # Top border
        print(f"{c.CYAN}┌{'─' * c1}┬{'─' * c2}┬{'─' * c3}┐{c.RESET}")
        title = f"{c.BOLD}{c.WHITE}TEST WORKBENCH EXECUTION SUMMARY{c.RESET}"
        print(f"{c.CYAN}│{c.RESET} {pad_str(title, total_inner - 2, '^')} {c.CYAN}│{c.RESET}")
        print(f"{c.CYAN}├{'─' * c1}┼{'─' * c2}┼{'─' * c3}┤{c.RESET}")

        # Column headers
        h_name = f"{c.BOLD}Test Suite Name{c.RESET}"
        h_status = f"{c.BOLD}Status{c.RESET}"
        h_metrics = f"{c.BOLD}Time / Passed{c.RESET}"
        print(f"{c.CYAN}│{c.RESET} {pad_str(h_name, w1, '<')} {c.CYAN}│{c.RESET} {pad_str(h_status, w2, '^')} {c.CYAN}│{c.RESET} {pad_str(h_metrics, w3, '<')} {c.CYAN}│{c.RESET}")
        print(f"{c.CYAN}├{'─' * c1}┼{'─' * c2}┼{'─' * c3}┤{c.RESET}")

        # Rows
        for s in self.suites:
            status_text = f"{c.GREEN}✔ PASS{c.RESET}" if s.passed else f"{c.RED}✖ FAIL{c.RESET}"
            name_display = s.name[:w1]
            metrics = f"{s.duration:6.2f}s ({s.pass_count:2d}/{s.total_count:2d})"
            print(f"{c.CYAN}│{c.RESET} {pad_str(name_display, w1, '<')} {c.CYAN}│{c.RESET} {pad_str(status_text, w2, '^')} {c.CYAN}│{c.RESET} {pad_str(metrics, w3, '<')} {c.CYAN}│{c.RESET}")

        # Separator before footer
        print(f"{c.CYAN}├{'─' * c1}┴{'─' * c2}┴{'─' * c3}┤{c.RESET}")

        # Summary footer line
        if all_passed:
            badge = f"{c.GREEN}✔ ALL {total_tests} TESTS PASSED (100% SUCCESS){c.RESET}"
            tot_str = f"{c.BOLD}{c.WHITE}Total Time: {total_time:6.2f}s{c.RESET}"
        else:
            badge = f"{c.RED}✖ {failed_tests}/{total_tests} TESTS FAILED{c.RESET}"
            tot_str = f"{c.BOLD}{c.RED}Total Time: {total_time:6.2f}s{c.RESET}"

        w_badge = 54
        w_tot = total_inner - 2 - w_badge
        print(f"{c.CYAN}│{c.RESET} {pad_str(badge, w_badge, '<')}{pad_str(tot_str, w_tot, '>')} {c.CYAN}│{c.RESET}")

        # Bottom border
        print(f"{c.CYAN}└{'─' * total_inner}┘{c.RESET}")

        # Inspection artifacts guidance
        print(f"\n{c.DIM}Visual Regression & QA Artifacts:{c.RESET}")
        print(f"  {c.GRAY}• Offscreen visual renders saved in : {self.build_dir}/qa/{c.RESET}")
        print(f"  • QA Inspector reference captures   : {os.path.join(os.path.dirname(self.build_dir), 'qa')}/\n")

        return 0 if all_passed else 1


def main():
    parser = argparse.ArgumentParser(description="MAVLinkLab Visual Test Runner")
    parser.add_argument("--build-dir", default="build", help="Path to build directory")
    parser.add_argument("--suite", choices=["all", "core", "desktop", "profile", "control-lines"], default="all")
    parser.add_argument("--filter", default="", help="Filter specific desktop test function")
    parser.add_argument("--no-color", action="store_true", help="Disable colored output")
    args = parser.parse_args()

    project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build_dir = os.path.abspath(args.build_dir if os.path.isabs(args.build_dir) else os.path.join(project_dir, args.build_dir))

    colors = Colors(enabled=not args.no_color)
    runner = VisualTestRunner(build_dir, colors)
    runner.print_banner()

    suites_to_run = []
    if args.suite in ("all", "core"):
        suites_to_run.append(("Core Unit Tests (mavcore)", "core-tests", [], False))
    if args.suite in ("all", "desktop"):
        desktop_args = ["-v1"]
        if args.filter:
            desktop_args.append(args.filter)
        suites_to_run.append(("Desktop GUI & PTY Workbench (37 tests)", "desktop-tests", desktop_args, True))
    if args.suite in ("all", "profile"):
        suites_to_run.append(("Profile Core Tests", "profile-tests", [], False))
        suites_to_run.append(("Profile Worker Thread Tests (7 tests)", "profile-worker-tests", ["-v1"], True))
        suites_to_run.append(("Profile UI Actions & Dialogs", "profile-ui-tests", ["-v1"], True))
    if args.suite in ("all", "control-lines"):
        suites_to_run.append(("Modem Control Lines Tests", "control-lines-tests", ["-v1"], True))

    total_steps = len(suites_to_run)
    for idx, (name, binary_name, extra_args, is_qttest) in enumerate(suites_to_run, 1):
        binary_path = os.path.join(build_dir, binary_name)
        if not os.path.exists(binary_path):
            print(f"{colors.RED}Error: Test binary not found: {binary_path}{colors.RESET}")
            sys.exit(1)

        suite_res = SuiteResult(name, idx, total_steps)
        runner.suites.append(suite_res)
        runner.run_command_live(binary_path, extra_args, suite_res, is_qttest=is_qttest)

    exit_code = runner.print_summary()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
