import base64, json, sys
from playwright.sync_api import sync_playwright
rom = open('/home/claude/test/metroid.nes','rb').read()
d = open(sys.argv[1],'rb').read()
ram = d[:0x8000]; ppu = d[0x8000:0xC000]
seed = sys.argv[2] if len(sys.argv) > 2 else ''
init = """
window.__ram = %s; window.__ppu = %s; window.__rom = '%s';
window.Native = {
  dual: function(){return true;}, version: function(){return '0.1';}, running: function(){return true;},
  mem: function(spec){ var o=''; spec.split(',').forEach(function(p){ var a=parseInt(p.split(':')[0],16), n=+(p.split(':')[1]||1);
     for(var i=0;i<n;i++){ o+=('0'+__ram[a+i].toString(16)).slice(-2);} }); return o; },
  pal: function(){ var o=''; for(var i=0;i<32;i++) o+=('0'+__ppu[0x3F00+i].toString(16)).slice(-2); return o; },
  chr: function(){ return btoa(String.fromCharCode.apply(null, __ppu.slice(0,0x1000))); },
  rom: function(off,len){ var b=atob(__rom).substr(off,len); return btoa(b); },
  overlay: function(){return false;}, menu: function(){}, close: function(){}
};
%s
""" % (json.dumps(list(ram)), json.dumps(list(ppu)), base64.b64encode(rom).decode(), seed)
with sync_playwright() as p:
    b = p.chromium.launch()
    for name, w, h in (('portrait', 675, 900), ('landscape', 900, 675), ('wide', 1280, 720)):
        pg = b.new_page(viewport={'width': w, 'height': h})
        pg.add_init_script(init)
        msgs=[]; pg.on('console', lambda m: msgs.append(m.text)); pg.on('pageerror', lambda e: msgs.append('ERR '+str(e)))
        pg.goto('file:///home/claude/metroid-dual/app/assets/link/index.html')
        pg.wait_for_timeout(1500)
        pg.screenshot(path=f'/home/claude/test/link_{name}.png')
        if name == 'portrait':
            pg.mouse.click(w*0.5, h-8); pg.wait_for_timeout(300); pg.screenshot(path='/home/claude/test/link_pw.png')
            pg.mouse.click(w*0.83, h-8); pg.wait_for_timeout(300); pg.screenshot(path='/home/claude/test/link_opt.png')
        print(name, msgs[:5])
    b.close()
