# FW-1 TC Firmware CI/CD Guide

## Overview

This project uses **GitHub Actions** for continuous integration and testing. All unit tests run automatically on every push and pull request, with results reported back to GitHub.

## Current Implementation (Phase 1)

### What Runs Automatically

**On every push to `main` or `feat/fw1-tc-firmware`:**
1. Compile all 4 phases (76 tests) with multiple compilers
2. Run all unit tests
3. Generate test summary
4. Upload artifacts
5. Comment on PRs with test results

**Supported Compilers:**
- GCC (default)
- Clang (alternative)

### GitHub Actions Workflow

File: `.github/workflows/test.yml`

```yaml
on:
  push:
    branches: [ main, feat/fw1-tc-firmware ]
  pull_request:
    branches: [ main, feat/fw1-tc-firmware ]
```

**What it does:**
```
✓ Phase 1: Compile 9 HAL/GPIO tests → Run → Report results
✓ Phase 2: Compile 19 Hardware tests → Run → Report results
✓ Phase 3: Compile 21 Recipe Manager tests → Run → Report results
✓ Phase 4: Compile 27 DUT Interface tests → Run → Report results
✓ Phase 5: Verify mock implementations compile (no hardware needed)
```

## Viewing Test Results

### Option 1: GitHub Actions Tab

1. Go to your repository: https://github.com/INTenX/G3-MB-Embedded-Tester-Client
2. Click **Actions** tab
3. Select the latest workflow run
4. View detailed logs for each phase

**What you'll see:**
- Compiler version
- Compile output
- Test execution output
- Summary statistics

### Option 2: Pull Request Comments

When you create a PR, GitHub Actions automatically comments with test results:

```
## FW-1 TC Firmware Test Results

========================================
  FW-1 TC FIRMWARE TEST RESULTS
========================================

Compiler: gcc
Date: 2026-01-17 13:45:32

Phase 1: HAL/GPIO (9 tests)
9 Tests 0 Failures 0 Ignored OK

Phase 2: Hardware Module (19 tests)
19 Tests 0 Failures 0 Ignored OK

Phase 3: Recipe Manager (21 tests)
21 Tests 0 Failures 0 Ignored OK

Phase 4: DUT Interface (27 tests)
27 Tests 0 Failures 0 Ignored OK

Phase 5: ESP32 HAL Mocks (Compilation verified)
✓ All mock implementations compile

========================================
TOTAL: 76 unit tests (mock, off-board)
========================================
```

### Option 3: Download Artifacts

Each workflow run produces artifacts:

1. Go to Actions → Recent run
2. Scroll to bottom → "Artifacts"
3. Download `test-results-gcc.txt` or `test-results-clang.txt`

### Option 4: Local HTML Report

Generate a beautiful HTML report locally:

```bash
cd embedded/tester-client
chmod +x scripts/generate_test_report.sh
./scripts/generate_test_report.sh
```

**Output:** `test_reports/index.html`

Open in browser to see:
- Overall pass/fail statistics
- Charts showing test distribution by phase
- Detailed results for each phase
- Responsive design for mobile/desktop

## Local Testing (Before Pushing)

Always test locally before pushing to avoid CI failures:

### Quick Test (All 76 tests)

```bash
cd embedded/tester-client

# Compile all tests
gcc -I. -DDEBUG test/test_hal_gpio.c hal/mock/hal_gpio_mock.c -o t1 && ./t1
gcc -I. -DDEBUG test/test_hardware.c hal/mock/*.c bsp/bsp_g3_tc.c -o t2 && ./t2
gcc -I. -DDEBUG test/test_recipe_manager.c storage/storage_mock.c recipes/recipe_manager_mock.c -o t3 && ./t3
gcc -I. -DDEBUG test/test_dut_interface.c hal/mock/hal_uart_mock.c dut/dut_interface_mock.c -o t4 && ./t4
```

### Test with HTML Report

```bash
./scripts/generate_test_report.sh
open test_reports/index.html
```

### Test with Specific Compiler

```bash
# Use clang instead of gcc
./scripts/generate_test_report.sh clang
```

## CI/CD Best Practices

### 1. Branch Protection Rules

**Recommended GitHub settings** (Settings → Branches → Require status checks to pass):
- ✓ Require status checks to pass before merging
- ✓ Require branches to be up to date before merging
- ✓ Require pull requests to be reviewed before merging
- ✓ Require code review from code owners

### 2. Commit Message Standards

Write clear, descriptive commits:

```
✓ Good:
  feat: add ADC calibration for Phase 2
  fix: correct UART timeout handling in DUT interface
  test: add edge case coverage for recipe manager

✗ Bad:
  fixed stuff
  wip
  asdf
```

### 3. PR Checklist

Before creating a PR:
- [ ] All tests pass locally
- [ ] No compiler warnings (use `-Wall -Wextra`)
- [ ] Code follows project style
- [ ] Comments explain "why", not "what"
- [ ] Documentation updated if needed

### 4. Reviewing PRs

When reviewing:
1. Check GitHub Actions passed
2. Review code changes
3. Verify test coverage
4. Check for security issues

## Planned Enhancements (Phase 2+)

### Coming Soon

**Code Coverage Tracking:**
```bash
# Generate coverage reports
gcov *.c
# Upload to codecov.io
```

**Automated Performance Benchmarks:**
```bash
# Track test execution time over time
time ./test_dut_interface
```

**Multi-Compiler Testing:**
```yaml
matrix:
  compiler: [gcc, clang, arm-none-eabi-gcc]
  architecture: [x86_64, armv7, armv8]
```

**Hardware Integration Tests:**
```bash
# Run tests on real ESP32-DevKit (separate pipeline)
pio test -e esp32-Devkit
```

**Test History Dashboard:**
- Track test results over time
- Identify flaky tests
- Performance trend analysis
- Coverage trends

## Troubleshooting

### CI Failure: "compiler not found"

**Cause:** Workflow running on Ubuntu that doesn't have specific compiler
**Fix:** Add to workflow:
```yaml
- name: Install dependencies
  run: |
    sudo apt-get update
    sudo apt-get install -y gcc clang arm-none-eabi-gcc
```

### CI Failure: "test timeout"

**Cause:** Test hanging or taking too long
**Fix:** Add timeout to workflow:
```yaml
- name: Run tests with timeout
  timeout-minutes: 5
  run: ./test_dut_interface
```

### CI Failure: "file not found"

**Cause:** Working directory not set correctly
**Fix:** Ensure `cd embedded/tester-client` in each step

### Local Tests Pass but CI Fails

**Cause:** Environment differences
**Solution:**
1. Compare compiler versions: `gcc --version`
2. Check if test compiles with `-Wall -Wextra -pedantic`
3. Run with memory sanitizer: `ASAN_OPTIONS=detect_leaks=1 ./test`

## Running Specific Tests

### Run Only Phase 4 (DUT Interface)

```bash
cd embedded/tester-client
gcc -I. -DDEBUG test/test_dut_interface.c \
    hal/mock/hal_uart_mock.c \
    dut/dut_interface_mock.c \
    -o test_dut && ./test_dut
```

### Run With Address Sanitizer

```bash
gcc -I. -DDEBUG -fsanitize=address,undefined \
    test/test_hardware.c \
    hal/mock/*.c bsp/bsp_g3_tc.c \
    -o test_hw && ./test_hw
```

### Run With Verbose Debug Output

```bash
# All mocks print detailed logs with DEBUG flag
gcc -I. -DDEBUG test/test_dut_interface.c \
    hal/mock/hal_uart_mock.c \
    dut/dut_interface_mock.c \
    -o test_dut && ./test_dut 2>&1 | grep "\[UART\]"
```

## GitHub Actions Dashboard

### Workflow Status

View at: `https://github.com/INTenX/G3-MB-Embedded-Tester-Client/actions`

Status badge:
```markdown
[![Tests](https://github.com/INTenX/G3-MB-Embedded-Tester-Client/actions/workflows/test.yml/badge.svg)](https://github.com/INTenX/G3-MB-Embedded-Tester-Client/actions)
```

### Recent Runs

Shows:
- Branch name
- Commit message
- Test status (✓ or ✗)
- Execution time
- Artifacts

## Integration with Main Project

The submodule workflow feeds into main project:
- Submodule: G3-MB-Embedded-Tester-Client (this repo)
- Main project: RTGF-Test-Fixture-Projects

**Data flow:**
```
feat/fw1-tc-firmware push
    ↓
.github/workflows/test.yml runs
    ↓
76 tests pass
    ↓
Comment added to PR
    ↓
Artifacts uploaded
    ↓
PR ready for review
    ↓
Merge to main when approved
```

## Next Steps

1. **Verify workflow works:**
   - Push a small change to feat/fw1-tc-firmware
   - Check Actions tab for results

2. **Add to main project CI:**
   - Create similar workflow in main project
   - Run tests for multiple platform targets (ESP32, Arduino)

3. **Set branch protection:**
   - GitHub Settings → Branches
   - Require status checks to pass before merge

4. **Configure notifications:**
   - Watch PR comments
   - Email on workflow failure (optional)

## Resources

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Ubuntu Latest Runners](https://docs.github.com/en/actions/using-github-hosted-runners/about-github-hosted-runners)
- [Workflow Syntax](https://docs.github.com/en/actions/using-workflows/workflow-syntax-for-github-actions)
- [Building and testing in CI](https://docs.github.com/en/actions/guides/building-and-testing)
