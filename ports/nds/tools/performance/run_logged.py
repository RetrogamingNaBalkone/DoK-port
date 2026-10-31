"""Stream native output to LOG.gz and return its real exit code."""
import gzip
from pathlib import Path
import subprocess
import sys
import shutil


def main():
    log = Path(sys.argv[1])
    if log.suffix != '.gz':
        if log.exists():
            raise FileExistsError(f'Use a new log label: {log}')
        log = log.with_name(log.name + '.gz')
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('xb') as output:
        with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as stream:
            with subprocess.Popen([sys.executable, '-B', '-X', 'utf8', *sys.argv[2:]],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as child:
                try:
                    shutil.copyfileobj(child.stdout, stream)
                except BaseException:
                    child.terminate()
                    child.wait()
                    raise
            code = child.returncode
    print(f'{log}: exit {code}')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
