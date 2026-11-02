# Wraps a raw recording from the desktop driver (drive rom script, using recopen/rec/recclose) into a .mdrec.
import sys, zlib
raw = open(sys.argv[1], 'rb').read()
open(sys.argv[2], 'wb').write(b'MDREC1' + zlib.compress(raw, 9))
print(len(raw), '->', sys.argv[2])
