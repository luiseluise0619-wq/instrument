#!/usr/bin/env python3
"""Browser-only screenshot and interaction checks; NOT a JUCE compile/runtime test.
Requires playwright, with a Chromium install (system Chromium also supported).
"""
from __future__ import annotations
import base64
import json
import shutil
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]

def run() -> None:
    html = (ROOT / 'preview/index.html').read_text()
    for image in (ROOT / 'assets/reference').glob('*.png'):
        data = base64.b64encode(image.read_bytes()).decode('ascii')
        html = html.replace('../assets/reference/' + image.name, 'data:image/png;base64,' + data)
    checks: list[dict] = []
    errors: list[str] = []
    def check(name: str, value: bool) -> None:
        checks.append({'name': name, 'passed': bool(value)})
        if not value:
            raise AssertionError(name)
    try:
        with sync_playwright() as p:
            executable = shutil.which('chromium') or shutil.which('chromium-browser')
            kwargs = {'headless': True, 'args': ['--no-sandbox']}
            if executable:
                kwargs['executable_path'] = executable
            browser = p.chromium.launch(**kwargs)
            page = browser.new_page(viewport={'width': 1536, 'height': 1024}, device_scale_factor=1)
            page.on('pageerror', lambda e: errors.append(str(e)))
            # set_content avoids depending on a server, browser file-URL policy or network.
            page.set_content(html, wait_until='load')
            page.wait_for_function('window.__referenceReady === true')
            check('Canvas is 1536 x 1024', page.locator('#slyce').bounding_box()['width'] == 1536)
            check('Eight slice pad controls', page.locator('.pad').count() == 8)
            check('Seven knob controls', page.locator('.knob[role="slider"]').count() == 7)
            check('Five-octave keyboard: 35 white / 25 black keys',
                  page.locator('.whitekey').count() == 35 and page.locator('.blackkey').count() == 25)
            check('Current sound/preset count is derived correctly', page.evaluate('audio.presetCount') == 445)
            page.locator('.pad').nth(3).click()
            check('Pad selection moves', page.locator('.pad').nth(3).evaluate("e=>e.classList.contains('active')"))
            knob = page.get_by_role('slider', name='REVERB', exact=True)
            before = int(knob.get_attribute('aria-valuenow'))
            knob.focus()
            page.keyboard.press('ArrowUp')
            check('Knob keyboard input changes preview value', int(knob.get_attribute('aria-valuenow')) > before)
            box = knob.bounding_box()
            before = int(knob.get_attribute('aria-valuenow'))
            page.mouse.move(box['x']+box['width']/2,box['y']+box['height']/2)
            page.mouse.down()
            page.mouse.move(box['x']+box['width']/2,box['y']+box['height']/2-25, steps=6)
            page.mouse.up()
            check('Knob pointer drag changes preview value', int(knob.get_attribute('aria-valuenow')) > before)
            page.get_by_role('button', name='MANUAL', exact=True).click()
            check('Manual mode indication moves', page.get_by_role('button', name='MANUAL', exact=True).evaluate("e=>e.classList.contains('selected')"))
            page.get_by_role('button', name='AUTO', exact=True).click()
            page.get_by_role('button', name='LOOP', exact=True).click()
            check('Secondary panel opens', page.locator('#modal').is_visible())
            page.get_by_role('button', name='close', exact=True).click()
            check('Secondary panel closes', not page.locator('#modal').is_visible())
            page.get_by_role('button', name='more', exact=True).click()
            page.locator('#darkChoice').click()
            check('Menu changes dark theme', page.locator('body').evaluate("e=>e.classList.contains('dark')"))
            page.get_by_role('button', name='more', exact=True).click()
            page.locator('#lightChoice').click()
            check('Menu restores light theme', not page.locator('body').evaluate("e=>e.classList.contains('dark')"))
            page.locator('input.search').fill('Vocal')
            check('Search filters preview labels', page.locator('.row').first.evaluate("e=>e.style.opacity") == '.2' or page.locator('.row').first.evaluate("e=>e.style.opacity") == '0.2')
            page.locator('input.search').fill('')
            page.set_viewport_size({'width': 960,'height': 640})
            page.evaluate('fit()')
            scaled = page.locator('#slyce').bounding_box()
            check('Proportional fit at minimum native size', abs(scaled['width']-960)<1 and abs(scaled['height']-640)<1)
            # Reload for neutral screenshots, so tests leave no transient hover/menu state.
            page.close()
            page = browser.new_page(viewport={'width':1536,'height':1024}, device_scale_factor=1)
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.set_content(html, wait_until='load')
            page.wait_for_function('window.__referenceReady === true')
            page.evaluate("document.body.classList.add('screenshot');fit()")
            page.mouse.move(1520,1010)
            for theme_name in ('light','dark'):
                page.evaluate('dark='+str(theme_name=='dark').lower()+';theme()')
                page.wait_for_timeout(150)
                page.screenshot(path=str(ROOT/f'preview/skin_preview_{theme_name}.png'))
            check('No browser JavaScript errors', not errors)
            browser.close()
    finally:
        report = {'scope':'Browser skin/layout preview ONLY. Not native JUCE or DAW testing.',
                  'checks':checks,'javascript_errors':errors,'native_juce_build_verified':False}
        (ROOT/'reports/reference_preview_checks.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(checks)} browser preview checks passed. Native JUCE runtime remains unverified.')

if __name__=='__main__':
    run()
