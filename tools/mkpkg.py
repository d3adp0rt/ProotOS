#!/usr/bin/env python3
import io, os, sys, tarfile
src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]
with tarfile.open(dst, 'w') as t:
    data = open(src,'rb').read()
    info = tarfile.TarInfo(name)
    info.size = len(data); info.mode = 0o755
    t.addfile(info, io.BytesIO(data))
