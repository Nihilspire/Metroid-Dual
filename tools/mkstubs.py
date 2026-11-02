import sys, re, os
out, bionic, extra, api = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
def parse_map(path):
    syms = {}
    for line in open(path):
        line = line.strip()
        m = re.match(r'^([A-Za-z_][\w]*);\s*(#.*)?$', line)
        if not m: continue
        tags = (m.group(2) or '')
        if 'arm64' not in tags and re.search(r'\b(arm|x86|x86_64|riscv64)\b', tags) and 'arm64' not in tags: continue
        if 'platform-only' in tags or 'apex' in tags or 'llndk' in tags and 'introduced' not in tags: pass
        mi = re.search(r'introduced(?:-arm64)?=(\d+)', tags)
        syms[m.group(1)] = 'var' in tags.split()
    return syms
def emit(name, syms):
    with open(os.path.join(out, name + '.stub.c'), 'w') as f:
        for s, var in sorted(syms.items()):
            f.write(('int %s;\n' if var else 'void %s(void){}\n') % s)
emit('libc', parse_map(bionic + '/libc/libc.map.txt'))
emit('libm', parse_map(bionic + '/libm/libm.map.txt'))
emit('libdl', parse_map(bionic + '/libdl/libdl.map.txt'))
for fn in os.listdir(extra):
    syms = {}
    for l in open(os.path.join(extra, fn)):
        l = l.strip()
        if l and not l.startswith('#'): syms[l.split()[0]] = l.endswith(' var')
    emit(fn.replace('.txt', ''), syms)
