"""Compile the actual production filesystem module; also name helpers on Linux."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--compiler', default='g++')
parser.add_argument('--output', default=str(root / 'lab_results' / 'unit-test.log'))
args = parser.parse_args()
output = Path(args.output).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
binary = output.parent / ('drop_table_files_test.exe' if sys.platform == 'win32' else 'drop_table_files_test')
command = [args.compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic',
           '-I' + str(root / 'src'), '-I' + str(root / 'src/observer'),
           str(root / 'lab_tests/drop_table_files_test.cpp')]
if sys.platform != 'win32':
    command.append(str(root / 'src/observer/storage/common/meta_util.cpp'))
command.extend(['-o', str(binary)])
records = ['COMPILER: ' + (shutil.which(args.compiler) or args.compiler), 'COMMAND: ' + subprocess.list2cmdline(command)]
build = subprocess.run(command, capture_output=True, text=True)
records.extend(['BUILD_EXIT: ' + str(build.returncode), build.stdout, build.stderr])
if build.returncode == 0:
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    records.extend(['TEST_EXIT: ' + str(run.returncode), run.stdout, run.stderr])
    code = run.returncode
else:
    code = build.returncode
output.write_text('\n'.join(records), encoding='utf-8')
print('\n'.join(records))
sys.exit(code)
