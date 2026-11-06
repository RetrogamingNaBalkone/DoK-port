"""Fallback for Windows environments where Ninja's build subprocess launch stalls.

Uses the configured Ninja graph and shared toolchain; does not maintain a second
set of compiler/linker flags. Compiles the current C++ targets, then executes
their library/link commands in dependency order. No code generation is involved.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
from compiler_cache import compiler_cache


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--ninja', type=Path, required=True)
    parser.add_argument('targets', nargs='*', default=['all'])
    args = parser.parse_args()
    commands = subprocess.check_output(
        [str(args.ninja), '-t', 'commands', *args.targets],
        cwd=args.build_dir, text=True).splitlines()
    commands = list(dict.fromkeys(commands))
    compile_commands = [command for command in commands if ' -c ' in command]
    link_commands = [command for command in commands if ' -c ' not in command]

    def run(command):
        subprocess.run(command, cwd=args.build_dir, check=True, timeout=180)

    with ThreadPoolExecutor(max_workers=2) as pool:
        for index, _ in enumerate(pool.map(run, compile_commands), 1):
            print(f'Compile {index}/{len(compile_commands)}', flush=True)
    for index, command in enumerate(link_commands, 1):
        run(command)
        print(f'Link {index}/{len(link_commands)}', flush=True)


if __name__ == '__main__':
    with compiler_cache():
        main()
