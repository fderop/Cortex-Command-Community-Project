from pathlib import Path
import os, shutil, subprocess, sys
runtime = Path(sys.argv[1]).resolve()
exe = runtime / 'CortexCommand.app/Contents/MacOS/CortexCommand'
shutil.copy2(sys.argv[2], exe)
env = os.environ.copy()
with open(sys.argv[3], 'w') as log:
    proc = subprocess.Popen([str(exe), '-cout'], cwd=runtime, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in proc.stdout:
        log.write(line)
        log.flush()
        if 'PERF_CAPTURE_DONE' in line:
            print(line.strip())
            proc.terminate()
            break
    proc.wait()
