#!/usr/bin/env python3
"""
Run flake tests for all VMOS platform configurations.

Calls 'make flake' for each combination of:
- 6 platforms (arm64, arm32, x64, x32, rv64, rv32)
- 2 VirtIO transports (MMIO, PCI)
= 12 configurations total

Each 'make flake' runs 10 tests and categorizes as:
- works: all 10 runs pass
- flaky: at least 1 run passes but not all
- broken: no runs pass

Parallelism is limited to 4 simultaneous tests.
"""

import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import List

PLATFORMS = ["arm64", "arm32", "x64", "x32", "rv64", "rv32"]
USE_PCI_OPTIONS = [0, 1]  # 0 = MMIO, 1 = PCI
MAX_PARALLEL = 4


@dataclass
class ConfigStats:
    """Statistics for a configuration from 'make flake'."""
    platform: str
    use_pci: int
    total_runs: int
    passed_runs: int
    failed_runs: int
    success: bool  # Whether make flake itself succeeded (all 10 passed)

    @property
    def transport(self) -> str:
        return "PCI" if self.use_pci == 1 else "MMIO"

    @property
    def config_name(self) -> str:
        return f"{self.platform}/{self.transport}"

    @property
    def status(self) -> str:
        """Categorize as works/flaky/broken."""
        if self.passed_runs == self.total_runs:
            return "works"
        elif self.passed_runs > 0:
            return "flaky"
        else:
            return "broken"


def run_flake_test(platform: str, use_pci: int) -> ConfigStats:
    """Run 'make flake' for a single configuration."""
    transport = "PCI" if use_pci == 1 else "MMIO"
    print(f"\n>>> Testing {platform}/{transport}...", flush=True)

    try:
        result = subprocess.run(
            ["make", "flake", f"PLATFORM={platform}", f"USE_PCI={use_pci}"],
            capture_output=True,
            text=True,
            timeout=300,  # 5 minute timeout for 10 tests
        )

        # Parse the output to extract results
        # Look for lines like "[1/10] ✓ PASSED" or "[1/10] ✗ FAILED"
        passed = 0
        failed = 0

        for line in result.stdout.split('\n'):
            if '✓ PASSED' in line:
                passed += 1
            elif '✗ FAILED' in line:
                failed += 1

        # Also try to parse the summary line
        # "=== Results: 8/10 passed ==="
        summary_match = re.search(r'Results: (\d+)/(\d+) passed', result.stdout)
        if summary_match:
            passed = int(summary_match.group(1))
            total = int(summary_match.group(2))
            failed = total - passed
        else:
            # Fallback: assume 10 total if we didn't find the summary
            total = 10
            if passed + failed == 0:
                # Couldn't parse anything, assume all failed
                failed = 10
                passed = 0

        success = result.returncode == 0
        status_symbol = "✓" if success else "✗"
        print(f"<<< {platform}/{transport}: {status_symbol} {passed}/10 passed", flush=True)

        return ConfigStats(platform, use_pci, 10, passed, failed, success)

    except subprocess.TimeoutExpired:
        print(f"<<< {platform}/{transport}: ✗ TIMEOUT", flush=True)
        return ConfigStats(platform, use_pci, 10, 0, 10, False)
    except Exception as e:
        print(f"<<< {platform}/{transport}: ✗ ERROR: {e}", flush=True)
        return ConfigStats(platform, use_pci, 10, 0, 10, False)


def main():
    """Run flake tests for all configurations with controlled parallelism."""
    print("=" * 70)
    print("FLAKE TEST - ALL CONFIGURATIONS")
    print("=" * 70)
    print(f"Configurations: {len(PLATFORMS)} platforms × {len(USE_PCI_OPTIONS)} transports = 12 total")
    print(f"Runs per config: 10")
    print(f"Total tests: {len(PLATFORMS) * len(USE_PCI_OPTIONS) * 10}")
    print(f"Parallelism: {MAX_PARALLEL} simultaneous configs")
    print("=" * 70)

    # Create all configuration tasks
    tasks = [
        (platform, use_pci)
        for platform in PLATFORMS
        for use_pci in USE_PCI_OPTIONS
    ]

    all_stats: List[ConfigStats] = []

    # Run configurations in parallel with controlled parallelism
    with ThreadPoolExecutor(max_workers=MAX_PARALLEL) as executor:
        future_to_config = {
            executor.submit(run_flake_test, platform, use_pci): (platform, use_pci)
            for platform, use_pci in tasks
        }

        for future in as_completed(future_to_config):
            config = future_to_config[future]
            try:
                stats = future.result()
                all_stats.append(stats)
            except Exception as e:
                platform, use_pci = config
                transport = "PCI" if use_pci == 1 else "MMIO"
                print(f"EXCEPTION: {platform}/{transport} - {e}")

    # Categorize results
    works = [s for s in all_stats if s.status == "works"]
    flaky = [s for s in all_stats if s.status == "flaky"]
    broken = [s for s in all_stats if s.status == "broken"]

    # Print summary
    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)

    print(f"\n✓ WORKS ({len(works)} configurations - all 10 runs pass):")
    if works:
        for s in sorted(works, key=lambda x: (x.platform, x.use_pci)):
            print(f"  - {s.config_name}")
    else:
        print("  (none)")

    print(f"\n⚠ FLAKY ({len(flaky)} configurations - some runs pass):")
    if flaky:
        for s in sorted(flaky, key=lambda x: (x.platform, x.use_pci)):
            print(f"  - {s.config_name}: {s.passed_runs}/{s.total_runs} passed")
    else:
        print("  (none)")

    print(f"\n✗ BROKEN ({len(broken)} configurations - no runs pass):")
    if broken:
        for s in sorted(broken, key=lambda x: (x.platform, x.use_pci)):
            print(f"  - {s.config_name}")
    else:
        print("  (none)")

    print("\n" + "=" * 70)
    print(f"Total: {len(all_stats)} configurations tested")
    print(f"  Works: {len(works)}")
    print(f"  Flaky: {len(flaky)}")
    print(f"  Broken: {len(broken)}")
    print("=" * 70)

    # Exit with non-zero if any configurations are broken or flaky
    if broken or flaky:
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
