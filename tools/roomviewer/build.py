# Inlines mroom.js into the viewer page -> metroid-rooms.html (one self-contained file).
import os
d = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(d, 'viewer.src.html')).read()
lib = open(os.path.join(d, '..', '..', 'app', 'assets', 'link', 'mroom.js')).read()
open(os.path.join(d, 'metroid-rooms.html'), 'w').write(src.replace('/*MROOM*/', lib))
print('ok', len(src) + len(lib))
