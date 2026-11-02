import asyncio, sys
from playwright.async_api import async_playwright
async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch(args=['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist'])
        pg = await b.new_page(viewport={'width':1280,'height':760})
        logs=[]; pg.on('console', lambda m: logs.append(m.text)); pg.on('pageerror', lambda e: logs.append('ERR '+str(e)))
        await pg.goto('file:///home/claude/metroid-dual/tools/roomviewer/metroid-rooms.html')
        await pg.screenshot(path='/tmp/rv0.png')
        await pg.set_input_files('#file', '/home/claude/test/metroid.nes')
        await pg.wait_for_timeout(1500)
        await pg.screenshot(path='/tmp/rv1.png')
        for i,cell in enumerate(sys.argv[1:]):
            x,y = [int(v,16) for v in cell.split(',')]
            await pg.evaluate(f'selectCell({y*32+x})')
            await pg.wait_for_timeout(700)
            await pg.screenshot(path=f'/tmp/rv_{i+2}.png')
        print('\n'.join(logs))
        await b.close()
asyncio.run(main())
