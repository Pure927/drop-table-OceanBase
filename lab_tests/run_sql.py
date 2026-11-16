"""Run real SQL against a fresh MiniOB observer over its documented plain protocol.

Requires an already compiled Linux observer. Never connects to an existing DB.
The unique run folder is retained for inspecting table files and server logs.
"""
import argparse
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--observer', required=True)
parser.add_argument('--config', required=True)
parser.add_argument('--output', default='lab_results/sql-runs')
args = parser.parse_args()
observer, config = Path(args.observer).resolve(), Path(args.config).resolve()
if not observer.is_file() or not config.is_file():
    parser.error('observer executable and config must both exist')
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
run = Path(tempfile.mkdtemp(prefix='drop-table-', dir=out))
socket_path = run / 'observer.sock'
if len(str(socket_path).encode()) > 100:
    parser.error('output path is too long for a Unix socket; use --output /tmp/miniob-lab')
records = []
process = None
client = None
server_log = open(run / 'server-console.log', 'ab')

def start():
    global process, client
    if socket_path.exists():
        socket_path.unlink()
    process = subprocess.Popen([str(observer), '-f', str(config), '-s', str(socket_path),
                                '-P', 'plain', '-E', 'heap', '-t', 'vacuous', '-d'],
                               cwd=run, stdout=server_log, stderr=subprocess.STDOUT)
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError('observer exited during startup; see server-console.log')
        try:
            client = socket.socket(socket.AF_UNIX)
            client.settimeout(10)
            client.connect(str(socket_path))
            return
        except (OSError, FileNotFoundError):
            client.close()
            time.sleep(0.1)
    raise RuntimeError('observer startup timed out')

def stop():
    global client, process
    if client:
        client.close()
        client = None
    if process and process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError('observer did not shut down gracefully')

def sql(statement, expect=None):
    client.sendall(statement.encode() + b'\0')
    data = b''
    while not data.endswith(b'\0'):
        part = client.recv(8192)
        if not part:
            raise RuntimeError('observer disconnected')
        data += part
    result = data[:-1].decode('utf-8').strip()
    records.append('SQL> ' + statement + '\n' + result)
    if expect is not None and result != expect:
        raise AssertionError(f'{statement}: expected {expect!r}, got {result!r}')
    return result

def expect_rows(statement, expected):
    result = sql(statement)
    rows = [line.strip() for line in result.splitlines()]
    if rows != expected:
        raise AssertionError(f'{statement}: expected {expected!r}, got {rows!r}')

def check_files_removed(name):
    db = run / 'miniob/db/sys'
    residue = [p.name for p in db.iterdir()
               if p.name in {name + suffix for suffix in ['.table', '.data', '.lob', '.table.tmp']}
               or p.name.startswith(name + '-') or p.name.startswith(name + '.table.drop')
               or p.name.startswith(name + '.data.drop') or p.name.startswith(name + '.lob.drop')]
    if residue:
        raise AssertionError('table files remain: ' + repr(residue))
    records.append('FILES> no resources remain for ' + name)

try:
    start()
    sql('drop table missing;', 'FAILURE')
    sql('create table t (id int, score float);', 'SUCCESS')
    sql('create table t2 (id int);', 'SUCCESS')
    sql('insert into t2 values (99);', 'SUCCESS')
    sql('insert into t values (1, 1.5);', 'SUCCESS')
    sql('create index idx_id on t(id);', 'SUCCESS')
    sql('create index idx_score on t(score);', 'SUCCESS')
    expect_rows('select id from t where id=1;', ['id', '1'])
    sql('drop table t;', 'SUCCESS')
    check_files_removed('t')
    expect_rows('show tables;', ['Tables_in_SYS', 't2'])
    sql('select * from t;', 'FAILURE')
    sql('desc t;', 'FAILURE')
    sql('drop table t;', 'FAILURE')
    expect_rows('select * from t2;', ['id', '99'])
    sql('create table t (id int);', 'SUCCESS')
    sql('insert into t values (7);', 'SUCCESS')
    sql('create index idx_id on t(id);', 'SUCCESS')
    expect_rows('select * from t;', ['id', '7'])
    sql('begin;', 'SUCCESS')
    sql('drop table t;', 'FAILURE')
    sql('rollback;', 'SUCCESS')
    expect_rows('select * from t;', ['id', '7'])
    sql('drop table t;', 'SUCCESS')
    check_files_removed('t')
    stop()
    records.append('RESTART> same data directory')
    start()
    sql('select * from t;', 'FAILURE')
    expect_rows('show tables;', ['Tables_in_SYS', 't2'])
    expect_rows('select * from t2;', ['id', '99'])
    sql('create table t (id int);', 'SUCCESS')
    sql('drop table t;', 'SUCCESS')
    sql('drop table t2;', 'SUCCESS')
    check_files_removed('t')
    check_files_removed('t2')
    records.append('RESULT: SQL integration and graceful restart checks passed')
except Exception as exc:
    records.append('RESULT: FAILED: ' + repr(exc))
    raise
finally:
    try:
        stop()
    finally:
        server_log.close()
        (run / 'sql-test.log').write_text('\n\n'.join(records), encoding='utf-8')
        print('\n\n'.join(records))
        print('Evidence folder:', run)
