# Usage: python3 tools/mock_ra.py <memory dump: CPU $0000-$7FFF then PPU $0000-$3FFF> [memory|ramonly|nomap] [port]
# Mock RetroArch network command server (UDP 55355) serving a Metroid memory dump. Mimics command.c reply formats.
import socket, sys
dump = open(sys.argv[1], 'rb').read(); cpu = dump[:0x8000]; ppu = dump[0x8000:]
mode = sys.argv[2] if len(sys.argv) > 2 else 'memory'      # 'memory' | 'ramonly' (no READ_CORE_MEMORY) | 'nomap'
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', int(sys.argv[3]) if len(sys.argv) > 3 else 55355))
log = open('/tmp/ratest/mock.log', 'w')
def readmem(a, n):
    if a >= 0x80000000:
        a &= 0xFFFF
        if 0x3000 <= a < 0x3020: return bytes(ppu[0x3F00 + (a - 0x3000) + i] for i in range(min(n, 0x3020 - a)))
        return None
    if a >= 0x8000: return None
    end = min(a + n, (a // 1024 + 1) * 1024)            # clamp to the 1 KB descriptor
    return cpu[a:end]
while True:
    data, addr = s.recvfrom(4096); cmd = data.decode().strip(); log.write(cmd + '\n'); log.flush()
    t = cmd.split()
    if t[0] == 'GET_STATUS': rep = 'GET_STATUS PLAYING nes,Metroid (USA)\n'
    elif t[0] in ('READ_CORE_MEMORY', 'READ_CORE_RAM'):
        if t[0] == 'READ_CORE_MEMORY' and mode == 'ramonly': continue          # unknown command: no reply
        a = int(t[1], 16); n = int(t[2]); d = readmem(a, n)
        if mode == 'nomap' and t[0] == 'READ_CORE_MEMORY': rep = 'READ_CORE_MEMORY %x -1 no memory map defined\n' % a
        elif d is None: rep = '%s %x -1 no descriptor for address\n' % (t[0], a)
        else: rep = t[0] + ' %x' % a + ''.join(' %02X' % b for b in d) + '\n'
    elif t[0] in ('SAVE_STATE_SLOT', 'LOAD_STATE_SLOT'): rep = '%s %d' % (t[0], int(t[1]))
    else: continue
    s.sendto(rep.encode(), addr)
