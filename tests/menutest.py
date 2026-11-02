import base64, json
from playwright.sync_api import sync_playwright
rom = open('/home/claude/test/metroid.nes','rb').read()
font = base64.b64encode(rom[0x1B4D0:0x1B4D0+0x400]).decode()
thumb = 'data:image/png;base64,' + base64.b64encode(open('/home/claude/test/p.png','rb').read()).decode()
def init(mode):
    return """
var S={aspect:'4:3',bezels:true,integer:false,palette:'default',touch:'auto',padOpacity:70,split:'auto',swap:false,side:false,useExternal:true,skipLauncher:false,audio:true};
window.Host={ info:function(){return JSON.stringify({version:'0.1',mode:'%s',rom:{present:true,ok:true,crc:'70080810'},auto:true,settings:S,
  screens:{how:'hinge'},linkShown:true,slots:[{n:1,time:Date.now()-3600e3},{n:2,time:0},{n:3,time:Date.now()},{n:4,time:0},{n:5,time:0},{n:6,time:0}]});},
  font:function(){return '%s';}, thumb:function(n){return n==2?'':'%s';}, set:function(k,v){S[k]=v==='true'?true:v==='false'?false:v;},
  binds:function(){return JSON.stringify([{name:'UP',keys:['DPAD UP']},{name:'B',keys:['A','Z']},{name:'A',keys:['B','X']},{name:'TURBO B',keys:['X']}]);},
  rebind:function(){}, resume:function(){}, start:function(){}, save:function(){return true;}, load:function(){return true;} };
""" % (mode, font, thumb)
with sync_playwright() as p:
    b = p.chromium.launch()
    for mode, w, h, name in (('launcher', 900, 675, 'launcher'), ('pause', 675, 900, 'pause')):
        pg = b.new_page(viewport={'width': w, 'height': h}); errs=[]
        pg.on('pageerror', lambda e: errs.append(str(e)))
        pg.add_init_script(init(mode))
        pg.goto('file:///home/claude/metroid-dual/app/assets/menu/index.html'); pg.wait_for_timeout(600)
        pg.screenshot(path=f'/home/claude/test/menu_{name}.png')
        if mode == 'pause':
            pg.evaluate("pad('down')"); pg.evaluate("pad('down')"); pg.evaluate("pad('ok')"); pg.wait_for_timeout(500)
            pg.screenshot(path='/home/claude/test/menu_load.png')
            pg.evaluate("pad('back')"); 
            for i in range(2): pg.evaluate("pad('down')")
            pg.evaluate("pad('ok')"); pg.wait_for_timeout(300)
            pg.screenshot(path='/home/claude/test/menu_settings.png')
        print(name, errs)
    b.close()
