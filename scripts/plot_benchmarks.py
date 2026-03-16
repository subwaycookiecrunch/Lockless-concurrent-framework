#!/usr/bin/env python3
"""
Runs the stack and ring buffer benchmarks, parses the output,
and generates a throughput-vs-threads scaling chart.

Usage:
    python scripts/plot_benchmarks.py [--build-dir build]

Requires matplotlib (pip install matplotlib).
Saves output to docs/scaling.png
"""

import subprocess
import re
import sys
import os
import argparse


def run_benchmark(exe_path):
    """Run a benchmark executable and return its stdout."""
    result = subprocess.run(
        [exe_path], capture_output=True, text=True, timeout=120
    )
    if result.returncode != 0:
        print(f"Warning: {exe_path} returned {result.returncode}")
        print(result.stderr)
    return result.stdout


def parse_scaling_table(output, header_pattern):
    """
    Parse a scaling table from benchmark output.
    Returns list of (threads, ops_per_sec) tuples.
    """
    lines = output.split('\n')
    data = []
    in_table = False

    for line in lines:
        if header_pattern in line:
            in_table = True
            continue
        if in_table and '---' in line:
            continue
        if in_table:
            parts = line.strip().split('|')
            if len(parts) >= 2:
                try:
                    threads = int(parts[0].strip())
                    ops = float(parts[1].strip())
                    data.append((threads, ops))
                except ValueError:
                    if data:  # we've finished the table
                        break
            elif data:
                break

    return data


def parse_stack_scaling(output):
    """Parse the stack scaling table (has lock-free and mutex columns)."""
    lines = output.split('\n')
    lf_data = []
    mx_data = []
    in_table = False

    for line in lines:
        if 'Stack Contention Scaling' in line:
            in_table = True
            continue
        if in_table and '---' in line:
            continue
        if in_table:
            parts = line.strip().split('|')
            if len(parts) >= 3:
                try:
                    threads = int(parts[0].strip())
                    lf_ops = float(parts[1].strip())
                    mx_ops = float(parts[2].strip())
                    lf_data.append((threads, lf_ops))
                    mx_data.append((threads, mx_ops))
                except ValueError:
                    if lf_data:
                        break
            elif lf_data:
                break

    return lf_data, mx_data


def plot_results(rb_data, stack_lf_data, stack_mx_data, output_path):
    """Generate the scaling chart."""
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed. Install with: pip install matplotlib")
        print("Skipping chart generation.")
        return False

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
    fig.patch.set_facecolor('#1a1a2e')

    for ax in (ax1, ax2):
        ax.set_facecolor('#16213e')
        ax.tick_params(colors='#e0e0e0')
        ax.xaxis.label.set_color('#e0e0e0')
        ax.yaxis.label.set_color('#e0e0e0')
        ax.title.set_color('#e0e0e0')
        for spine in ax.spines.values():
            spine.set_color('#333366')

    # ring buffer chart
    if rb_data:
        threads = [d[0] for d in rb_data]
        ops = [d[1] / 1e6 for d in rb_data]  # convert to M ops/s
        ax1.plot(threads, ops, 'o-', color='#00d4ff', linewidth=2,
                 markersize=8, label='MPMC RingBuffer')
        ax1.set_xlabel('Threads')
        ax1.set_ylabel('Throughput (M ops/sec)')
        ax1.set_title('Ring Buffer Scaling')
        ax1.legend(facecolor='#1a1a2e', edgecolor='#333366',
                   labelcolor='#e0e0e0')
        ax1.set_xticks(threads)

    # stack chart (lock-free vs mutex)
    if stack_lf_data and stack_mx_data:
        threads = [d[0] for d in stack_lf_data]
        lf_ops = [d[1] / 1e6 for d in stack_lf_data]
        mx_ops = [d[1] / 1e6 for d in stack_mx_data]

        ax2.plot(threads, lf_ops, 'o-', color='#00d4ff', linewidth=2,
                 markersize=8, label='Lock-Free Stack')
        ax2.plot(threads, mx_ops, 's--', color='#ff6b6b', linewidth=2,
                 markersize=8, label='Mutex Stack')
        ax2.set_xlabel('Threads')
        ax2.set_ylabel('Throughput (M ops/sec)')
        ax2.set_title('Stack: Lock-Free vs Mutex')
        ax2.legend(facecolor='#1a1a2e', edgecolor='#333366',
                   labelcolor='#e0e0e0')
        ax2.set_xticks(threads)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, facecolor=fig.get_facecolor(),
                bbox_inches='tight')
    print(f"Chart saved to {output_path}")
    return True


def main():
    parser = argparse.ArgumentParser(description='Run benchmarks and plot results')
    parser.add_argument('--build-dir', default='build',
                        help='CMake build directory (default: build)')
    args = parser.parse_args()

    build_dir = args.build_dir
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    # figure out executable extension
    ext = '.exe' if sys.platform == 'win32' else ''

    rb_bench = os.path.join(project_root, build_dir, f'benchmark_ring_buffer{ext}')
    stack_bench = os.path.join(project_root, build_dir, f'benchmark_stack{ext}')

    # also check Release subdirectory (MSVC multi-config)
    if not os.path.exists(rb_bench):
        rb_bench = os.path.join(project_root, build_dir, 'Release',
                                f'benchmark_ring_buffer{ext}')
        stack_bench = os.path.join(project_root, build_dir, 'Release',
                                   f'benchmark_stack{ext}')

    if not os.path.exists(rb_bench):
        print(f"Benchmark not found at {rb_bench}")
        print("Build the project first: cmake -S . -B build && cmake --build build --config Release")
        sys.exit(1)

    print("Running ring buffer benchmark...")
    rb_output = run_benchmark(rb_bench)
    print(rb_output)

    print("Running stack benchmark...")
    stack_output = run_benchmark(stack_bench)
    print(stack_output)

    # parse results
    rb_data = parse_scaling_table(rb_output, "Contention Scaling")
    stack_lf_data, stack_mx_data = parse_stack_scaling(stack_output)

    if not rb_data and not stack_lf_data:
        print("Couldn't parse any benchmark data. Check output format.")
        sys.exit(1)

    # generate chart
    output_path = os.path.join(project_root, 'docs', 'scaling.png')
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    plot_results(rb_data, stack_lf_data, stack_mx_data, output_path)


if __name__ == '__main__':
    main()
