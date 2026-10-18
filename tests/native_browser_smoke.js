const { chromium } = require('playwright');
const fs = require('fs');
const http = require('http');
const path = require('path');
const zlib = require('zlib');

const baseUrl = process.env.WEBSDR_HARNESS_URL || 'http://127.0.0.1:8073/';

function auditExtensionPanels() {
    const expected = [
        'ACARS', 'AIS', 'ALE_2G', 'CW_decoder', 'CW_skimmer', 'DRM', 'FAX', 'FFT',
        'FSK', 'FT8', 'HFDL', 'IBP_scan', 'IQ_display', 'Loran_C', 'NAVTEX', 'SSTV',
        'S_meter', 'TDoA', 'ant_switch', 'colormap', 'devl', 'digi_modes', 'example',
        'iframe', 'noise_blank', 'noise_filter', 'prefs', 's4285', 'space_weather',
        'timecode', 'waterfall', 'wspr'
    ].sort((a, b) => a.localeCompare(b));
    const extensionRoot = path.resolve(__dirname, '../web/extensions');
    const entries = [];

    for (const directory of fs.readdirSync(extensionRoot, { withFileTypes: true })) {
        if (!directory.isDirectory())
            continue;
        for (const file of fs.readdirSync(path.join(extensionRoot, directory.name))) {
            if (!file.endsWith('.js'))
                continue;
            const filePath = path.join(extensionRoot, directory.name, file);
            const source = fs.readFileSync(filePath, 'utf8');
            const main = source.match(/^\s*function\s+([A-Za-z0-9_]+)_main\s*\(/m);
            if (!main)
                continue;
            entries.push({
                name: main[1],
                file: path.relative(path.resolve(__dirname, '..'), filePath),
                sharedPanel: source.includes('ext_panel_show(')
            });
        }
    }

    entries.sort((a, b) => a.name.localeCompare(b.name));
    const extSource = fs.readFileSync(path.join(extensionRoot, 'ext.js'), 'utf8');
    const drmSource = fs.readFileSync(path.join(extensionRoot, 'DRM/DRM.js'), 'utf8');
    return {
        expected,
        entries,
        allShared: entries.every(entry => entry.sharedPanel),
        antSwitchRfTab: /use_rf_tab:\s*\[\s*'ant_switch'\s*\]/.test(extSource),
        drmCloseOverride: /id-ext-controls-close|toggle_panel\([\"']ext-controls/.test(drmSource)
    };
}

function fetchRawResponse(headers, path) {
    const url = new URL(baseUrl);
    return new Promise((resolve, reject) => {
        const request = http.request({
            host: url.hostname,
            port: url.port || 80,
            path: path || `${url.pathname}${url.search}`,
            headers
        }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('end', () => resolve({
                statusCode: response.statusCode,
                headers: response.headers,
                body: Buffer.concat(chunks)
            }));
        });
        request.on('error', reject);
        request.end();
    });
}

function fetchJson(url) {
    return new Promise((resolve, reject) => {
        http.get(url, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('end', () => {
                if (response.statusCode !== 200) {
                    reject(new Error(`fixture request failed with ${response.statusCode}`));
                    return;
                }
                try {
                    resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')));
                } catch (error) {
                    reject(error);
                }
            });
        }).on('error', reject);
    });
}

(async () => {
    const extensionPanelAudit = auditExtensionPanels();
    if (JSON.stringify(extensionPanelAudit.entries.map(entry => entry.name)) !==
            JSON.stringify(extensionPanelAudit.expected) ||
        !extensionPanelAudit.allShared ||
        !extensionPanelAudit.antSwitchRfTab ||
        extensionPanelAudit.drmCloseOverride)
        throw new Error(`invalid extension panel source audit: ${JSON.stringify(extensionPanelAudit)}`);

    const browser = await chromium.launch({
        headless: true,
        args: ['--autoplay-policy=no-user-gesture-required']
    });
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
    const errors = [];

    async function responsiveState(targetPage, viewport) {
        await targetPage.setViewportSize(viewport);
        await targetPage.waitForTimeout(650);
        await targetPage.evaluate(() => {
            if (typeof mobile_scale_control_panel !== 'function')
                return;
            const control = document.getElementById('id-control');
            if (!control)
                return;
            const mobile = ext_mobile_info();
            mobile_scale_control_panel(mobile,
                mobile.narrow || mobile.height < control.offsetHeight);
        });
        await targetPage.waitForTimeout(50);
        await targetPage.evaluate(() =>
            w3_el('id-nav-optbar-audio')?.click());
        await targetPage.waitForTimeout(50);
        return targetPage.evaluate(() => {
            const bounds = id => {
                const el = document.getElementById(id);
                if (!el)
                    return null;
                const rect = el.getBoundingClientRect();
                return {
                    left: Math.round(rect.left),
                    right: Math.round(rect.right),
                    width: Math.round(rect.width),
                    height: Math.round(rect.height)
                };
            };
            const rect = element => {
                if (!element)
                    return null;
                const box = element.getBoundingClientRect();
                return {
                    left: Math.round(box.left),
                    top: Math.round(box.top),
                    right: Math.round(box.right),
                    bottom: Math.round(box.bottom),
                    width: Math.round(box.width),
                    height: Math.round(box.height)
                };
            };
            const overlaps = (a, b) => !!a && !!b &&
                a.left < b.right && a.right > b.left &&
                a.top < b.bottom && a.bottom > b.top;
            w3_hide2('id-vol');
            w3_hide2('id-pan');
            w3_hide2('id-vol-comp', false);
            const audioRow = w3_el('id-vol-comp');
            const deEmphasis = rect(audioRow?.querySelector('.id-deemp:not(.w3-hide)'));
            const compression = rect(audioRow?.querySelector('.id-button-compression'));
            audio_panner_ui_init();
            w3_el('id-nav-optbar-rf')?.click();
            const themePickerElement = document.querySelector('.ui-theme-picker');
            const controlElement = document.getElementById('id-control');
            const themePicker = rect(themePickerElement);
            const control = rect(controlElement);
            return {
                viewport: [window.innerWidth, window.innerHeight],
                documentOverflow: document.documentElement.scrollWidth > window.innerWidth + 2,
                bodyOverflow: document.body.scrollWidth > window.innerWidth + 2,
                theme: document.documentElement.dataset.uiTheme,
                modern: document.documentElement.classList.contains('ui-modern'),
                main: bounds('id-main-container'),
                waterfall: bounds('id-waterfall-container'),
                control,
                themePicker,
                themeControlOverlap: overlaps(themePicker, control) &&
                    !controlElement?.contains(themePickerElement),
                controlFitsViewport: !control ||
                    (control.left >= -1 && control.top >= -1 &&
                        control.right <= window.innerWidth + 1 &&
                        control.bottom <= window.innerHeight + 1),
                audioControls: {
                    deEmphasis,
                    compression,
                    overlap: overlaps(deEmphasis, compression),
                    gap: deEmphasis && compression?
                        Math.round(compression.left - deEmphasis.right) : null
                },
                compactThemeReadable: window.innerWidth > 760 ||
                    (themePicker && themePicker.width >= 84 &&
                        getComputedStyle(document.getElementById('id-ui-theme-select')).color !==
                            'rgba(0, 0, 0, 0)')
            };
        });
    }

    async function extensionLayoutState(targetPage, name, viewport) {
        const markers = {
            CW_decoder: '.id-cw-controls',
            Loran_C: '.id-loran_c-controls',
            SSTV: '.id-sstv-freq-menu',
            colormap: '.id-colormap-controls',
            IBP_scan: '.id-IBP-menu',
            waterfall: '.id-waterfall-controls',
            wspr: '.id-wspr-controls',
            FT8: '.cl-ft8-text',
            DRM: '.id-drm-controls',
            ACARS: '.id-acars-controls',
            AIS: '.id-ais-controls'
        };
        await targetPage.setViewportSize(viewport);
        await targetPage.waitForTimeout(650);
        await targetPage.evaluate(() => {
            if (typeof mobile_scale_control_panel !== 'function')
                return;
            const mobile = ext_mobile_info();
            mobile_scale_control_panel(mobile, mobile.narrow);
        });
        await targetPage.evaluate(extensionName => extint_open(extensionName), name);
        try {
            await targetPage.waitForFunction(({ extensionName, marker }) =>
                extint.current_ext_name?.toLowerCase() === extensionName.toLowerCase() &&
                    !!document.querySelector(marker),
                { extensionName: name, marker: markers[name] }, { timeout: 10000 });
        } catch (error) {
            const state = await targetPage.evaluate(({ extensionName, marker }) => ({
                currentExtension: extint.current_ext_name,
                markerPresent: !!document.querySelector(marker),
                panelDisplayed: extint.displayed,
                soundSocketState: window.ws_snd?.readyState,
                waterfallSocketState: window.ws_wf?.readyState,
                extensionSocketState: extint.ws?.readyState,
                socketCloses: window.nativeSocketCloses,
                extensionNamesReady: Array.isArray(extint_names),
                selectedExtension: w3_el('id-select-ext')?.value
            }), { extensionName: name, marker: markers[name] });
            throw new Error(`extension ${name} did not become ready: ${JSON.stringify(state)}\n` +
                error.message);
        }
        if (name === 'Loran_C')
            await targetPage.evaluate(() => Loran_C_blur());
        await targetPage.waitForTimeout(name === 'DRM' ? 1000 : 350);
        if (name === 'FT8') {
            await targetPage.evaluate(() =>
                ft8_mode_cb('ft8.mode', ft8.FST4W_1800, false));
            await targetPage.waitForTimeout(100);
        }
        if (name === 'AIS') {
            await targetPage.waitForFunction(() => ais.kmap && ais.kmap.map,
                null, { timeout: 10000 });
            await targetPage.evaluate(() =>
                ais_report('type=1|mmsi=123456789|lat=37.774900|lon=-122.419400|' +
                    'sog=12.3|cog=270.0|hdg=270|name=TEST VESSEL|call=TEST123|payload=15MuqP0000'));
            await targetPage.waitForTimeout(100);
        }

        const collapseSnapshot = () => targetPage.evaluate(() => {
            const panel = document.getElementById('id-ext-controls');
            const button = document.getElementById('id-ext-controls-collapse-btn');
            const content = document.querySelector('.id-ext-controls-container');
            const close = document.getElementById('id-ext-controls-close');
            const panelRect = panel.getBoundingClientRect();
            const buttonRect = button.getBoundingClientRect();
            const hit = document.elementFromPoint(
                buttonRect.left + buttonRect.width / 2,
                buttonRect.top + buttonRect.height / 2);
            return {
                collapsed: panel.classList.contains('ui-extension-collapsed'),
                state: extint.panel_collapsed,
                displayed: extint.displayed,
                currentExtension: extint.current_ext_name,
                viewportHeight: window.innerHeight,
                panelTop: Math.round(panelRect.top),
                panelBottom: Math.round(panelRect.bottom),
                handle: {
                    left: Math.round(buttonRect.left),
                    top: Math.round(buttonRect.top),
                    right: Math.round(buttonRect.right),
                    bottom: Math.round(buttonRect.bottom),
                    width: Math.round(buttonRect.width),
                    height: Math.round(buttonRect.height),
                    tappable: hit === button || button.contains(hit)
                },
                ariaExpanded: button.getAttribute('aria-expanded'),
                label: button.getAttribute('aria-label'),
                icon: document.getElementById('id-ext-controls-collapse-icon').className,
                contentVisibility: getComputedStyle(content).visibility,
                contentAriaHidden: content.getAttribute('aria-hidden'),
                closeVisibility: getComputedStyle(close).visibility
            };
        });
        const collapse = { expanded: await collapseSnapshot() };
        await targetPage.locator('#id-ext-controls-collapse-btn').click();
        await targetPage.waitForTimeout(250);
        collapse.collapsed = await collapseSnapshot();
        await targetPage.locator('#id-ext-controls-collapse-btn').click();
        await targetPage.waitForTimeout(250);
        collapse.restored = await collapseSnapshot();

        const layout = await targetPage.evaluate(extensionName => {
            const root = document.querySelector('.id-ext-controls-container');
            const visible = element => {
                const style = getComputedStyle(element);
                const rect = element.getBoundingClientRect();
                return style.display !== 'none' && style.visibility !== 'hidden' &&
                    rect.width > 1 && rect.height > 1;
            };
            const bounds = element => {
                const rect = element.getBoundingClientRect();
                return {
                    left: Number(rect.left.toFixed(1)),
                    top: Number(rect.top.toFixed(1)),
                    right: Number(rect.right.toFixed(1)),
                    bottom: Number(rect.bottom.toFixed(1))
                };
            };
            const controls = Array.from(
                root.querySelectorAll('button, select, input, textarea')).filter(visible);
            const overlaps = [];
            for (let first = 0; first < controls.length; first++) {
                for (let second = first + 1; second < controls.length; second++) {
                    const a = bounds(controls[first]);
                    const b = bounds(controls[second]);
                    const width = Math.max(0,
                        Math.min(a.right, b.right) - Math.max(a.left, b.left));
                    const height = Math.max(0,
                        Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top));
                    if (width * height > 2) {
                        overlaps.push({
                            first: controls[first].className || controls[first].tagName,
                            second: controls[second].className || controls[second].tagName
                        });
                    }
                }
            }

            const layoutHooks = Array.from(root.querySelectorAll(
                '.ui-extension-control-row, .ui-extension-control-grid, ' +
                '.ui-extension-two-column, .ui-cw-metric-card'));
            const helpButton = w3_el('id-ext-controls-help-btn');
            const helpStyle = getComputedStyle(helpButton);
            const helpRect = helpButton.getBoundingClientRect();
            return {
                name: extensionName,
                viewport: [window.innerWidth, window.innerHeight],
                controls: controls.length,
                overlaps,
                overflowingHooks: layoutHooks.filter(element =>
                    element.scrollWidth > element.clientWidth + 1).map(element => element.className),
                helpButton: {
                    visible: helpRect.width > 1 && helpRect.height > 1,
                    height: Math.round(helpRect.height),
                    minHeight: helpStyle.minHeight,
                    paddingTop: helpStyle.paddingTop,
                    paddingBottom: helpStyle.paddingBottom
                },
                drmRegistered: extint_names.includes('DRM'),
                drmRendered: extensionName !== 'DRM' ||
                    root.textContent.includes('Digital Radio Mondiale decoder') ||
                    !!document.querySelector('.id-drm-panel-container'),
                fstwRendered: extensionName !== 'FT8' ||
                    root.textContent.includes('FST4W-1800'),
                fstwSelected: extensionName !== 'FT8' ||
                    ft8.mode === ft8.FST4W_1800,
                aisMapRendered: extensionName !== 'AIS' ||
                    !!ais.kmap?.map,
                aisVesselRendered: extensionName !== 'AIS' ||
                    (document.querySelectorAll('.id-ais-vessel').length === 1 &&
                        document.querySelector('.id-ais-list')?.textContent.includes('TEST VESSEL'))
            };
        }, name);
        layout.collapse = collapse;
        return layout;
    }

    async function dxDialogLayoutState(targetPage, viewport) {
        await targetPage.setViewportSize(viewport);
        await targetPage.waitForTimeout(650);
        await targetPage.evaluate(() => {
            const mobile = ext_mobile_info();
            mobile_scale_control_panel(mobile, mobile.narrow);
            dx.db = dx.DB_STORED;
            dx.o.gid = -1;
            dx_show_edit_panel2();
        });
        await targetPage.waitForFunction(() =>
            !!document.querySelector('.id-dx-edit-panel') &&
            !!document.getElementById('id-dx.o.begin') &&
            !!document.getElementById('id-dx.o.end'));
        await targetPage.waitForTimeout(100);

        return targetPage.evaluate(() => {
            const form = document.querySelector('.id-dx-edit-panel');
            const begin = document.getElementById('id-dx.o.begin');
            const end = document.getElementById('id-dx.o.end');
            const bounds = element => {
                const rect = element.getBoundingClientRect();
                return {
                    left: rect.left,
                    top: rect.top,
                    right: rect.right,
                    bottom: rect.bottom,
                    width: rect.width,
                    height: rect.height
                };
            };
            const visibleAndTappable = element => {
                const rect = bounds(element);
                const hit = document.elementFromPoint(
                    rect.left + rect.width / 2, rect.top + rect.height / 2);
                return rect.width > 1 && rect.height > 1 &&
                    rect.left >= 0 && rect.right <= window.innerWidth &&
                    rect.top >= 0 && rect.bottom <= window.innerHeight &&
                    (hit === element || element.contains(hit));
            };
            const beginRect = bounds(begin);
            const endRect = bounds(end);
            const rows = ['id-dx-fields', 'id-dx-extension', 'id-dx-schedule', 'id-dx-actions']
                .map(className => document.querySelector(`.${className}`));
            return {
                documentOverflow: document.documentElement.scrollWidth > window.innerWidth + 1,
                formOverflow: form.scrollWidth > form.clientWidth + 1,
                rowOverflow: rows.some(row => row.scrollWidth > row.clientWidth + 1),
                beginTappable: visibleAndTappable(begin),
                endTappable: visibleAndTappable(end),
                timeInputsOverlap: beginRect.left < endRect.right &&
                    beginRect.right > endRect.left &&
                    beginRect.top < endRect.bottom &&
                    beginRect.bottom > endRect.top
            };
        });
    }

    const geolocationFixture = {
        city: 'Test City',
        country_name: 'Test Country',
        country: 'Test Country',
        region: 'Test Region',
        regionName: 'Test Region'
    };
    await page.route('https://ipapi.co/json', route => route.fulfill({
        status: 200, contentType: 'application/json', body: JSON.stringify(geolocationFixture)
    }));
    await page.route('https://get.geojs.io/v1/ip/geo.json', route => route.fulfill({
        status: 200, contentType: 'application/json', body: JSON.stringify(geolocationFixture)
    }));
    await page.route('http://ip-api.com/json?fields=49177', route => route.fulfill({
        status: 200, contentType: 'application/json', body: JSON.stringify(geolocationFixture)
    }));

    await page.route('https://services.swpc.noaa.gov/**', async route => {
        const path = new URL(route.request().url()).pathname;
        const fixtures = {
            '/products/noaa-planetary-k-index.json':
                [{ time_tag: '2026-09-12T18:00:00', Kp: 2.33, a_running: 9 }],
            '/products/noaa-planetary-k-index-forecast.json': [
                { time_tag: '2026-09-13T00:00:00', kp: 2.0, observed: 'predicted' },
                { time_tag: '2026-09-13T03:00:00', kp: 3.0, observed: 'predicted' }
            ],
            '/products/summary/10cm-flux.json':
                [{ flux: 109, time_tag: '2026-09-12T20:00:00' }],
            '/json/goes/primary/xray-background-7-day.json':
                [{ time_tag: '2026-09-11T00:00:00Z', background: 3.1e-7 }],
            '/products/summary/solar-wind-mag-field.json':
                [{ bt: 7, bz_gsm: -4, time_tag: '2026-09-12T23:17:00Z' }]
        };
        if (!fixtures[path])
            return route.abort();
        await route.fulfill({
            status: 200,
            contentType: 'application/json',
            body: JSON.stringify(fixtures[path])
        });
    });

    page.on('console', message => {
        if (message.type() === 'error')
            errors.push(`console: ${message.text()}`);
    });
    page.on('pageerror', error => errors.push(`page: ${error.message}`));

    try {
        const identityResponse = await fetchRawResponse({ 'Accept-Encoding': 'identity' });
        const gzipResponse = await fetchRawResponse({ 'Accept-Encoding': 'gzip' });
        const disabledGzipResponse = await fetchRawResponse({ 'Accept-Encoding': 'gzip;q=0' });
        const gzipVersionResponse = await fetchRawResponse({ 'Accept-Encoding': 'gzip' }, '/VER');
        const pngResponse = await fetchRawResponse({ 'Accept-Encoding': 'gzip' },
            '/gfx/openwebrx-play-button.png');
        const vary = gzipResponse.headers.vary || '';
        let versionResponse;
        try {
            versionResponse = JSON.parse(zlib.gunzipSync(gzipVersionResponse.body));
        } catch (error) {
            versionResponse = { error: error.message };
        }
        if (identityResponse.statusCode !== 200 || gzipResponse.statusCode !== 200 ||
            disabledGzipResponse.statusCode !== 200 ||
            gzipResponse.headers['content-encoding'] !== 'gzip' ||
            !vary.toLowerCase().split(',').map(value => value.trim()).includes('accept-encoding') ||
            identityResponse.headers['content-encoding'] ||
            disabledGzipResponse.headers['content-encoding'] ||
            !zlib.gunzipSync(gzipResponse.body).equals(identityResponse.body) ||
            !disabledGzipResponse.body.equals(identityResponse.body) ||
            gzipVersionResponse.statusCode !== 200 ||
            gzipVersionResponse.headers['content-encoding'] !== 'gzip' ||
            !Number.isInteger(versionResponse.maj) ||
            !Number.isInteger(versionResponse.min) ||
            !Number.isInteger(versionResponse.ts) ||
            pngResponse.statusCode !== 200 ||
            pngResponse.headers['content-encoding']) {
            throw new Error(`invalid gzip HTTP response: ${JSON.stringify({
                identity: {
                    statusCode: identityResponse.statusCode,
                    contentEncoding: identityResponse.headers['content-encoding']
                },
                gzip: {
                    statusCode: gzipResponse.statusCode,
                    contentEncoding: gzipResponse.headers['content-encoding'],
                    vary: gzipResponse.headers.vary
                },
                disabledGzip: {
                    statusCode: disabledGzipResponse.statusCode,
                    contentEncoding: disabledGzipResponse.headers['content-encoding']
                },
                gzipVersion: {
                    statusCode: gzipVersionResponse.statusCode,
                    contentEncoding: gzipVersionResponse.headers['content-encoding'],
                    body: versionResponse
                },
                png: {
                    statusCode: pngResponse.statusCode,
                    contentEncoding: pngResponse.headers['content-encoding']
                }
            })}`);
        }

        await page.goto(baseUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });
        await page.waitForFunction(() => {
            return window.waterfall_setup_done === 1 &&
                window.ws_snd && window.ws_snd.readyState === WebSocket.OPEN &&
                window.ws_wf && window.ws_wf.readyState === WebSocket.OPEN;
        }, null, { timeout: 30000 });
        await page.evaluate(() => {
            window.nativeSocketCloses = {};
            for (const [name, socket] of [
                ['sound', window.ws_snd],
                ['waterfall', window.ws_wf]
            ]) {
                socket.addEventListener('close', event => {
                    window.nativeSocketCloses[name] = {
                        code: event.code,
                        reason: event.reason,
                        wasClean: event.wasClean
                    };
                }, { once: true });
            }
        });

        const initialLine = await page.evaluate(() => window.wf_canvas_actual_line);
        await page.waitForFunction(line => {
            return Number.isFinite(window.wf_canvas_actual_line) &&
                window.wf_canvas_actual_line !== line;
        }, initialLine, { timeout: 30000 });

        await page.waitForFunction(() => typeof window.noise_filter_init === 'function');
        await page.evaluate(() => {
            window.nativeNRCommands = [];
            const send = window.ws_snd.send.bind(window.ws_snd);
            window.ws_snd.send = command => {
                if (typeof command === 'string' && command.startsWith('SET nr '))
                    window.nativeNRCommands.push(command);
                send(command);
            };
            w3_el('id-nav-optbar-audio')?.click();
        });
        const nrSelect = page.locator('select.id-nr_algo');
        const nrOptions = await nrSelect.locator('option:not(:disabled)').evaluateAll(options =>
            options.map(option => [option.textContent, option.value]));
        const expectedNROptions = [
            ['off', '0'], ['Super NR', '4'], ['wdsp', '1'], ['LMS', '2'], ['spec', '3']
        ];
        if (JSON.stringify(nrOptions) !== JSON.stringify(expectedNROptions))
            throw new Error(`invalid NR menu order or algorithm IDs: ${JSON.stringify(nrOptions)}`);
        for (const algo of ['1', '2', '3', '0']) {
            await nrSelect.selectOption(algo);
            const nrSelection = await page.evaluate(() => ({
                algo: noise_filter.algo,
                stored: kiwi_storeGet('last_nr_algo'),
                commands: window.nativeNRCommands
            }));
            if (nrSelection.algo !== Number(algo) || nrSelection.stored !== algo ||
                !nrSelection.commands.includes(`SET nr algo=${algo}`))
                throw new Error(`NR menu algorithm mismatch: ${JSON.stringify(nrSelection)}`);
        }
        await nrSelect.selectOption({ label: 'Super NR' });
        await page.evaluate(() => extint_open('noise_filter'));
        await page.waitForFunction(() =>
            extint.current_ext_name === 'noise_filter' &&
            !!w3_el('noise_filter.super_level'));
        const nrSlider = page.locator('input[class~="id-noise_filter.super_level"]');
        await page.waitForTimeout(600);
        if (await nrSlider.getAttribute('min') !== '0' ||
            await nrSlider.getAttribute('max') !== '30' ||
            await nrSlider.getAttribute('step') !== '1')
            throw new Error('Super NR slider must expose all integer levels 0-30');
        for (const level of [0, 15, 30]) {
            await nrSlider.evaluate((element, value) => {
                element.value = String(value);
                element.dispatchEvent(new Event('input', { bubbles: true }));
                element.dispatchEvent(new Event('change', { bubbles: true }));
            }, level);
            await page.waitForTimeout(350);
            const nrState = await page.evaluate(() => ({
                algo: noise_filter.algo,
                level: noise_filter.super_level,
                stored: kiwi_storeGet('last_nr_SuperLevel'),
                enabled: noise_filter.denoise,
                commands: window.nativeNRCommands,
                sound: window.ws_snd.readyState,
                waterfall: window.ws_wf.readyState
            }));
            if (nrState.algo !== 4 || nrState.level !== level ||
                nrState.stored !== String(level) || !nrState.enabled ||
                !nrState.commands.includes(`SET nr type=0 param=0 pval=${level}`) ||
                nrState.sound !== 1 || nrState.waterfall !== 1)
                throw new Error(`Super NR control failed: ${JSON.stringify(nrState)}`);
        }
        const nrGainSlider = page.locator('input[class~="id-noise_filter.super_gain"]');
        if (await nrGainSlider.getAttribute('min') !== '-30' ||
            await nrGainSlider.getAttribute('max') !== '30' ||
            await nrGainSlider.getAttribute('step') !== '1')
            throw new Error('Super NR gain must match spectral NR: -30..30 dB');
        for (const gain of [-30, 0, 30]) {
            await nrGainSlider.evaluate((element, value) => {
                element.value = String(value);
                element.dispatchEvent(new Event('input', { bubbles: true }));
                element.dispatchEvent(new Event('change', { bubbles: true }));
            }, gain);
            const gainState = await page.evaluate(() => ({
                gain: noise_filter.super_gain,
                stored: kiwi_storeGet('last_nr_SuperGain'),
                commands: window.nativeNRCommands,
                label: w3_el('noise_filter.super_gain').parentNode.textContent
            }));
            if (gainState.gain !== gain || gainState.stored !== String(gain) ||
                !gainState.commands.includes(`SET nr type=0 param=1 pval=${Math.pow(10, gain/20)}`) ||
                !gainState.label.includes(`Gain: ${gain} dB`))
                throw new Error(`Super NR gain failed: ${JSON.stringify(gainState)}`);
        }
        await page.evaluate(() => {
            noise_filter_init();
            if (noise_filter.algo !== 4 || noise_filter.super_level !== 30 ||
                noise_filter.super_gain !== 30)
                throw new Error('Super NR selection/level was not restored');
            noise_filter_load_defaults();
            if (noise_filter.super_level !== cfg.nr_superLevel ||
                noise_filter.super_gain !== cfg.nr_superGain)
                throw new Error('Super NR server default was not restored');
            nr_algo_cb('nr_algo', 0, false, 'm');
            extint_panel_hide();
        });

        await page.evaluate(() => {
            const focusReturn = document.createElement('button');
            focusReturn.id = 'id-test-focus-return';
            focusReturn.type = 'button';
            focusReturn.textContent = 'focus return';
            focusReturn.style.position = 'fixed';
            focusReturn.style.left = '-10000px';
            document.body.appendChild(focusReturn);
            focusReturn.focus();
            window.nativeSocketCloses.extension = null;
            extint_open('space_weather');
            extint.ws.addEventListener('close', event => {
                window.nativeSocketCloses.extension = {
                    code: event.code,
                    reason: event.reason,
                    wasClean: event.wasClean
                };
            }, { once: true });
        });
        await page.waitForFunction(() => {
            const el = w3_el('id-sw-data');
            return el && el.textContent.includes('Solar flux') &&
                el.textContent.includes('109') &&
                el.textContent.includes('Kp 2.0 - 3.0') &&
                el.textContent.includes('-4.0 nT');
        }, null, { timeout: 30000 });
        await page.locator('#id-ext-controls-close').focus();
        await page.waitForFunction(() =>
            document.activeElement?.id === 'id-ext-controls-close',
            null, { timeout: 30000 });
        const state = await page.evaluate(() => ({
            title: document.title,
            soundSocket: window.ws_snd.readyState,
            waterfallSocket: window.ws_wf.readyState,
            waterfallLine: window.wf_canvas_actual_line,
            waterfallCanvases: window.wf_canvases.length,
            spaceWeather: w3_el('id-sw-data').textContent,
            spectrumPassbandCanvas: {
                present: !!window.spec.passband_canvas,
                pointerEvents: window.spec.passband_canvas.style.pointerEvents,
                width: window.spec.passband_canvas.width,
                height: window.spec.passband_canvas.height
            },
            squelchRecording: (() => {
                const saved = {
                    sndSend: window.snd_send,
                    curMode: window.cur_mode,
                    squelch: window.squelch,
                    squelchTail: window.squelch_tail,
                    recording: window.recording,
                    recordingMeta: window.recording_meta,
                    audioLastSq: window.audio_last_sq,
                    audioModeIq: window.audio_mode_iq,
                    audioData: Array.from(window.audio_data.slice(0, 512)),
                    audioDataUnsquelched: Array.from(window.audio_data_unsquelched.slice(0, 512)),
                    preBuf: window.kiwi.pre_buf,
                    preData: window.kiwi.pre_data,
                    preSize: window.kiwi.pre_size,
                    preOff: window.kiwi.pre_off,
                    preWrapped: window.kiwi.pre_wrapped,
                    preCaptured: window.kiwi.pre_captured,
                    prePingPong: window.kiwi.pre_ping_pong
                };
                try {
                    let command;
                    window.snd_send = value => { command = value; };
                    window.cur_mode = 'am';
                    window.squelch = 7;
                    window.squelch_tail = 3;
                    send_squelch();

                    window.audio_mode_iq = false;
                    window.audio_last_sq = true;
                    window.recording = true;
                    window.recording_meta = {
                        buffers: [new ArrayBuffer(65536)],
                        data: null,
                        offset: 0,
                        total_size: 0
                    };
                    window.recording_meta.data = new DataView(window.recording_meta.buffers[0]);
                    window.kiwi.pre_size = 1536;
                    window.kiwi.pre_buf = new ArrayBuffer(window.kiwi.pre_size);
                    window.kiwi.pre_data = new DataView(window.kiwi.pre_buf);
                    window.kiwi.pre_off = 0;
                    window.kiwi.pre_wrapped = false;
                    window.kiwi.pre_captured = false;
                    window.kiwi.pre_ping_pong = 0;
                    for (let i = 0; i < 512; i++)
                        window.audio_data_unsquelched[i] = 1000 + i;
                    audio_record(false);
                    for (let i = 0; i < 512; i++)
                        window.audio_data_unsquelched[i] = 2000 + i;
                    audio_record(false);

                    window.audio_last_sq = false;
                    for (let i = 0; i < 512; i++)
                        window.audio_data[i] = 3000 + i;
                    audio_record(false);

                    let preSequenceMatches = true;
                    for (let i = 0; i < 768; i++) {
                        const expected = (i < 256)? 1256 + i : 2000 + i - 256;
                        if (window.recording_meta.data.getInt16(i * 2, true) !== expected)
                            preSequenceMatches = false;
                    }
                    let currentSequenceMatches = true;
                    for (let i = 0; i < 512; i++) {
                        if (window.recording_meta.data.getInt16(1536 + i * 2, true) !== 3000 + i)
                            currentSequenceMatches = false;
                    }
                    return {
                        command,
                        totalSize: window.recording_meta.total_size,
                        preSequenceMatches,
                        currentSequenceMatches,
                        preCaptured: window.kiwi.pre_captured
                    };
                } finally {
                    window.snd_send = saved.sndSend;
                    window.cur_mode = saved.curMode;
                    window.squelch = saved.squelch;
                    window.squelch_tail = saved.squelchTail;
                    window.recording = saved.recording;
                    window.recording_meta = saved.recordingMeta;
                    window.audio_last_sq = saved.audioLastSq;
                    window.audio_mode_iq = saved.audioModeIq;
                    window.audio_data.set(saved.audioData, 0);
                    window.audio_data_unsquelched.set(saved.audioDataUnsquelched, 0);
                    window.kiwi.pre_buf = saved.preBuf;
                    window.kiwi.pre_data = saved.preData;
                    window.kiwi.pre_size = saved.preSize;
                    window.kiwi.pre_off = saved.preOff;
                    window.kiwi.pre_wrapped = saved.preWrapped;
                    window.kiwi.pre_captured = saved.preCaptured;
                    window.kiwi.pre_ping_pong = saved.prePingPong;
                }
            })(),
            spectrumPassband: (() => {
                const savedCenter = center_freq;
                try {
                    center_freq = 1000;
                    const range = { start:1000, bw:1000 };
                    return {
                        visible: spectrum_passband_px(
                            { offset_frequency:500, low_cut:-100, high_cut:200 }, range, 1000, 975),
                        clipped: spectrum_passband_px(
                            { offset_frequency:0, low_cut:-100, high_cut:100 }, range, 1000, 975),
                        axisScaled: spectrum_passband_px(
                            { offset_frequency:900, low_cut:0, high_cut:50 }, range, 1000, 975),
                        rightClipped: spectrum_passband_px(
                            { offset_frequency:950, low_cut:-10, high_cut:100 }, range, 1000, 975),
                        narrow: spectrum_passband_px(
                            { offset_frequency:998.6, low_cut:0, high_cut:0.8 }, range, 1000, 999),
                        reversed: spectrum_passband_px(
                            { offset_frequency:500, low_cut:200, high_cut:-100 }, range, 1000, 975),
                        offscreen: spectrum_passband_px(
                            { offset_frequency:-500, low_cut:-200, high_cut:-100 }, range, 1000, 975)
                    };
                } finally {
                    center_freq = savedCenter;
                }
            })(),
            dxLabelRows: (() => {
                const rows = [0, 1, 2, 3].map(i => dx_label_top_px(i));
                const label = document.createElement('button');
                label.className = 'w3-btn w3-ext-btn ui-button w3-round-large cl-dx-label';
                label.textContent = 'DX';
                label.style.setProperty('--dx-label-bg', '#123456');
                label.style.backgroundColor = '#123456';
                document.body.appendChild(label);
                const style = getComputedStyle(label);
                const labelHeight = label.getBoundingClientRect().height;
                const labelFontSize = parseFloat(style.fontSize);
                const labelPadding = parseFloat(style.paddingTop);
                const labelRadius = parseFloat(style.borderTopLeftRadius);
                const firstTypeColor = style.backgroundColor;
                label.style.setProperty('--dx-label-bg', '#abcdef');
                label.style.backgroundColor = '#abcdef';
                const secondTypeColor = getComputedStyle(label).backgroundColor;
                label.remove();
                return {
                    rows, labelHeight, labelFontSize, labelPadding, labelRadius,
                    firstTypeColor, secondTypeColor
                };
            })()
        }));

        const uiFoundation = await page.evaluate(() => {
            const select = document.getElementById('id-ui-theme-select');
            const themes = [];
            const rgb = hex => {
                const value = hex.replace('#', '');
                return [0, 2, 4].map(offset => parseInt(value.slice(offset, offset + 2), 16));
            };
            const luminance = color => {
                const values = rgb(color).map(value => {
                    const channel = value / 255;
                    return channel <= 0.03928 ? channel / 12.92 :
                        Math.pow((channel + 0.055) / 1.055, 2.4);
                });
                return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722;
            };
            const contrast = (a, b) => {
                const first = luminance(a);
                const second = luminance(b);
                return (Math.max(first, second) + 0.05) /
                    (Math.min(first, second) + 0.05);
            };
            for (const theme of ['midnight', 'ember', 'cloud', 'classic']) {
                select.value = theme;
                select.dispatchEvent(new Event('change', { bubbles: true }));
                const style = getComputedStyle(document.documentElement);
                const muted = style.getPropertyValue('--ui-text-muted').trim();
                const surface = style.getPropertyValue('--ui-surface').trim();
                const picker = select.closest('.ui-theme-picker');
                const actions = picker.parentElement;
                themes.push({
                    theme: document.documentElement.dataset.uiTheme,
                    accent: style.getPropertyValue('--ui-accent').trim(),
                    mutedContrast: Number(contrast(muted, surface).toFixed(2)),
                    modern: document.documentElement.classList.contains('ui-modern'),
                    classic: document.documentElement.classList.contains('ui-classic'),
                    actionsDisplay: getComputedStyle(actions).display,
                    actionsJustify: getComputedStyle(actions).justifyContent
                });
            }
            select.value = 'midnight';
            select.dispatchEvent(new Event('change', { bubbles: true }));
            return {
                selectPresent: !!select,
                receiverThemeParent: select?.closest('.ui-theme-picker')?.parentElement?.id,
                themeOptions: Array.from(select.options, option => option.value),
                storedTheme: localStorage.getItem('web888_ui_theme'),
                themes,
                semanticShell: {
                    header: document.getElementById('id-top-container').tagName,
                    main: document.getElementById('id-main-container').tagName,
                    panels: document.getElementById('id-panels-container').tagName
                },
                hooks: {
                    buttons: document.querySelectorAll('.ui-button').length,
                    fields: document.querySelectorAll('.ui-field').length,
                    panels: document.querySelectorAll('.class-panel').length
                },
                panelToggle: {
                    tagName: document.getElementById('id-control-hide')?.tagName,
                    tabIndex: document.getElementById('id-control-hide')?.tabIndex,
                    label: document.getElementById('id-control-hide')?.getAttribute('aria-label')
                },
                extensionOutput: (() => {
                    const content = document.getElementById('id-ext-data-container');
                    const output = document.createElement('div');
                    output.className = 'w3-text-output';
                    content.appendChild(output);
                    document.body.appendChild(content);
                    const style = getComputedStyle(output);
                    const result = {
                        color: style.color,
                        background: style.backgroundColor,
                        fontSize: parseFloat(style.fontSize)
                    };
                    output.remove();
                    return result;
                })(),
                typography: {
                    panel: parseFloat(getComputedStyle(document.getElementById('id-control')).fontSize),
                    stationName: parseFloat(getComputedStyle(document.getElementById('id-rx-title')).fontSize),
                    rf: parseFloat(getComputedStyle(document.getElementById('id-nav-optbar-rf')).fontSize),
                    wf: parseFloat(getComputedStyle(document.getElementById('id-nav-optbar-wf')).fontSize),
                    audio: parseFloat(getComputedStyle(document.getElementById('id-nav-optbar-audio')).fontSize),
                    agc: parseFloat(getComputedStyle(document.getElementById('id-nav-optbar-agc')).fontSize),
                    select: parseFloat(getComputedStyle(document.getElementById('id-select-band')).fontSize),
                    selectColor: getComputedStyle(document.getElementById('id-select-band')).color,
                    selectBackground: getComputedStyle(document.getElementById('id-select-band')).backgroundColor
                },
                controlAlignment: (() => {
                    const buttons = Array.from(document.querySelectorAll('#id-control .class-button'));
                    return [
                        ['AM', buttons.find(element => element.textContent.trim() === 'AM')],
                        ['SAM', buttons.find(element => element.textContent.trim() === 'SAM')],
                        ['RF', document.getElementById('id-nav-optbar-rf')],
                        ['WF', document.getElementById('id-nav-optbar-wf')],
                        ['AUD', document.getElementById('id-nav-optbar-audio')],
                        ['More', buttons.find(element => element.textContent.trim() === 'More')]
                    ].map(([text, element]) => {
                        if (!element)
                            return { text, missing: true };
                        const style = getComputedStyle(element);
                        return {
                            text,
                            display: style.display,
                            alignItems: style.alignItems,
                            justifyContent: style.justifyContent
                        };
                    });
                })(),
                controlFocus: (() => {
                    const selectedOptbar = document.querySelector(
                        '.id-optbar .w3int-cur-sel')?.id;
                    const controls = [
                        { selector: '#id-select-band' },
                        { selector: '#id-select-ext' },
                        { selector: '.id-optbar-wf select.ui-select', nav: 'id-nav-optbar-wf' },
                        { selector: '.id-optbar-audio select.ui-select', nav: 'id-nav-optbar-audio' }
                    ];
                    const result = controls.map(control => {
                        if (control.nav) document.getElementById(control.nav).click();
                        const element = document.querySelector(control.selector);
                        element?.focus();
                        const style = element? getComputedStyle(element) : null;
                        return {
                            selector: control.selector,
                            exists: !!element,
                            height: element?.getBoundingClientRect().height || 0,
                            outlineOffset: style?.outlineOffset,
                            focused: document.activeElement === element
                        };
                    });
                    if (selectedOptbar)
                        document.getElementById(selectedOptbar).click();
                    return result;
                })(),
                wfFilterRow: (() => {
                    const selectedOptbar = document.querySelector(
                        '.id-optbar .w3int-cur-sel')?.id;
                    document.getElementById('id-nav-optbar-wf').click();
                    const row = document.querySelector('.ui-wf-filter-row');
                    const children = Array.from(row.children);
                    const rects = children.map(element => element.getBoundingClientRect());
                    const selectStyle = getComputedStyle(row.querySelector('select'));
                    const result = {
                        display: getComputedStyle(row).display,
                        gaps: rects.slice(1).map((rect, index) =>
                            Number((rect.left - rects[index].right).toFixed(1))),
                        selectFontSize: parseFloat(selectStyle.fontSize),
                        selectPaddingLeft: parseFloat(selectStyle.paddingLeft),
                        selectPaddingRight: parseFloat(selectStyle.paddingRight)
                    };
                    if (selectedOptbar)
                        document.getElementById(selectedOptbar).click();
                    return result;
                })(),
                optbarScroll: (() => {
                    const selectedOptbar = document.querySelector(
                        '.id-optbar .w3int-cur-sel')?.id;
                    const area = document.querySelector('.id-optbar-content');
                    const tabs = ['rf', 'wf', 'audio', 'agc', 'users', 'status'];
                    const states = tabs.map(tab => {
                        document.getElementById('id-nav-optbar-'+ tab).click();
                        area.scrollTop = area.scrollHeight;
                        const style = getComputedStyle(area);
                        const state = {
                            tab,
                            overflowY: style.overflowY,
                            scrollbarWidth: style.scrollbarWidth,
                            scrollbarGutter: style.scrollbarGutter,
                            scrollable: area.scrollHeight > area.clientHeight,
                            scrolled: area.scrollTop > 0
                        };
                        area.scrollTop = 0;
                        return state;
                    });
                    if (selectedOptbar)
                        document.getElementById(selectedOptbar).click();
                    return states;
                })(),
                step9_10: (() => {
                    const cell = w3_el('id-9-10-cell');
                    const button = w3_el('id-button-9-10');
                    const savedFrequency = freq_displayed_Hz;
                    const savedMode = cur_mode;
                    const findBand = find_band;
                    window.find_band = () => ({ name: 'MW' });
                    freq_displayed_Hz = 1000000;
                    cur_mode = 'am';
                    freq_step_update_ui(true);
                    const band = find_band(freq_displayed_Hz);
                    const before = button.textContent;
                    const beforeStep = freq_step_amount(band).step_Hz;
                    button.click();
                    const after = button.textContent;
                    const afterStep = freq_step_amount(band).step_Hz;
                    button.click();
                    const enabled = !cell.classList.contains('w3-disabled');
                    freq_displayed_Hz = savedFrequency;
                    cur_mode = savedMode;
                    window.find_band = findBand;
                    freq_step_update_ui(true);
                    return {
                        enabled,
                        before,
                        after,
                        steps: [beforeStep, afterStep].sort((a, b) => a - b)
                    };
                })(),
                tabPalette: (() => {
                    const tabs = ['rf', 'wf', 'audio', 'agc', 'users', 'status', 'off']
                        .map(id => document.getElementById(`id-nav-optbar-${id}`))
                        .filter(Boolean);
                    const selected = tabs.find(tab => tab.classList.contains('w3int-cur-sel'));
                    const sample = selected || tabs[0];
                    const inactive = tabs.filter(tab => tab !== selected);
                    const inactiveBackgrounds = Array.from(new Set(inactive.map(tab =>
                        getComputedStyle(tab).backgroundColor)));
                    if (!selected) sample.classList.add('w3int-cur-sel');
                    const selectedBackground = getComputedStyle(sample).backgroundColor;
                    if (!selected) sample.classList.remove('w3int-cur-sel');
                    return {
                        labels: tabs.map(tab => tab.textContent),
                        legacyColorClasses: tabs.filter(tab =>
                            Array.from(tab.classList).some(name =>
                                /^w3-(green|pink|blue|purple|aqua|yellow|black)$/.test(name))).length,
                        inactiveBackgrounds,
                        selectedBackground
                    };
                })()
            };
        });
        const cloudControlContrast = await page.evaluate(() => {
            const luminance = color => {
                const values = color.match(/[\d.]+/g).slice(0, 3).map(value => {
                    const channel = +value / 255;
                    return channel <= 0.03928? channel / 12.92 :
                        Math.pow((channel + 0.055) / 1.055, 2.4);
                });
                return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722;
            };
            const contrast = (first, second) => {
                const values = [luminance(first), luminance(second)].sort((a, b) => b - a);
                return (values[0] + 0.05) / (values[1] + 0.05);
            };
            const fixture = document.createElement('div');
            fixture.innerHTML =
                '<div class="class-button" style="color: white">Button</div>' +
                '<div class="class-button-small" style="color: white">Button</div>';
            modern_ui_set_theme('cloud', false);
            document.getElementById('id-control').appendChild(fixture);
            const buttons = Array.from(fixture.children).map(button => {
                const style = getComputedStyle(button);
                return {
                    className: button.className,
                    color: style.color,
                    background: style.backgroundColor,
                    contrast: Number(contrast(style.color, style.backgroundColor).toFixed(2))
                };
            });
            fixture.remove();
            const zoomFixture = document.createElement('div');
            zoomFixture.className = 'id-control-zoom';
            zoomFixture.innerHTML =
                '<div class="class-icon"><img src="icons/zoomin.png" width="32" height="32"></div>' +
                '<div class="class-icon"><img src="icons/zoomout.png" width="32" height="32"></div>';
            document.getElementById('id-control').appendChild(zoomFixture);
            const zoomIcons = Array.from(zoomFixture.querySelectorAll('.class-icon')).map(icon => {
                const image = icon.querySelector('img');
                return {
                    background: getComputedStyle(icon).backgroundColor,
                    imageBackground: getComputedStyle(image).backgroundColor,
                    imageFilter: getComputedStyle(image).filter
                };
            });
            zoomFixture.remove();
            const stepFixture = document.createElement('div');
            stepFixture.id = 'id-step-freq';
            stepFixture.innerHTML =
                '<div class="ui-frequency-step"><img src="icons/stepdn.20.png" ' +
                    'width="20" height="20"></div>' +
                '<div class="ui-frequency-step"><img src="icons/stepup.16.png" ' +
                    'width="16" height="16" style="padding-bottom:2px"></div>';
            document.getElementById('id-control').appendChild(stepFixture);
            const stepButtons = Array.from(stepFixture.children).map(button => {
                const image = button.querySelector('img');
                return {
                    background: getComputedStyle(button).backgroundColor,
                    imageBackground: getComputedStyle(image).backgroundColor,
                    imageFilter: getComputedStyle(image).filter,
                    width: Math.round(button.getBoundingClientRect().width),
                    height: Math.round(button.getBoundingClientRect().height)
                };
            });
            stepFixture.remove();
            const visibilityFixture = document.createElement('div');
            visibilityFixture.className = 'class-vis';
            visibilityFixture.innerHTML =
                '<button class="class-vis-button"><img src="icons/hideleft.24.png" ' +
                    'width="24" height="24"></button>';
            document.getElementById('id-control').appendChild(visibilityFixture);
            const visibilityButton = visibilityFixture.querySelector('.class-vis-button');
            const visibilityImage = visibilityButton.querySelector('img');
            const visibilityArrow = {
                buttonBackground: getComputedStyle(visibilityButton).backgroundColor,
                imageBackground: getComputedStyle(visibilityImage).backgroundColor,
                imageFilter: getComputedStyle(visibilityImage).filter,
                width: Math.round(visibilityButton.getBoundingClientRect().width),
                height: Math.round(visibilityButton.getBoundingClientRect().height)
            };
            visibilityFixture.remove();
            modern_ui_set_theme('midnight', false);
            return { buttons, zoomIcons, stepButtons, visibilityArrow };
        });
        const extensionThemes = await page.evaluate(async () => {
            const select = document.getElementById('id-ui-theme-select');
            const content = document.querySelector('.id-ext-controls-container');
            const fixture = document.createElement('div');
            fixture.id = 'id-extension-theme-fixture';
            fixture.innerHTML =
                '<h3 class="ui-extension-heading">Extension heading</h3>' +
                '<button class="ui-button w3-blue">Action</button>' +
                '<input class="ui-input" value="value">' +
                '<div class="ui-extension-notice">Help text</div>' +
                '<div class="ui-extension-notice ui-extension-warning">Warning text</div>' +
                '<div class="w3-background-pale-aqua w3-text-black">Legacy help text</div>' +
                '<div class="w3-text-output">Decoded text</div>' +
                '<table><tbody><tr><td>row one</td></tr><tr><td>row two</td></tr></tbody></table>';
            content.appendChild(fixture);

            const color = element => getComputedStyle(element).color;
            const background = element => getComputedStyle(element).backgroundColor;
            const themes = [];
            for (const theme of ['midnight', 'ember', 'cloud', 'classic']) {
                select.value = theme;
                select.dispatchEvent(new Event('change', { bubbles: true }));
                await new Promise(resolve => setTimeout(resolve, 180));
                const panel = document.getElementById('id-ext-controls');
                const row = document.querySelector('.sw-row');
                const title = document.querySelector('.sw-section-title');
                const button = fixture.querySelector('button');
                const input = fixture.querySelector('input');
                const notices = fixture.querySelectorAll('.ui-extension-notice');
                const legacy = fixture.querySelector('.w3-background-pale-aqua');
                const output = fixture.querySelector('.w3-text-output');
                const rows = fixture.querySelectorAll('tr');
                themes.push({
                    theme: document.documentElement.dataset.uiTheme,
                    panel: {
                        color: color(panel),
                        background: background(panel),
                        border: getComputedStyle(panel).borderTopColor
                    },
                    contentColor: color(content),
                    titleColor: color(title),
                    rowBorder: getComputedStyle(row).borderBottomColor,
                    button: { color: color(button), background: background(button) },
                    input: { color: color(input), background: background(input) },
                    notice: { color: color(notices[0]), background: background(notices[0]) },
                    warning: { color: color(notices[1]), background: background(notices[1]) },
                    legacy: { color: color(legacy), background: background(legacy) },
                    output: { color: color(output), background: background(output) },
                    rows: Array.from(rows, background)
                });
            }
            fixture.remove();
            select.value = 'midnight';
            select.dispatchEvent(new Event('change', { bubbles: true }));
            return themes;
        });
        const drmThemeAssets = await page.evaluate(async () => {
            const [script, stylesheet, sharedExtensionScript] = await Promise.all([
                fetch('extensions/DRM/DRM.js').then(response => response.text()),
                fetch('extensions/DRM/DRM.css').then(response => response.text()),
                fetch('extensions/ext.js').then(response => response.text())
            ]);
            return {
                lightCloseIcon: sharedExtensionScript.includes("icons/close.24.png") &&
                    !script.includes("icons/close.black.24.png"),
                themedScheduleMarker:
                    stylesheet.includes('background-color: var(--ui-border-strong)') &&
                    stylesheet.includes('opacity: 0.55')
            };
        });
        const extensionFocus = await page.evaluate(() => {
            const close = document.getElementById('id-ext-controls-close');
            close?.focus();
            const expected = extint.return_focus?.id;
            const closeSemantics = {
                tagName: close?.tagName,
                tabIndex: close?.tabIndex,
                focused: document.activeElement === close,
                wrapperOverflow: getComputedStyle(
                    document.querySelector('.id-ext-controls-container')).overflow,
                wrapperFits: (() => {
                    const wrapper = document.querySelector('.id-ext-controls-container');
                    return wrapper.scrollWidth <= wrapper.clientWidth + 1 &&
                        wrapper.scrollHeight <= wrapper.clientHeight + 1;
                })()
            };
            return { closeSemantics, expected };
        });
        await page.locator('#id-ext-controls-close').press('Enter');
        await page.waitForTimeout(50);
        Object.assign(extensionFocus, await page.evaluate(() => {
            const state = {
                restored: document.activeElement?.id,
                displayed: extint.displayed,
                panelVisible: getComputedStyle(document.getElementById('id-ext-controls')).visibility
            };
            document.getElementById('id-test-focus-return')?.remove();
            return state;
        }));

        await page.evaluate(() => w3_click_nav('optbar-rf', 'optbar'));

        const receiverResponsive = [];
        for (const viewport of [
            { width: 1440, height: 1000 },
            { width: 1024, height: 768 },
            { width: 390, height: 844 },
            { width: 844, height: 390 }
        ])
            receiverResponsive.push(await responsiveState(page, viewport));

        await page.setViewportSize({ width: 390, height: 844 });
        await page.selectOption('#id-select-ext', { label: 'FAX' });
        await page.waitForFunction(() => extint.current_ext_name === 'FAX' &&
            document.querySelector('.id-fax-controls'));
        await page.waitForTimeout(250);
        const faxMobile = await page.evaluate(() => {
            const viewportWidth = window.innerWidth;
            const panel = document.getElementById('id-ext-controls').getBoundingClientRect();
            const close = document.getElementById('id-ext-controls-close').getBoundingClientRect();
            const collapse = document.getElementById(
                'id-ext-controls-collapse-btn').getBoundingClientRect();
            const data = document.querySelector('.id-fax-data');
            const dataRect = data.getBoundingClientRect();
            const actionRows = Array.from(
                document.querySelectorAll('.id-fax-controls .w3-show-inline-new'));
            return {
                documentOverflow: document.documentElement.scrollWidth > viewportWidth + 1,
                panel: { left: panel.left, right: panel.right, width: panel.width },
                close: { left: close.left, right: close.right, width: close.width, height: close.height },
                collapse: {
                    left: collapse.left,
                    right: collapse.right,
                    width: collapse.width,
                    height: collapse.height
                },
                data: {
                    left: dataRect.left,
                    right: dataRect.right,
                    width: dataRect.width,
                    overflowX: getComputedStyle(data).overflowX
                },
                actionOverflow: actionRows.some(row => row.scrollWidth > row.clientWidth + 1)
            };
        });
        await page.locator('#id-ext-controls-collapse-btn').click();
        await page.waitForTimeout(250);
        const faxCollapse = {
            collapsed: await page.evaluate(() => {
                const panel = document.getElementById('id-ext-controls');
                const panelRect = panel.getBoundingClientRect();
                const button = document.getElementById('id-ext-controls-collapse-btn');
                const buttonRect = button.getBoundingClientRect();
                const data = document.querySelector('.id-fax-data');
                const dataRect = data.getBoundingClientRect();
                const content = document.querySelector('.id-ext-controls-container');
                const dataContainer = document.getElementById('id-ext-data-container');
                const hit = document.elementFromPoint(
                    buttonRect.left + buttonRect.width / 2,
                    buttonRect.top + buttonRect.height / 2);
                return {
                    state: extint.panel_collapsed,
                    displayed: extint.displayed,
                    currentExtension: extint.current_ext_name,
                    selectedExtension: w3_el('id-select-ext').value,
                    panelTop: Math.round(panelRect.top),
                    viewportHeight: window.innerHeight,
                    handleVisible: buttonRect.top >= 0 &&
                        buttonRect.bottom <= window.innerHeight &&
                        (hit === button || button.contains(hit)),
                    ariaExpanded: button.getAttribute('aria-expanded'),
                    label: button.getAttribute('aria-label'),
                    icon: document.getElementById(
                        'id-ext-controls-collapse-icon').className,
                    contentVisibility: content? getComputedStyle(content).visibility:'missing',
                    dataVisible: !!dataContainer &&
                        getComputedStyle(dataContainer).display !== 'none' &&
                        dataRect.width > 1 && dataRect.height > 1
                };
            })
        };
        await page.evaluate(() => extint_open('SSTV'));
        await page.waitForFunction(() => extint.current_ext_name === 'SSTV' &&
            document.querySelector('.id-sstv-freq-menu'));
        await page.waitForTimeout(250);
        faxCollapse.resetOnOpen = await page.evaluate(() => {
            const panel = document.getElementById('id-ext-controls');
            const button = document.getElementById('id-ext-controls-collapse-btn');
            return {
                state: extint.panel_collapsed,
                collapsed: panel.classList.contains('ui-extension-collapsed'),
                ariaExpanded: button.getAttribute('aria-expanded'),
                label: button.getAttribute('aria-label'),
                icon: document.getElementById('id-ext-controls-collapse-icon').className,
                contentVisibility: getComputedStyle(
                    document.querySelector('.id-ext-controls-container')).visibility
            };
        });
        await page.locator('#id-ext-controls-close').click();

        const extensionLayouts = [];
        for (const viewport of [
            { width: 1440, height: 1000 },
            { width: 390, height: 844 }
        ]) {
            for (const extension of [
                'CW_decoder', 'Loran_C', 'SSTV', 'colormap',
                'IBP_scan', 'waterfall', 'wspr', 'FT8', 'DRM', 'ACARS', 'AIS'
            ])
                extensionLayouts.push(
                    await extensionLayoutState(page, extension, viewport));
        }
        await page.locator('#id-ext-controls-close').click();

        await page.evaluate(() => extint_open('DRM'));
        await page.waitForFunction(() => extint.current_ext_name === 'DRM' &&
            !!document.querySelector('.id-drm-controls, .id-drm-panel-container'));
        await page.waitForTimeout(1000);
        await page.locator('#id-ext-controls-collapse-btn').click();
        await page.waitForTimeout(250);
        const drmPanelControls = {
            collapsed: await page.evaluate(() => ({
                state: extint.panel_collapsed,
                displayed: extint.displayed,
                currentExtension: extint.current_ext_name,
                panelVisible: getComputedStyle(
                    document.getElementById('id-ext-controls')).visibility
            }))
        };
        await page.locator('#id-ext-controls-collapse-btn').click();
        await page.waitForTimeout(250);
        await page.locator('#id-ext-controls-close').click();
        await page.waitForTimeout(100);
        drmPanelControls.closed = await page.evaluate(() => ({
            state: extint.panel_collapsed,
            displayed: extint.displayed,
            currentExtension: extint.current_ext_name,
            panelVisible: getComputedStyle(
                document.getElementById('id-ext-controls')).visibility,
            selectedExtension: w3_el('id-select-ext').value
        }));

        const dxDialogMobile = [];
        for (const theme of ['midnight', 'classic']) {
            await page.evaluate(selectedTheme => modern_ui_set_theme(selectedTheme, false), theme);
            dxDialogMobile.push(await dxDialogLayoutState(page, { width: 390, height: 844 }));
            await page.evaluate(() => w3_el('id-ext-controls-close').click());
        }
        await page.evaluate(() => modern_ui_set_theme('midnight', false));

        await page.evaluate(() => extint_open('ACARS'));
        await page.waitForSelector('.id-acars-test', { timeout: 10000 });
        await page.waitForTimeout(500);
        await page.locator('.id-acars-test').click();
        await page.waitForFunction(() => {
            const text = document.querySelector('.id-acars-messages')?.textContent || '';
            return text.includes('B-18722') && text.includes('CI5118') &&
                text.includes('/KLAX.TI2/024KLAXA91A1');
        }, null, { timeout: 10000 });
        await page.locator('#id-ext-controls-close').click();

        const panelToggle = { desktop: {}, phone: {}, readme: {} };
        await page.setViewportSize({ width: 1440, height: 1000 });
        await page.waitForTimeout(650);
        await page.evaluate(() => mobile_scale_control_panel(ext_mobile_info(), false));
        await page.locator('#id-control-hide').click();
        await page.waitForTimeout(1100);
        panelToggle.desktop.hidden = await page.evaluate(() => {
            const panel = document.getElementById('id-control');
            const panelRect = panel.getBoundingClientRect();
            const show = document.getElementById('id-control-show');
            const showRect = show.getBoundingClientRect();
            return {
                shown: panel.panelShown,
                panelLeft: Math.round(panelRect.left),
                viewportWidth: window.innerWidth,
                hideDisplay: getComputedStyle(document.getElementById('id-control-hide')).display,
                showDisplay: getComputedStyle(show).display,
                showVisible: showRect.left >= 0 && showRect.right <= window.innerWidth
            };
        });
        await page.locator('#id-control-show').click();
        await page.waitForTimeout(1100);
        panelToggle.desktop.shown = await page.evaluate(() => {
            const panel = document.getElementById('id-control');
            const rect = panel.getBoundingClientRect();
            return {
                shown: panel.panelShown,
                left: Math.round(rect.left),
                right: Math.round(rect.right),
                viewportWidth: window.innerWidth,
                hideDisplay: getComputedStyle(document.getElementById('id-control-hide')).display,
                showDisplay: getComputedStyle(document.getElementById('id-control-show')).display
            };
        });

        await page.evaluate(() => toggle_panel('id-readme', 1));
        await page.waitForTimeout(1100);
        await page.locator('#id-readme-hide').click();
        await page.waitForTimeout(1100);
        panelToggle.readme.hidden = await page.evaluate(() => {
            const panel = document.getElementById('id-readme');
            const panelRect = panel.getBoundingClientRect();
            const show = document.getElementById('id-readme-show');
            const showRect = show.getBoundingClientRect();
            return {
                shown: panel.panelShown,
                panelRight: Math.round(panelRect.right),
                hideDisplay: getComputedStyle(document.getElementById('id-readme-hide')).display,
                showDisplay: getComputedStyle(show).display,
                showVisible: showRect.left >= 0 && showRect.right <= window.innerWidth
            };
        });
        await page.locator('#id-readme-show').click();
        await page.waitForTimeout(1100);
        panelToggle.readme.shown = await page.evaluate(() => {
            const panel = document.getElementById('id-readme');
            const rect = panel.getBoundingClientRect();
            return {
                shown: panel.panelShown,
                left: Math.round(rect.left),
                right: Math.round(rect.right),
                viewportWidth: window.innerWidth,
                hideDisplay: getComputedStyle(document.getElementById('id-readme-hide')).display,
                showDisplay: getComputedStyle(document.getElementById('id-readme-show')).display
            };
        });

        await page.setViewportSize({ width: 390, height: 844 });
        await page.waitForTimeout(650);
        await page.evaluate(() => {
            const mobile = ext_mobile_info();
            mobile_scale_control_panel(mobile, true);
        });
        await page.locator('#id-control-hide').click();
        await page.waitForTimeout(1100);
        panelToggle.phone.hidden = await page.evaluate(() => {
            const panel = document.getElementById('id-control');
            const panelRect = panel.getBoundingClientRect();
            const show = document.getElementById('id-control-show');
            const showRect = show.getBoundingClientRect();
            return {
                shown: panel.panelShown,
                panelLeft: Math.round(panelRect.left),
                viewportWidth: window.innerWidth,
                hideDisplay: getComputedStyle(document.getElementById('id-control-hide')).display,
                showDisplay: getComputedStyle(show).display,
                showVisible: showRect.left >= 0 && showRect.right <= window.innerWidth
            };
        });
        await page.locator('#id-control-show').click();
        await page.waitForTimeout(1100);
        panelToggle.phone.shown = await page.evaluate(() => {
            const panel = document.getElementById('id-control');
            const rect = panel.getBoundingClientRect();
            return {
                shown: panel.panelShown,
                left: Math.round(rect.left),
                right: Math.round(rect.right),
                viewportWidth: window.innerWidth,
                hideDisplay: getComputedStyle(document.getElementById('id-control-hide')).display,
                showDisplay: getComputedStyle(document.getElementById('id-control-show')).display,
                scaled: panel.panel_isScaled,
                transform: panel.style.transform
            };
        });
        await page.setViewportSize({ width: 1440, height: 1000 });

        const adminPage = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
        adminPage.on('console', message => {
            if (message.type() === 'error')
                errors.push(`admin console: ${message.text()}`);
        });
        adminPage.on('pageerror', error => errors.push(`admin page: ${error.message}`));
        const clickAdminNav = id =>
            adminPage.evaluate(navId => document.getElementById(navId).click(), id);
        await adminPage.goto(new URL('admin', baseUrl).href,
            { waitUntil: 'domcontentloaded', timeout: 30000 });
        await adminPage.waitForFunction(() => {
            return typeof airband_adc_clock_effective === 'function' &&
                window.adm && w3_el('id-airband-adc-clock-status');
        }, null, { timeout: 30000 });
        const airbandClockAdmin = await adminPage.evaluate(() => {
            const saved = {
                airband: adm.airband,
                sndRate: adm.snd_rate,
                clock: adm.airband_adc_clock
            };
            try {
                adm.airband = true;
                adm.airband_adc_clock = 0;
                adm.snd_rate = 1;
                airband_adc_clock_status();
                const shownInAirband = !w3_el('id-airband-adc-clock-field')
                    .classList.contains('w3-hide');
                const preferred = w3_el('id-airband-adc-clock-status').textContent;

                adm.snd_rate = 2;
                airband_adc_clock_status();
                const forced = w3_el('id-airband-adc-clock-status').textContent;
                const forcedDisabled = w3_el('id-airband-adc-clock').disabled;
                const forcedValue = +w3_el('id-airband-adc-clock').value;

                adm.airband_adc_clock = 1;
                airband_adc_clock_status();
                const advanced = w3_el('id-airband-adc-clock-status').textContent;

                adm.snd_rate = 1;
                airband_adc_clock_status();
                const restoredEnabled = !w3_el('id-airband-adc-clock').disabled;

                adm.snd_rate = 2;
                adm.airband_adc_clock = 1;
                airband_rx_rate_cb('adm.snd_rate', 0, true);
                airband_adc_clock_cb('adm.airband_adc_clock', 0, true);
                const staleFirstIgnored =
                    adm.snd_rate === 2 && adm.airband_adc_clock === 1;

                adm.airband = false;
                airband_adc_clock_status();
                const hiddenInHF = w3_el('id-airband-adc-clock-field')
                    .classList.contains('w3-hide');
                const hiddenStatusCleared =
                    w3_el('id-airband-adc-clock-status').textContent === '';

                return {
                    effective: [
                        airband_adc_clock_effective(0, 0),
                        airband_adc_clock_effective(0, 1),
                        airband_adc_clock_effective(0, 2),
                        airband_adc_clock_effective(1, 2)
                    ],
                    shownInAirband,
                    preferred,
                    forced,
                    forcedDisabled,
                    forcedValue,
                    advanced,
                    restoredEnabled,
                    staleFirstIgnored,
                    hiddenInHF,
                    hiddenStatusCleared
                };
            } finally {
                adm.airband = saved.airband;
                adm.snd_rate = saved.sndRate;
                adm.airband_adc_clock = saved.clock;
                airband_adc_clock_status();
            }
        });
        const adminResponsive = [];
        for (const viewport of [
            { width: 1440, height: 1000 },
            { width: 1024, height: 768 },
            { width: 390, height: 844 }
        ])
            adminResponsive.push(await responsiveState(adminPage, viewport));
        await adminPage.setViewportSize({ width: 1440, height: 1000 });
        await adminPage.waitForFunction(() =>
            document.getElementById('id-status-user-count')?.textContent.includes('receiver channels active'),
            null, { timeout: 10000 });
        const adminFoundation = await adminPage.evaluate(() => ({
            theme: document.documentElement.dataset.uiTheme,
            shell: document.querySelector('.id-admin')?.classList.contains('ui-admin-shell'),
            nav: document.querySelector('.ui-admin-nav')?.getAttribute('role'),
            pages: document.querySelectorAll('.ui-admin-page[role="tabpanel"]').length,
            title: document.querySelector('.ui-admin-titlebar h1')?.textContent,
            themeParent: document.querySelector('.ui-theme-picker')?.parentElement?.id,
            themePosition: getComputedStyle(document.querySelector('.ui-theme-picker')).position,
            restartNotice: (() => {
                const status = document.querySelector('.id-restart .ui-status');
                const heading = status?.querySelector('h5');
                const style = status? getComputedStyle(status) : null;
                return {
                    danger: status?.classList.contains('ui-status-danger'),
                    fontSize: heading? parseFloat(getComputedStyle(heading).fontSize) : 0,
                    text: heading?.textContent,
                    display: style?.display,
                    alignItems: style?.alignItems,
                    justifyContent: style?.justifyContent
                };
            })(),
            keyboardNavigation: (() => {
                const before = document.querySelector('.ui-admin-nav [aria-selected="true"]');
                before?.focus();
                before?.dispatchEvent(new KeyboardEvent('keydown', {
                    key: 'ArrowRight',
                    bubbles: true
                }));
                const after = document.querySelector('.ui-admin-nav [aria-selected="true"]');
                return {
                    before: before?.id,
                    after: after?.id,
                    focused: document.activeElement?.id
                };
            })()
        }));
        const adminClassic = await adminPage.evaluate(() => {
            const select = document.getElementById('id-ui-theme-select');
            select.value = 'classic';
            select.dispatchEvent(new Event('change', { bubbles: true }));
            const page = document.querySelector('.ui-admin-page');
            const state = {
                theme: document.documentElement.dataset.uiTheme,
                rootClassic: document.documentElement.classList.contains('ui-classic'),
                rootModern: document.documentElement.classList.contains('ui-modern'),
                bodyClassic: document.body.classList.contains('ui-classic'),
                titleDisplay: getComputedStyle(document.querySelector('.ui-admin-titlebar > div:first-child')).display,
                titlebarPosition: getComputedStyle(document.querySelector('.ui-admin-titlebar')).position,
                pickerPosition: getComputedStyle(document.querySelector('.ui-theme-picker')).position,
                pickerTop: document.querySelector('.ui-theme-picker').getBoundingClientRect().top,
                pickerRight: document.querySelector('.ui-theme-picker').getBoundingClientRect().right,
                pageBorderRadius: getComputedStyle(page).borderRadius,
                pageBoxShadow: getComputedStyle(page).boxShadow,
                statusColumns: getComputedStyle(document.querySelector('.ui-admin-status-grid')).gridTemplateColumns,
                runtimeColumns: getComputedStyle(document.querySelector('.ui-admin-runtime-grid')).gridTemplateColumns,
                cpuRowDisplay: getComputedStyle(document.querySelector('.ui-admin-runtime-cpu-row')).display,
                histogramHeight: parseFloat(getComputedStyle(
                    document.querySelector('.ui-admin-runtime-histogram-bars')).height)
            };
            document.getElementById('id-nav-control').click();
            state.control = {
                sectionColumns: getComputedStyle(document.querySelector(
                    '.ui-admin-control .ui-admin-section-grid')).gridTemplateColumns,
                actionColumns: getComputedStyle(document.querySelector(
                    '.ui-admin-control-actions')).gridTemplateColumns,
                actionDisplay: getComputedStyle(document.querySelector(
                    '.ui-admin-control-action')).display,
                fieldColumns: getComputedStyle(document.querySelector(
                    '.ui-admin-control-fields')).gridTemplateColumns,
                sessionDisplay: getComputedStyle(document.querySelector(
                    '.ui-admin-control-session-action')).display
            };
            document.getElementById('id-nav-connect').click();
            state.connect = {
                selectorDisplay: getComputedStyle(document.querySelector(
                    '.ui-admin-connect .id-admin-nav-dom')).display,
                fieldColumns: getComputedStyle(document.querySelector(
                    '.ui-admin-duc-fields')).gridTemplateColumns,
                controlColumns: getComputedStyle(document.querySelector(
                    '.ui-admin-duc-controls')).gridTemplateColumns,
                actionDisplay: getComputedStyle(document.querySelector(
                    '.ui-admin-duc-action')).display,
                statusDisplay: getComputedStyle(document.querySelector(
                    '.ui-admin-duc-status')).display,
                proxyHeaderColor: getComputedStyle(
                    document.querySelector('.ui-admin-connect .id-proxy-hdr')).color,
                proxyHeaderBackground: getComputedStyle(
                    document.querySelector('.ui-admin-connect .id-proxy-hdr')).backgroundColor
            };
            select.value = 'midnight';
            select.dispatchEvent(new Event('change', { bubbles: true }));
            state.connect.modernProxyHeaderColor = getComputedStyle(
                document.querySelector('.ui-admin-connect .id-proxy-hdr')).color;
            state.connect.modernProxyHeaderBackground = getComputedStyle(
                document.querySelector('.ui-admin-connect .id-proxy-hdr')).backgroundColor;
            return state;
        });
        const adminWarningThemes = await adminPage.evaluate(() => {
            const select = document.getElementById('id-ui-theme-select');
            const warning = document.querySelector('.ui-admin-connect .ui-admin-warning');
            const connectValue = document.querySelector(
                '.ui-admin-connect .ui-admin-connect-value-missing');
            const luminance = color => {
                const values = color.match(/[\d.]+/g).slice(0, 3).map(value => {
                    const channel = +value / 255;
                    return channel <= 0.03928? channel / 12.92 :
                        Math.pow((channel + 0.055) / 1.055, 2.4);
                });
                return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722;
            };
            const contrast = (first, second) => {
                const values = [luminance(first), luminance(second)].sort((a, b) => b - a);
                return (values[0] + 0.05) / (values[1] + 0.05);
            };
            const themes = ['midnight', 'ember', 'cloud', 'classic'].map(theme => {
                select.value = theme;
                select.dispatchEvent(new Event('change', { bubbles: true }));
                const style = getComputedStyle(warning);
                const valueStyle = getComputedStyle(connectValue);
                return {
                    theme,
                    display: style.display,
                    alignItems: style.alignItems,
                    background: style.backgroundColor,
                    border: style.borderLeftColor,
                    color: style.color,
                    marker: getComputedStyle(warning, '::before').content,
                    headingMargin: getComputedStyle(warning.querySelector('h5')).margin,
                    connectValueBackground: valueStyle.backgroundColor,
                    connectValueBorder: valueStyle.borderColor,
                    connectValueColor: valueStyle.color,
                    connectValueContrast: Number(
                        contrast(valueStyle.color, valueStyle.backgroundColor).toFixed(2))
                };
            });
            select.value = 'midnight';
            select.dispatchEvent(new Event('change', { bubbles: true }));
            return {
                themes,
                warningCount: document.querySelectorAll('.ui-admin-warning').length,
                hardcodedYellow: document.querySelectorAll('.ui-admin-warning.w3-yellow').length,
                legacyConnectColors: document.querySelectorAll(
                    '.ui-admin-connect-value.w3-override-yellow, ' +
                    '.ui-admin-connect-value.w3-background-pale-aqua').length
            };
        });
        const adminStatus = await adminPage.evaluate(() => {
            const status = document.querySelector('.ui-admin-page.id-status[role="tabpanel"]');
            const cards = Array.from(status?.querySelectorAll('.ui-admin-status-card') || []);
            const grid = status?.querySelector('.ui-admin-status-grid');
            const users = status?.querySelector('.ui-admin-status-users');
            const runtime = status?.querySelector('.ui-admin-status-runtime');
            const gridTemplate = grid? getComputedStyle(grid).gridTemplateColumns : '';
            return {
                heading: status?.querySelector('.ui-admin-status-header h2')?.textContent,
                cardTitles: cards.map(card => card.querySelector('h3')?.textContent),
                gridTemplate,
                twoColumnGrid: gridTemplate.startsWith('repeat(2,'),
                userSummary: status?.querySelector('#id-status-user-count')?.textContent,
                userRows: status?.querySelectorAll('.id-users-list > div').length || 0,
                usersBorder: users? getComputedStyle(users).borderStyle : '',
                runtime: {
                    panels: runtime?.querySelectorAll('.ui-admin-runtime-panel').length || 0,
                    cpuRows: runtime?.querySelectorAll('.ui-admin-runtime-cpu-row').length || 0,
                    trafficSegments:
                        runtime?.querySelectorAll('.ui-admin-runtime-throughput-bar i').length || 0,
                    sparkline: !!runtime?.querySelector('.ui-admin-runtime-sparkline polyline'),
                    counters: runtime?.querySelectorAll('.ui-admin-runtime-counters > div').length || 0,
                    histograms: runtime?.querySelectorAll('.ui-admin-runtime-histogram').length || 0,
                    histogramBars:
                        runtime?.querySelectorAll('.ui-admin-runtime-histogram-bars i').length || 0,
                    resetButton: Array.from(runtime?.querySelectorAll('button') || [])
                        .some(button => button.textContent.trim() === 'Reset queue stats')
                }
            };
        });
        await clickAdminNav('id-nav-usage');
        await adminPage.waitForFunction(() =>
            document.querySelectorAll('.ui-usage-heat-cell').length === 168 &&
            document.querySelectorAll('.ui-usage-summary-card').length === 8,
            null, { timeout: 10000 });
        const emptyUsageRange = await adminPage.evaluate(async () => {
            const now = Date.now() / 1000;
            const future = (usage.heatmap?.cells || []).filter(cell => cell.start > now);
            const unavailable = Array.from(document.querySelectorAll('.ui-usage-heat-cell'))
                .find(cell => cell.classList.contains('is-unavailable'));
            if (!unavailable) return null;
            unavailable.click();
            await new Promise(resolve => setTimeout(resolve, 300));
            return {
                futureSlotsAreZero: future.length > 0 && future.every(cell =>
                    !cell.available && cell.listener_minutes === 0 &&
                    cell.sessions === 0 && cell.unique === 0),
                dayText: document.querySelector('#id-usage-day')?.textContent || '',
                hourText: document.querySelector('#id-usage-hour')?.textContent || ''
            };
        });
        if (!emptyUsageRange || !emptyUsageRange.futureSlotsAreZero ||
            /[1-9]\d{14,}/.test(emptyUsageRange.dayText) ||
            emptyUsageRange.hourText.includes('214053043954515968')) {
            throw new Error(`invalid empty usage range: ${JSON.stringify(emptyUsageRange)}`);
        }
        await adminPage.evaluate(() => ext_send('SET usage_test_stress=5000'));
        await adminPage.waitForTimeout(500);
        await adminPage.evaluate(() => usage_refresh());
        await adminPage.waitForTimeout(300);
        await adminPage.waitForFunction(() =>
            document.querySelectorAll('#id-usage-geo .ui-usage-bar-row').length > 0,
            null, { timeout: 10000 });
        const adminUsage = await adminPage.evaluate(async () => {
            const cells = Array.from(document.querySelectorAll('.ui-usage-heat-cell'));
            const partial = cells.find(cell => cell.classList.contains('is-partial'));
            partial?.click();
            await new Promise(resolve => setTimeout(resolve, 300));
            const metric = document.getElementById('id-usage-metric');
            metric.value = 'unique';
            metric.dispatchEvent(new Event('change', { bubbles: true }));
            await new Promise(resolve => setTimeout(resolve, 150));
            const page = document.querySelector('.ui-admin-usage');
            const rect = page.getBoundingClientRect();
            const luminance = color => {
                const rgb = color.match(/[\d.]+/g).slice(0, 3).map(value => {
                    const channel = Number(value) / 255;
                    return channel <= 0.03928 ? channel / 12.92 :
                        Math.pow((channel + 0.055) / 1.055, 2.4);
                });
                return rgb[0] * 0.2126 + rgb[1] * 0.7152 + rgb[2] * 0.0722;
            };
            const contrast = (first, second) => {
                const values = [luminance(first), luminance(second)].sort((a, b) => b - a);
                return (values[0] + 0.05) / (values[1] + 0.05);
            };
            const cardContrast = ['midnight', 'ember'].map(theme => {
                modern_ui_set_theme(theme, false);
                const card = page.querySelector('.ui-usage-summary-card');
                const label = card.querySelector('span');
                const cardStyle = getComputedStyle(card);
                const labelStyle = getComputedStyle(label);
                return {
                    theme,
                    value: Number(contrast(cardStyle.color, cardStyle.backgroundColor).toFixed(2)),
                    label: Number(contrast(labelStyle.color, cardStyle.backgroundColor).toFixed(2))
                };
            });
            modern_ui_set_theme('midnight', false);
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                summaryCards: page.querySelectorAll('.ui-usage-summary-card').length,
                cardContrast,
                monthBars: page.querySelectorAll('#id-usage-month .ui-usage-bar-row').length,
                geoBars: page.querySelectorAll('#id-usage-geo .ui-usage-bar-row').length,
                geoRows: page.querySelectorAll('#id-usage-geo tbody tr').length,
                geoText: page.querySelector('#id-usage-geo')?.textContent || '',
                heatmapCells: cells.length,
                heatmapRows: page.querySelectorAll('.ui-usage-heatmap-row').length,
                partialCells: page.querySelectorAll('.ui-usage-heat-cell.is-partial').length,
                unavailableCells: page.querySelectorAll('.ui-usage-heat-cell.is-unavailable').length,
                selectedMetric: metric.value,
                dayBars: page.querySelectorAll('#id-usage-day .ui-usage-bar-row').length,
                hourSummary: page.querySelectorAll('.ui-usage-hour-summary strong').length,
                overflowVisible: page.querySelector('.ui-usage-overflow')?.textContent || '',
                memoryText: Array.from(page.querySelectorAll('.ui-usage-summary-card'))
                    .find(card => card.textContent.includes('Analytics memory'))?.textContent || '',
                enabledControl: !!document.getElementById('id-usage-enabled'),
                deleteControl: Array.from(page.querySelectorAll('button'))
                    .some(button => button.textContent.includes('Delete all usage data')),
                rawIpVisible: page.textContent.includes('127.0.0.1') ||
                    page.textContent.includes('::1'),
                injectedMarkup: !!page.querySelector('#usage-xss-probe') ||
                    !!page.querySelector('#id-usage-recent script'),
                escapedMarkupVisible: page.querySelector('#id-usage-recent')?.textContent.includes(
                    '<img id=usage-xss-probe>') || false,
                fits: rect.left >= -1 && rect.right <= window.innerWidth + 1,
                horizontalHeatmapScroll:
                    document.getElementById('id-usage-heatmap').scrollWidth >=
                    document.getElementById('id-usage-heatmap').clientWidth
            };
        });
        await clickAdminNav('id-nav-update');
        await adminPage.waitForSelector('.id-update-primary-action', { state: 'attached' });
        const fixtureAction = adminPage.locator('.id-update-primary-action');
        const fixtureProgress = adminPage.locator('.id-update-progress');
        await adminPage.evaluate(() => update_channel_cb('adm.update_channel', 0, false));
        const fixtureCheckStart = {
            label: (await fixtureAction.textContent()).trim(),
            disabled: await fixtureAction.isDisabled(),
            progressVisible: await fixtureProgress.evaluate(element =>
                getComputedStyle(element).display !== 'none')
        };
        await adminPage.waitForFunction(() => {
            const action = document.querySelector('.id-update-primary-action');
            return action.textContent.trim() === 'Install' && !action.disabled;
        }, null, { timeout: 30000 });
        const fixtureCheck = {
            label: (await fixtureAction.textContent()).trim(),
            release: await adminPage.locator('.id-update-result').textContent()
        };
        await adminPage.evaluate(() => update_primary_cb());
        const fixtureInstallStart = {
            label: (await fixtureAction.textContent()).trim(),
            disabled: await fixtureAction.isDisabled(),
            progressVisible: await fixtureProgress.evaluate(element =>
                getComputedStyle(element).display !== 'none')
        };
        await adminPage.waitForFunction(() =>
            document.querySelector('.id-update-primary-action').textContent.trim() === 'Restart',
            null, { timeout: 30000 });
        const fixtureInstall = {
            label: (await fixtureAction.textContent()).trim(),
            result: await adminPage.locator('.id-update-result').textContent()
        };
        const updateFixtureRoot = process.env.WEBSDR_UPDATE_ROOT;
        const updateFixtureUrl = process.env.WEBSDR_UPDATE_FIXTURE_URL;
        if (!updateFixtureRoot || !updateFixtureUrl)
            throw new Error('missing native update fixture configuration');
        const fixtureUpdate = {
            checkStart: fixtureCheckStart,
            check: fixtureCheck,
            installStart: fixtureInstallStart,
            install: fixtureInstall,
            stats: await fetchJson(new URL('stats', updateFixtureUrl).href),
            releaseMarker: fs.readFileSync(path.join(updateFixtureRoot, 'release-marker.txt'), 'utf8'),
            configSentinel: fs.readFileSync(path.join(updateFixtureRoot, 'config', 'sentinel.conf'), 'utf8'),
            archiveCached: fs.existsSync(path.join(updateFixtureRoot, 'update', 'fixture-release.zip'))
        };
        const adminUpgrade = await adminPage.evaluate(() => {
            const primaryAction = w3_el('id-update-primary-action');
            update_primary_set('check');
            update_progress(false);
            w3_hide('id-update-result-row');
            const primaryRect = primaryAction.getBoundingClientRect();
            const headerRect = document.querySelector('.ui-admin-update-header').getBoundingClientRect();
            const primaryInitial = {
                label: primaryAction.textContent.trim(),
                disabled: primaryAction.disabled,
                rightAligned: Math.abs(primaryRect.right - headerRect.right) <= 1
            };
            update_primary_set('checking');
            update_progress(true, 'Checking for updates');
            const primaryChecking = {
                label: primaryAction.textContent.trim(),
                disabled: primaryAction.disabled,
                progressVisible: getComputedStyle(
                    w3_el('id-update-progress')).display !== 'none',
                progressHeight: parseFloat(getComputedStyle(
                    w3_el('id-update-progress').querySelector('div')).height)
            };
            const updateCheckTime = Math.floor(Date.now() / 1000) - 5 * 60;
            update_status_cb({
                fail_reason: 0,
                vmaj: 2026,
                vmin: 101,
                pmaj: 2026,
                pmin: 919,
                release_date: '2026-09-19',
                release_filename: 'newest.zip',
                release_changes: 'Newest release',
                restart_required: false,
                last_check: updateCheckTime
            });
            const primaryInstall = {
                label: primaryAction.textContent.trim(),
                disabled: primaryAction.disabled,
                result: w3_el('id-update-result').textContent,
                progressVisible: getComputedStyle(
                    w3_el('id-update-progress')).display !== 'none',
                lastCheck: w3_el('id-update-last-check').textContent,
                relativeDays: update_last_check_age(updateCheckTime - 2 * 24 * 60 * 60,
                    updateCheckTime * 1000)
            };
            update_status_cb({ message: 'Downloading release package (primary attempt 1 of 3)' });
            const transferLabel = w3_el('id-update-result-progress').textContent;
            update_status_cb({ restart_required: true });
            const primaryRestart = primaryAction.textContent.trim();
            update_status_cb({
                fail_reason: 0,
                vmaj: 2026,
                vmin: 920,
                pmaj: 2026,
                pmin: 919,
                release_date: '2026-09-19',
                restart_required: false
            });
            const noNewUpdates = {
                label: primaryAction.textContent.trim(),
                resultHidden: getComputedStyle(w3_el('id-update-result-row')).display === 'none',
                message: w3_el('id-msg-update').textContent
            };
            const picker = w3_el('id-release-picker');
            const previousHidden = getComputedStyle(picker).display === 'none';
            update_previous_releases_cb();
            update_release_list_cb({
                local: ['newest.zip', 'second.zip', 'third.zip', 'fourth.zip'],
                releases: {
                    '20260919': {
                        date: '2026-09-19',
                        changes: ['Newest release'],
                        downloads: [{ filename: 'newest.zip' }]
                    },
                    '20260917': {
                        date: '2026-09-17',
                        changes: ['Second release'],
                        downloads: [{ filename: 'second.zip' }]
                    },
                    '20260609': {
                        date: '2026-06-09',
                        changes: ['Third release'],
                        downloads: [{ filename: 'third.zip' }]
                    },
                    '20260205': {
                        date: '2026-02-05',
                        changes: ['Fourth release'],
                        downloads: [{ filename: 'fourth.zip' }]
                    },
                    '20250101': {
                        date: '2025-01-01',
                        changes: ['Remote-only release'],
                        downloads: [{ filename: 'remote.zip' }]
                    }
                }
            });
            const releaseList = w3_el('id-update-release-list');
            const releaseDetails = w3_el('id-update-release-details');
            const releaseActions = w3_el('id-update-release-actions');
            const releaseCards = Array.from(releaseList?.querySelectorAll('.ui-admin-release-card') || []);
            const selectedRelease = releaseList?.querySelector('.ui-admin-release-card.is-selected');
            const listRect = releaseList.getBoundingClientRect();
            const detailsRect = releaseDetails.getBoundingClientRect();
            const actionsRect = releaseActions.getBoundingClientRect();
            return {
                primary: {
                    initial: primaryInitial,
                    checking: primaryChecking,
                    install: primaryInstall,
                    restart: primaryRestart,
                    noNewUpdates,
                    transferLabel
                },
                previous: {
                    hidden: previousHidden,
                    expanded: getComputedStyle(picker).display !== 'none'
                },
                releasePicker: {
                    cards: releaseCards.length,
                    nativeListbox: !!w3_el('id-update-release-select'),
                    scrollable: releaseList?.scrollHeight > releaseList?.clientHeight,
                    selected: selectedRelease?.dataset.releaseId,
                    details: w3_el('id-update-release-details')?.textContent,
                    local: document.querySelector('.ui-admin-release-local')?.textContent,
                    desktopFlow: listRect.left < detailsRect.left &&
                        Math.abs(listRect.top - detailsRect.top) <= 1,
                    separateActions: !releaseDetails.contains(
                        w3_el('id-update-release-install')) &&
                        actionsRect.top >= Math.max(listRect.bottom, detailsRect.bottom)
                }
            };
        });
        await adminPage.setViewportSize({ width: 390, height: 844 });
        const adminUpgradeMobile = await adminPage.evaluate(() => {
            const listRect = w3_el('id-update-release-list').getBoundingClientRect();
            const detailsRect = w3_el('id-update-release-details').getBoundingClientRect();
            return {
                verticalFlow: detailsRect.top >= listRect.bottom &&
                    Math.abs(listRect.left - detailsRect.left) <= 1,
                documentOverflow: document.documentElement.scrollWidth > window.innerWidth + 1
            };
        });
        await adminPage.setViewportSize({ width: 1440, height: 1000 });
        await clickAdminNav('id-nav-control');
        await adminPage.waitForTimeout(100);
        const adminControl = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-control');
            const rect = page.getBoundingClientRect();
            const buttons = Array.from(page.querySelectorAll('button'));
            const buttonSection = label => {
                const button = buttons.find(button => button.textContent.trim() === label);
                return button?.closest('.ui-admin-section')?.querySelector('h3')?.textContent;
            };
            const lifecycleButtons = buttons.filter(button =>
                ['Restart server', 'Reboot device'].includes(button.textContent.trim()));
            const fieldSection = title => Array.from(
                page.querySelectorAll('.ui-admin-control-field-title'))
                .find(field => field.textContent === title)
                ?.closest('.ui-admin-section')?.querySelector('h3')?.textContent;
            const savedAirband = adm.airband;
            adm.airband = true;
            airband_adc_clock_status();
            const radioSection = Array.from(page.querySelectorAll('.ui-admin-section'))
                .find(section => section.querySelector('h3')?.textContent === 'Radio configuration');
            const radioFields = Array.from(radioSection.querySelectorAll('.ui-admin-control-field'))
                .filter(field => getComputedStyle(field).display !== 'none');
            const radioWidths = radioFields.map(field => field.getBoundingClientRect().width);
            adm.airband = savedAirband;
            airband_adc_clock_status();
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                fits: rect.left >= -1 && rect.right <= window.innerWidth + 1,
                actionSections: {
                    restart: buttonSection('Restart server'),
                    reboot: buttonSection('Reboot device'),
                    kick: buttonSection('Kick all users'),
                    measure: buttonSection('Measure SNR now')
                },
                radioConfiguration: {
                    bandModeSection: fieldSection('User-selectable band mode'),
                    airbandClockSection: fieldSection('Airband ADC clock'),
                    visibleFields: radioFields.length,
                    aligned: Math.max(...radioWidths) - Math.min(...radioWidths) <= 1
                },
                lifecycleAligned: lifecycleButtons.length === 2 &&
                    Math.abs(lifecycleButtons[0].getBoundingClientRect().width -
                        lifecycleButtons[1].getBoundingClientRect().width) <= 1 &&
                    Math.abs(lifecycleButtons[0].getBoundingClientRect().top -
                        lifecycleButtons[1].getBoundingClientRect().top) <= 1
            };
        });
        await clickAdminNav('id-nav-connect');
        await adminPage.waitForTimeout(100);
        const adminConnect = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-connect');
            const selector = page.querySelector('.id-admin-nav-dom');
            const dynamicDns = Array.from(page.querySelectorAll('.ui-admin-section'))
                .find(section => section.querySelector('h3')?.textContent === 'Dynamic DNS');
            const ducFields = dynamicDns.querySelector('.ui-admin-duc-fields');
            const ducControls = dynamicDns.querySelector('.ui-admin-duc-controls');
            const startButton = Array.from(dynamicDns.querySelectorAll('button'))
                .find(button => button.textContent.trim() === 'Start or restart DUC');
            const sourceRows = ['id-connect-duc-dom', 'id-connect-rev-dom', 'id-connect-pub-ip']
                .map(className => {
                    const row = page.querySelector('.'+ className);
                    const value = row.querySelector('.ui-admin-connect-value');
                    const style = getComputedStyle(value);
                    return {
                        display: getComputedStyle(row).display,
                        columns: getComputedStyle(row).gridTemplateColumns,
                        missing: value.classList.contains('ui-admin-connect-value-missing'),
                        emptyText: value.textContent.trim().startsWith('('),
                        background: style.backgroundColor,
                        border: style.borderColor,
                        color: style.color
                    };
                });
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                selectorDisplay: getComputedStyle(selector).display,
                selectorPosition: getComputedStyle(selector).position,
                sourceRows,
                dynamicDns: {
                    groups: dynamicDns.querySelectorAll('.ui-admin-duc-group').length,
                    fieldColumns: getComputedStyle(ducFields).gridTemplateColumns,
                    controlColumns: getComputedStyle(ducControls).gridTemplateColumns,
                    hostWide: getComputedStyle(
                        dynamicDns.querySelector('.ui-admin-duc-host')).gridColumnEnd === '-1',
                    actionButton: !!startButton,
                    status: !!dynamicDns.querySelector('.ui-admin-duc-status .id-net-duc-status')
                }
            };
        });
        await clickAdminNav('id-nav-config');
        await adminPage.waitForTimeout(100);
        const adminConfig = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-config');
            const section = title => Array.from(page.querySelectorAll('.ui-admin-section'))
                .find(item => item.querySelector('h3')?.textContent === title);
            const clockingRows = Array.from(section('Clocking')
                ?.querySelector('.ui-admin-section-body > div')?.children || []);
            const clockingWidths = clockingRows.map(row => row.getBoundingClientRect().width);
            const adcDescriptions = Array.from(section('ADC behavior')
                ?.querySelectorAll('.ui-admin-section-body > .w3-row > .w3-col .w3-text-black') || []);
            const slider = className => {
                const input = Array.from(page.getElementsByClassName(className))
                    .find(element => element.type === 'range');
                return input && {
                    type: input.type,
                    min: input.min,
                    max: input.max,
                    step: input.step
                };
            };
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                fields: page.querySelectorAll('.ui-field').length,
                clockingLabels: clockingRows.map(row =>
                    row.querySelector('.ui-label')?.textContent),
                clockingLayout: {
                    rows: clockingRows.length,
                    display: clockingRows.map(row => getComputedStyle(row).display),
                    columns: clockingRows.map(row => getComputedStyle(row).gridTemplateColumns),
                    aligned: Math.max(...clockingWidths) - Math.min(...clockingWidths) <= 1,
                    descriptions: clockingRows.filter(row =>
                        row.querySelector('.ui-admin-config-option-desc')).length
                },
                clockingOverlap: clockingRows.some((row, index) =>
                    index && row.getBoundingClientRect().top <
                        clockingRows[index - 1].getBoundingClientRect().bottom),
                adcCentered: adcDescriptions.length === 3 &&
                    adcDescriptions.every(row => getComputedStyle(row).textAlign === 'center'),
                sliders: {
                    waterfallFloor: slider('id-init.floor_dB'),
                    waterfallCeil: slider('id-init.ceil_dB'),
                    zoom: slider('id-init.zoom'),
                    sMeter: slider('id-S_meter_cal'),
                    waterfall: slider('id-waterfall_cal'),
                    identLength: slider('id-ident_len')
                }
            };
        });
        await clickAdminNav('id-nav-webpage');
        await adminPage.waitForTimeout(100);
        const adminWebpage = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-webpage');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                previews: page.querySelectorAll(
                    '.id-webpage-title-preview, .id-webpage-owner-info-preview, .id-webpage-status-preview'
                ).length
            };
        });
        await clickAdminNav('id-nav-sdr_hu');
        await adminPage.waitForTimeout(100);
        const adminPublic = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-public');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                registrationStatus: !!page.querySelector('.id-kiwisdr_com-reg-status')
            };
        });
        await clickAdminNav('id-nav-dx');
        await adminPage.waitForTimeout(700);
        const adminDX = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-dx');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                labelList: !!page.querySelector('.id-dx-list'),
                searchFields: ['id-dx.o.search_f', 'id-dx.o.search_i', 'id-dx.o.search_n']
                    .filter(id => document.getElementById(id)).length
            };
        });
        await clickAdminNav('id-nav-update');
        await adminPage.waitForTimeout(100);
        const adminUpdate = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-update');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                statusHook: !!page.querySelector('.id-msg-update'),
                primaryAction: page.querySelector('.id-update-primary-action')?.textContent.trim(),
                previousAction: page.querySelector('.id-update-previous-releases')?.textContent.trim(),
                updateSettings: {
                    alphaStable: page.textContent.includes('Release channel') &&
                        page.textContent.includes('Alpha') && page.textContent.includes('Stable'),
                    autoDownload: page.textContent.includes('Auto download'),
                    autoInstall: page.textContent.includes('Auto install')
                }
            };
        });
        await clickAdminNav('id-nav-network');
        await adminPage.waitForTimeout(100);
        const adminNetwork = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-network');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                networkStatus: !!page.querySelector('.id-net-config'),
                blacklistStatus: !!page.querySelector('.id-ip-blacklist-status')
            };
        });
        const adminScrollDesktop = await adminPage.evaluate(() => {
            const container = document.querySelector('.id-kiwi-container[data-type="admin"]');
            const header = document.querySelector('.id-admin-header-container');
            container.scrollTop = 0;
            const headerTop = header.getBoundingClientRect().top;
            const maxScroll = container.scrollHeight - container.clientHeight;
            container.scrollTop = maxScroll;
            const result = {
                overflowY: getComputedStyle(container).overflowY,
                viewportHeight: container.clientHeight === window.innerHeight,
                maxScroll,
                reachedBottom: Math.abs(container.scrollTop - maxScroll) <= 1,
                stickyHeader: Math.abs(header.getBoundingClientRect().top - headerTop) <= 1
            };
            container.scrollTop = 0;
            return result;
        });
        await clickAdminNav('id-nav-gps');
        await adminPage.waitForTimeout(200);
        const adminGPS = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-gps');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                infoTable: !!page.querySelector('.id-gps-info'),
                channelTable: !!page.querySelector('.id-gps-ch'),
                skyCanvas: !!page.querySelector('#id-gps-azel-canvas')
            };
        });
        await clickAdminNav('id-nav-log');
        await adminPage.waitForTimeout(100);
        const adminLog = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-log');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                output: !!page.querySelector('.id-log-msg'),
                actions: Array.from(page.querySelectorAll('button'))
                    .filter(button => ['Log state', 'Log IP blacklist', 'Clear Histogram']
                        .includes(button.textContent.trim())).length
            };
        });
        await clickAdminNav('id-nav-console');
        await adminPage.waitForTimeout(100);
        const adminConsole = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-console');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                terminal: !!document.getElementById('id-console-msgs'),
                connect: Array.from(page.querySelectorAll('button'))
                    .some(button => button.textContent.trim() === 'Connect')
            };
        });
        const consoleOpenOrder = await adminPage.evaluate(() => {
            const originalSend = window.ext_send;
            const originalOpen = admin.console_open;
            const sent = [];
            window.ext_send = command => sent.push(command);
            admin.console_open = false;
            console_connect_cb();
            admin.console_open = originalOpen;
            window.ext_send = originalSend;
            return {
                sizeFirst: sent[0]?.startsWith('SET console_rows_cols=') &&
                    sent[1] === 'SET console_open',
                sent
            };
        });
        const consoleANSI = await adminPage.evaluate(() => {
            const output = document.createElement('div');
            output.id = 'id-test-console-output';
            const scroll = document.createElement('div');
            scroll.id = 'id-test-console-scroll';
            scroll.appendChild(output);
            document.body.appendChild(scroll);

            const state = { rows: 6, cols: 20, show_cursor: false };
            const send = text => {
                state.s = encodeURIComponent(text);
                kiwi_output_msg(output.id, scroll.id, state);
            };
            send('\x1b[?1049h\x1b[2J');
            send('\x1b[3;3HA\x1b[4CB');
            const countedMove = state.screen[3][3] === 'A' && state.screen[3][8] === 'B';
            send('\x1b[2;2HX\x1b7\x1b[5;10H\x1b8Y');
            const savedCursor = state.screen[2][2] === 'X' && state.screen[2][3] === 'Y';
            send('\x1b[2;5r\x1b[4;6H\x1b[1TZ');
            const scrollCursor = state.r === 4 && state.screen[4][6] === 'Z' &&
                state.nrows === state.rows && state.margin_bottom === 5;
            send('\x1b[r');
            const marginReset = !state.margin_set && state.margin_top === 1 &&
                state.margin_bottom === state.rows;
            send('\x1b[1;1H\x1b]0;ignored title\x07Q');
            const oscConsumed = state.screen[1][1] === 'Q' && state.screen[1][2] === ' ';
            send('\x1b[3;12Htail\r\x1b[4drow');
            const carriageReturn = state.screen[4].slice(1, 4).join('') === 'row';

            scroll.remove();
            return {
                countedMove, savedCursor, scrollCursor, marginReset, oscConsumed, carriageReturn
            };
        });
        await clickAdminNav('id-nav-extensions');
        await adminPage.waitForTimeout(100);
        const adminExtensions = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-extensions');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                navigation: !!page.querySelector('.id-extensions-nav'),
                configuration: !!page.querySelector('.id-extensions-config')
            };
        });
        await clickAdminNav('id-nav-security');
        await adminPage.waitForTimeout(100);
        const adminSecurity = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-security');
            return {
                heading: page.querySelector('.ui-admin-page-header h2')?.textContent,
                sections: Array.from(page.querySelectorAll('.ui-admin-section > header h3'),
                    heading => heading.textContent),
                userPassword: !!document.getElementById('id-adm.user_password'),
                adminPassword: !!document.getElementById('id-adm.admin_password')
            };
        });
        await adminPage.setViewportSize({ width: 390, height: 844 });
        await clickAdminNav('id-nav-control');
        await adminPage.waitForTimeout(100);
        const adminControlMobile = await adminPage.evaluate(() => {
            const page = document.querySelector('.ui-admin-control');
            const actionCards = Array.from(page.querySelectorAll('.ui-admin-control-action'));
            const rect = page.getBoundingClientRect();
            return {
                fits: rect.left >= -1 && rect.right <= window.innerWidth + 1,
                actionColumns: actionCards.length >= 2 &&
                    Math.abs(actionCards[0].getBoundingClientRect().left -
                        actionCards[1].getBoundingClientRect().left) <= 1,
                fullWidthButtons: actionCards.every(card => {
                    const button = card.querySelector('button');
                    const style = getComputedStyle(card);
                    const innerWidth = card.getBoundingClientRect().width -
                        parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
                    return Math.abs(button.getBoundingClientRect().width - innerWidth) <= 2;
                })
            };
        });
        await clickAdminNav('id-nav-extensions');
        await adminPage.waitForTimeout(100);
        const adminExtensionsMobile = await adminPage.evaluate(() => {
            const nav = document.querySelector('.id-extensions-nav');
            const config = document.querySelector('.id-extensions-config');
            const selected = nav?.querySelector('.w3int-cur-sel');
            const configRect = config?.getBoundingClientRect();
            return {
                navDisplay: nav? getComputedStyle(nav).display : '',
                navPosition: nav? getComputedStyle(nav).position : '',
                navScrollable: !!nav && nav.scrollWidth >= nav.clientWidth,
                selectedReadable: !!selected && selected.getBoundingClientRect().width >=
                    selected.scrollWidth - 1,
                configFits: !!configRect && configRect.left >= -1 &&
                    configRect.right <= window.innerWidth + 1
            };
        });
        const adminClassicMobile = await adminPage.evaluate(() => {
            modern_ui_set_theme('classic', false);
            document.getElementById('id-nav-control').click();
            const controlAction = document.querySelector('.ui-admin-control-action');
            const controlButton = controlAction.querySelector('button');
            const controlStyle = getComputedStyle(controlAction);
            const controlInnerWidth = controlAction.getBoundingClientRect().width -
                    parseFloat(controlStyle.paddingLeft) - parseFloat(controlStyle.paddingRight);
            const control = {
                    sectionColumns: getComputedStyle(document.querySelector(
                        '.ui-admin-control .ui-admin-section-grid')).gridTemplateColumns,
                    actionColumns: getComputedStyle(document.querySelector(
                        '.ui-admin-control-actions')).gridTemplateColumns,
                    actionDirection: controlStyle.flexDirection,
                    fullWidthButton: Math.abs(
                        controlButton.getBoundingClientRect().width - controlInnerWidth) <= 2
            };
            const picker = document.querySelector('.ui-theme-picker').getBoundingClientRect();
            document.getElementById('id-nav-connect').click();
            const connect = {
                    selectorDisplay: getComputedStyle(document.querySelector(
                        '.ui-admin-connect .id-admin-nav-dom')).display,
                    fieldColumns: getComputedStyle(document.querySelector(
                        '.ui-admin-duc-fields')).gridTemplateColumns,
                    controlColumns: getComputedStyle(document.querySelector(
                        '.ui-admin-duc-controls')).gridTemplateColumns,
                    actionDirection: getComputedStyle(document.querySelector(
                        '.ui-admin-duc-action')).flexDirection,
                    documentOverflow: document.documentElement.scrollWidth > window.innerWidth + 2
            };
            modern_ui_set_theme('midnight', false);
            return {
                control,
                connect,
                picker: {
                    top: picker.top,
                    right: picker.right,
                    viewportWidth: window.innerWidth
                }
            };
        });
        await clickAdminNav('id-nav-network');
        await adminPage.waitForTimeout(100);
        const adminScrollMobile = await adminPage.evaluate(() => {
            const container = document.querySelector('.id-kiwi-container[data-type="admin"]');
            const maxScroll = container.scrollHeight - container.clientHeight;
            container.scrollTop = maxScroll;
            const result = {
                overflowY: getComputedStyle(container).overflowY,
                viewportHeight: container.clientHeight === window.innerHeight,
                maxScroll,
                reachedBottom: Math.abs(container.scrollTop - maxScroll) <= 1
            };
            container.scrollTop = 0;
            return result;
        });
        await adminPage.close();

        const usageRoot = path.resolve(
            process.env.WEBSDR_HARNESS_BUILD_DIR || path.resolve(__dirname, '../build-native'),
            'config/usage');
        const usageFiles = [];
        const collectUsageFiles = directory => {
            if (!fs.existsSync(directory))
                return;
            for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
                const filePath = path.join(directory, entry.name);
                if (entry.isDirectory())
                    collectUsageFiles(filePath);
                else
                    usageFiles.push(filePath);
            }
        };
        collectUsageFiles(usageRoot);
        const usageFileAudit = {
            count: usageFiles.length,
            rawIpFound: usageFiles.some(file => {
                const content = fs.readFileSync(file);
                return content.includes(Buffer.from('127.0.0.1')) ||
                    content.includes(Buffer.from('::1'));
            }),
            largest: usageFiles.reduce((largest, file) =>
                Math.max(largest, fs.statSync(file).size), 0)
        };

        if (!uiFoundation.selectPresent ||
            uiFoundation.storedTheme !== 'midnight' ||
            uiFoundation.receiverThemeParent !== 'id-rf-theme-actions' ||
            JSON.stringify(uiFoundation.themes) !== JSON.stringify([
                { theme: 'midnight', accent: '#58a6ff', mutedContrast: 6.84,
                    modern: true, classic: false, actionsDisplay: 'flex',
                    actionsJustify: 'flex-end' },
                { theme: 'ember', accent: '#e58a3a', mutedContrast: 6.3,
                    modern: true, classic: false, actionsDisplay: 'flex',
                    actionsJustify: 'flex-end' },
                { theme: 'cloud', accent: '#2196f3', mutedContrast: 6.6,
                    modern: true, classic: false, actionsDisplay: 'flex',
                    actionsJustify: 'flex-end' },
                { theme: 'classic', accent: '#2196f3', mutedContrast: 7.46,
                    modern: false, classic: true, actionsDisplay: 'flex',
                    actionsJustify: 'flex-end' }
            ]) ||
            JSON.stringify(uiFoundation.themeOptions) !==
                JSON.stringify(['midnight', 'ember', 'cloud', 'classic']) ||
            uiFoundation.semanticShell.header !== 'HEADER' ||
            uiFoundation.semanticShell.main !== 'MAIN' ||
            uiFoundation.semanticShell.panels !== 'ASIDE' ||
            uiFoundation.hooks.buttons < 1 ||
            uiFoundation.hooks.fields < 1 ||
            uiFoundation.hooks.panels < 4 ||
            uiFoundation.panelToggle.tagName !== 'BUTTON' ||
            uiFoundation.panelToggle.tabIndex !== 0 ||
            uiFoundation.panelToggle.label !== 'Hide panel' ||
            uiFoundation.typography.panel < 14 ||
            uiFoundation.typography.stationName > 12 ||
            uiFoundation.typography.rf < 14 ||
            uiFoundation.typography.wf < 14 ||
            uiFoundation.typography.audio < 14 ||
            uiFoundation.typography.agc < 14 ||
            uiFoundation.typography.select < 14 ||
            uiFoundation.typography.selectBackground === 'rgb(255, 255, 255)' ||
            uiFoundation.controlAlignment.some(control =>
                control.missing ||
                control.display !== 'flex' && control.display !== 'inline-flex' ||
                control.alignItems !== 'center' ||
                control.justifyContent !== 'center') ||
            !uiFoundation.step9_10.enabled ||
            uiFoundation.step9_10.before === uiFoundation.step9_10.after ||
            uiFoundation.step9_10.steps.join(',') !== '9000,10000' ||
            uiFoundation.tabPalette.labels[2] !== 'AUD' ||
            uiFoundation.tabPalette.legacyColorClasses ||
            uiFoundation.tabPalette.inactiveBackgrounds.length !== 1 ||
            uiFoundation.tabPalette.selectedBackground === uiFoundation.tabPalette.inactiveBackgrounds[0])
            throw new Error(`invalid modern UI foundation: ${JSON.stringify(uiFoundation)}`);
        if (!drmThemeAssets.lightCloseIcon || !drmThemeAssets.themedScheduleMarker)
            throw new Error(`invalid DRM theme assets: ${JSON.stringify(drmThemeAssets)}`);
        if (extensionFocus.closeSemantics.tagName !== 'BUTTON' ||
            extensionFocus.closeSemantics.tabIndex !== 0 ||
            !extensionFocus.closeSemantics.focused ||
            extensionFocus.closeSemantics.wrapperOverflow !== 'hidden' ||
            !extensionFocus.closeSemantics.wrapperFits ||
            !extensionFocus.expected ||
            extensionFocus.restored !== extensionFocus.expected ||
            extensionFocus.displayed ||
            extensionFocus.panelVisible !== 'hidden')
            throw new Error(`invalid extension focus handling: ${JSON.stringify(extensionFocus)}`);
        if (uiFoundation.extensionOutput.background === 'rgb(230, 230, 230)' ||
            uiFoundation.extensionOutput.background === 'rgb(255, 255, 255)' ||
            uiFoundation.extensionOutput.color === 'rgb(0, 0, 0)' ||
            uiFoundation.extensionOutput.fontSize < 13)
            throw new Error(`invalid extension output theme: ${JSON.stringify(uiFoundation.extensionOutput)}`);
        if (cloudControlContrast.buttons.some(button =>
            button.color === button.background || button.contrast < 4.5))
            throw new Error(
                `invalid Cloud control-button contrast: ${JSON.stringify(cloudControlContrast)}`);
        if (cloudControlContrast.zoomIcons.length !== 2 ||
            cloudControlContrast.zoomIcons.some(icon =>
                icon.background === 'rgba(0, 0, 0, 0)' ||
                icon.imageBackground !== 'rgba(0, 0, 0, 0)' ||
                icon.imageFilter !== 'invert(1)'))
            throw new Error(
                `invalid Cloud zoom-control contrast: ${JSON.stringify(cloudControlContrast)}`);
        if (cloudControlContrast.stepButtons.length !== 2 ||
            cloudControlContrast.stepButtons.some(button =>
                button.background === 'rgba(0, 0, 0, 0)' ||
                button.imageBackground !== 'rgba(0, 0, 0, 0)' ||
                button.imageFilter !== 'invert(1)') ||
            cloudControlContrast.stepButtons[0].width !== 20 ||
            cloudControlContrast.stepButtons[0].height !== 20 ||
            cloudControlContrast.stepButtons[1].width !== 16 ||
            cloudControlContrast.stepButtons[1].height !== 16)
            throw new Error(
                `invalid Cloud frequency-step contrast: ${JSON.stringify(cloudControlContrast)}`);
        if (cloudControlContrast.visibilityArrow.buttonBackground ===
                'rgba(0, 0, 0, 0)' ||
            cloudControlContrast.visibilityArrow.imageBackground !==
                'rgba(0, 0, 0, 0)' ||
            cloudControlContrast.visibilityArrow.imageFilter !== 'invert(1)' ||
            cloudControlContrast.visibilityArrow.width !== 24 ||
            cloudControlContrast.visibilityArrow.height !== 25)
            throw new Error(
                `invalid Cloud visibility-arrow contrast: ${JSON.stringify(cloudControlContrast)}`);
        if (extensionThemes.length !== 4 ||
            new Set(extensionThemes.map(theme => theme.panel.background)).size !== 3 ||
            new Set(extensionThemes.map(theme => theme.titleColor)).size !== 3 ||
            new Set(extensionThemes.map(theme => theme.button.color)).size !== 3 ||
            new Set(extensionThemes.map(theme => theme.button.background)).size !== 3 ||
            extensionThemes.some(theme =>
                theme.theme === '' ||
                theme.panel.background === 'rgba(0, 0, 0, 0)' ||
                theme.panel.color === theme.panel.background ||
                theme.panel.border === theme.panel.background ||
                theme.contentColor === theme.panel.background ||
                theme.titleColor === theme.panel.background ||
                theme.rowBorder === 'rgba(0, 0, 0, 0)' ||
                theme.button.color === theme.button.background ||
                theme.input.color === theme.input.background ||
                theme.notice.color === theme.notice.background ||
                theme.warning.color === theme.warning.background ||
                theme.legacy.color === theme.legacy.background ||
                theme.output.color === theme.output.background ||
                theme.rows[0] === theme.rows[1]))
            throw new Error(`invalid extension themes: ${JSON.stringify(extensionThemes)}`);
        if (uiFoundation.controlFocus.some(control =>
            !control.exists || !control.focused || control.height > 34 ||
            parseFloat(control.outlineOffset) >= 0))
            throw new Error(`invalid control focus geometry: ${JSON.stringify(uiFoundation.controlFocus)}`);
        if (uiFoundation.wfFilterRow.display !== 'grid' ||
            uiFoundation.wfFilterRow.gaps.some(gap => gap < 3.5) ||
            uiFoundation.wfFilterRow.selectFontSize > 12 ||
            uiFoundation.wfFilterRow.selectPaddingLeft > 6 ||
            uiFoundation.wfFilterRow.selectPaddingRight > 18)
            throw new Error(`invalid WF filter row: ${JSON.stringify(uiFoundation.wfFilterRow)}`);
        if (uiFoundation.optbarScroll.some(tab =>
            tab.overflowY !== 'auto' ||
            tab.scrollbarWidth !== 'thin' ||
            !tab.scrollbarGutter.startsWith('stable')) ||
            ['wf', 'audio', 'agc'].some(name => {
                const tab = uiFoundation.optbarScroll.find(entry => entry.tab === name);
                return !tab?.scrollable || !tab.scrolled;
            }))
            throw new Error(`invalid optbar scrolling: ${JSON.stringify(uiFoundation.optbarScroll)}`);
        for (const layout of receiverResponsive) {
            if (!layout.modern || layout.theme !== 'midnight' ||
                layout.documentOverflow ||
                layout.themeControlOverlap ||
                !layout.controlFitsViewport ||
                !layout.audioControls.deEmphasis ||
                !layout.audioControls.compression ||
                layout.audioControls.overlap ||
                layout.audioControls.gap < 4 ||
                !layout.compactThemeReadable ||
                !layout.main || !layout.waterfall ||
                layout.waterfall.width <= 0 || layout.waterfall.width > layout.viewport[0] + 2)
                throw new Error(`invalid responsive receiver layout: ${JSON.stringify(layout)}`);
        }
        if (faxMobile.documentOverflow ||
            faxMobile.panel.left < -1 || faxMobile.panel.right > 391 ||
            faxMobile.close.left < -1 || faxMobile.close.right > 391 ||
            faxMobile.close.width < 32 || faxMobile.close.height < 32 ||
            faxMobile.collapse.left < -1 || faxMobile.collapse.right > 391 ||
            faxMobile.collapse.width < 44 || faxMobile.collapse.height < 34 ||
            faxMobile.data.left < -1 || faxMobile.data.right > 391 ||
            faxMobile.data.overflowX !== 'auto' ||
            faxMobile.actionOverflow)
            throw new Error(`invalid FAX mobile layout: ${JSON.stringify(faxMobile)}`);
        if (!faxCollapse.collapsed.state ||
            !faxCollapse.collapsed.displayed ||
            faxCollapse.collapsed.currentExtension !== 'FAX' ||
            faxCollapse.collapsed.panelTop < faxCollapse.collapsed.viewportHeight - 12 ||
            !faxCollapse.collapsed.handleVisible ||
            faxCollapse.collapsed.ariaExpanded !== 'false' ||
            faxCollapse.collapsed.label !== 'Expand extension controls' ||
            !faxCollapse.collapsed.icon.includes('fa-chevron-up') ||
            faxCollapse.collapsed.contentVisibility !== 'hidden' ||
            !faxCollapse.collapsed.dataVisible ||
            faxCollapse.resetOnOpen.state ||
            faxCollapse.resetOnOpen.collapsed ||
            faxCollapse.resetOnOpen.ariaExpanded !== 'true' ||
            faxCollapse.resetOnOpen.label !== 'Collapse extension controls' ||
            !faxCollapse.resetOnOpen.icon.includes('fa-chevron-down') ||
            faxCollapse.resetOnOpen.contentVisibility !== 'visible')
            throw new Error(`invalid FAX panel collapse: ${JSON.stringify(faxCollapse)}`);
        const invalidExtensionCollapse = layout => {
            const collapse = layout.collapse;
            const validHandle = snapshot =>
                snapshot.handle.left >= -1 &&
                snapshot.handle.right <= layout.viewport[0] + 1 &&
                snapshot.handle.top >= -1 &&
                snapshot.handle.bottom <= snapshot.viewportHeight + 1 &&
                snapshot.handle.width >= 44 &&
                snapshot.handle.height >= 34 &&
                snapshot.handle.tappable;
            return collapse.expanded.collapsed ||
                collapse.expanded.state ||
                !collapse.expanded.displayed ||
                collapse.expanded.currentExtension !== layout.name ||
                collapse.expanded.ariaExpanded !== 'true' ||
                collapse.expanded.label !== 'Collapse extension controls' ||
                !collapse.expanded.icon.includes('fa-chevron-down') ||
                collapse.expanded.contentVisibility !== 'visible' ||
                collapse.expanded.contentAriaHidden !== 'false' ||
                !validHandle(collapse.expanded) ||
                !collapse.collapsed.collapsed ||
                !collapse.collapsed.state ||
                !collapse.collapsed.displayed ||
                collapse.collapsed.currentExtension !== layout.name ||
                collapse.collapsed.panelTop < collapse.collapsed.viewportHeight - 12 ||
                collapse.collapsed.ariaExpanded !== 'false' ||
                collapse.collapsed.label !== 'Expand extension controls' ||
                !collapse.collapsed.icon.includes('fa-chevron-up') ||
                collapse.collapsed.contentVisibility !== 'hidden' ||
                collapse.collapsed.contentAriaHidden !== 'true' ||
                collapse.collapsed.closeVisibility !== 'hidden' ||
                !validHandle(collapse.collapsed) ||
                collapse.restored.collapsed ||
                collapse.restored.state ||
                !collapse.restored.displayed ||
                collapse.restored.currentExtension !== layout.name ||
                collapse.restored.ariaExpanded !== 'true' ||
                collapse.restored.label !== 'Collapse extension controls' ||
                !collapse.restored.icon.includes('fa-chevron-down') ||
                collapse.restored.contentVisibility !== 'visible' ||
                collapse.restored.contentAriaHidden !== 'false' ||
                !validHandle(collapse.restored);
        };
        if (extensionLayouts.some(layout =>
            layout.overlaps.length ||
            layout.overflowingHooks.length ||
            invalidExtensionCollapse(layout) ||
            (layout.helpButton.visible &&
                (layout.helpButton.height !== 30 ||
                    layout.helpButton.minHeight !== '30px' ||
                    layout.helpButton.paddingTop !== '0px' ||
                    layout.helpButton.paddingBottom !== '0px')) ||
            !layout.drmRegistered ||
            !layout.drmRendered ||
            !layout.fstwRendered ||
            !layout.fstwSelected ||
            !layout.aisMapRendered ||
            !layout.aisVesselRendered))
            throw new Error(
                `invalid extension control layout: ${JSON.stringify(extensionLayouts)}`);
        if (!drmPanelControls.collapsed.state ||
            !drmPanelControls.collapsed.displayed ||
            drmPanelControls.collapsed.currentExtension !== 'DRM' ||
            drmPanelControls.collapsed.panelVisible !== 'visible' ||
            drmPanelControls.closed.state ||
            drmPanelControls.closed.displayed ||
            drmPanelControls.closed.currentExtension !== null ||
            drmPanelControls.closed.panelVisible !== 'hidden')
            throw new Error(`invalid DRM panel controls: ${JSON.stringify(drmPanelControls)}`);
        if (dxDialogMobile.some(layout =>
            layout.documentOverflow ||
            layout.formOverflow ||
            layout.rowOverflow ||
            !layout.beginTappable ||
            !layout.endTappable ||
            layout.timeInputsOverlap))
            throw new Error(`invalid mobile DX dialog layout: ${JSON.stringify(dxDialogMobile)}`);
        const invalidPanelToggle = state =>
            state.hidden.shown ||
            state.hidden.panelLeft < state.hidden.viewportWidth - 10 ||
            state.hidden.hideDisplay !== 'none' ||
            state.hidden.showDisplay === 'none' ||
            !state.hidden.showVisible ||
            !state.shown.shown ||
            state.shown.left < -12 ||
            state.shown.right > state.shown.viewportWidth + 1 ||
            state.shown.hideDisplay === 'none' ||
            state.shown.showDisplay !== 'none';
        if (invalidPanelToggle(panelToggle.desktop) ||
            invalidPanelToggle(panelToggle.phone) ||
            panelToggle.readme.hidden.shown ||
            panelToggle.readme.hidden.panelRight > 10 ||
            panelToggle.readme.hidden.hideDisplay !== 'none' ||
            panelToggle.readme.hidden.showDisplay === 'none' ||
            !panelToggle.readme.hidden.showVisible ||
            !panelToggle.readme.shown.shown ||
            panelToggle.readme.shown.left < 0 ||
            panelToggle.readme.shown.right > panelToggle.readme.shown.viewportWidth + 1 ||
            panelToggle.readme.shown.hideDisplay === 'none' ||
            panelToggle.readme.shown.showDisplay !== 'none' ||
            !panelToggle.phone.shown.scaled ||
            !panelToggle.phone.shown.transform.startsWith('scale('))
            throw new Error(`invalid control panel toggle: ${JSON.stringify(panelToggle)}`);
        if (!adminFoundation.shell ||
            adminFoundation.nav !== 'tablist' ||
            adminFoundation.pages < 10 ||
            adminFoundation.title !== 'Administration' ||
            adminFoundation.theme !== 'midnight' ||
            adminFoundation.themeParent !== 'id-admin-theme-actions' ||
            adminFoundation.themePosition !== 'static' ||
            !adminFoundation.restartNotice.danger ||
            adminFoundation.restartNotice.fontSize < 14 ||
            adminFoundation.restartNotice.text !== 'Restart required for changes to take effect' ||
            adminFoundation.restartNotice.display !== 'flex' ||
            adminFoundation.restartNotice.alignItems !== 'center' ||
            adminFoundation.restartNotice.justifyContent !== 'center' ||
            !adminFoundation.keyboardNavigation.before ||
            adminFoundation.keyboardNavigation.before === adminFoundation.keyboardNavigation.after ||
            adminFoundation.keyboardNavigation.after !== adminFoundation.keyboardNavigation.focused)
            throw new Error(`invalid modern admin foundation: ${JSON.stringify(adminFoundation)}`);
        if (adminClassic.theme !== 'classic' ||
            !adminClassic.rootClassic ||
            adminClassic.rootModern ||
            !adminClassic.bodyClassic ||
            adminClassic.titleDisplay !== 'none' ||
            adminClassic.titlebarPosition !== 'static' ||
            adminClassic.pickerPosition !== 'static' ||
            adminClassic.pickerTop < 0 ||
            adminClassic.pickerTop > 8 ||
            adminClassic.pickerRight > 1432 ||
            adminClassic.pageBorderRadius !== '0px' ||
            adminClassic.pageBoxShadow !== 'none' ||
            !adminClassic.statusColumns.includes('px') ||
            !adminClassic.runtimeColumns.includes('px') ||
            adminClassic.cpuRowDisplay !== 'grid' ||
            adminClassic.histogramHeight < 90 ||
            !adminClassic.control.sectionColumns.includes('px') ||
            !adminClassic.control.actionColumns.includes('px') ||
            adminClassic.control.actionDisplay !== 'flex' ||
            !adminClassic.control.fieldColumns.includes('px') ||
            adminClassic.control.sessionDisplay !== 'flex' ||
            adminClassic.connect.selectorDisplay !== 'flex' ||
            !adminClassic.connect.fieldColumns.includes('px') ||
            !adminClassic.connect.controlColumns.includes('px') ||
            adminClassic.connect.actionDisplay !== 'flex' ||
            adminClassic.connect.statusDisplay !== 'grid' ||
            adminClassic.connect.proxyHeaderColor !== 'rgb(0, 105, 92)' ||
            adminClassic.connect.proxyHeaderBackground !== 'rgb(255, 255, 255)' ||
            adminClassic.connect.modernProxyHeaderColor !== 'rgb(88, 166, 255)' ||
            adminClassic.connect.modernProxyHeaderBackground !== 'rgb(23, 29, 37)')
            throw new Error(`invalid classic admin theme: ${JSON.stringify(adminClassic)}`);
        if (adminWarningThemes.warningCount < 7 ||
            adminWarningThemes.hardcodedYellow ||
            adminWarningThemes.legacyConnectColors ||
            adminWarningThemes.themes.length !== 4 ||
            adminWarningThemes.themes.some(theme =>
                theme.display !== 'flex' ||
                theme.alignItems !== 'flex-start' ||
                theme.background === 'rgba(0, 0, 0, 0)' ||
                theme.border === theme.background ||
                theme.color === theme.background ||
                theme.marker !== '"!"' ||
                theme.headingMargin !== '0px' ||
                theme.connectValueBackground === 'rgba(0, 0, 0, 0)' ||
                theme.connectValueBorder === theme.connectValueBackground ||
                theme.connectValueColor === theme.connectValueBackground ||
                theme.connectValueContrast < 4.5))
            throw new Error(`invalid admin warning themes: ${JSON.stringify(adminWarningThemes)}`);
        if (adminClassicMobile.control.sectionColumns !== '390px' ||
            adminClassicMobile.control.actionColumns !== '358px' ||
            adminClassicMobile.control.actionDirection !== 'column' ||
            !adminClassicMobile.control.fullWidthButton ||
            adminClassicMobile.connect.selectorDisplay !== 'flex' ||
            !adminClassicMobile.connect.fieldColumns.endsWith('px') ||
            adminClassicMobile.connect.fieldColumns.includes(' ') ||
            !adminClassicMobile.connect.controlColumns.endsWith('px') ||
            adminClassicMobile.connect.controlColumns.includes(' ') ||
            adminClassicMobile.connect.actionDirection !== 'column' ||
            adminClassicMobile.connect.documentOverflow ||
            adminClassicMobile.picker.top < 0 ||
            adminClassicMobile.picker.top > 8 ||
            adminClassicMobile.picker.right > adminClassicMobile.picker.viewportWidth - 8)
            throw new Error(
                `invalid classic mobile admin pages: ${JSON.stringify(adminClassicMobile)}`);
        if (adminStatus.heading !== 'System status' ||
            !['Receiver', 'Signal & timing', 'Runtime health', 'Nightly maintenance',
                'Admin client'].every(title => adminStatus.cardTitles.includes(title)) ||
            ['Compute', 'Traffic', 'Realtime diagnostics']
                .some(title => adminStatus.cardTitles.includes(title)) ||
            !adminStatus.twoColumnGrid ||
            !adminStatus.userSummary?.includes('receiver channels active') ||
            adminStatus.userRows < 1 ||
            adminStatus.usersBorder === 'none' ||
            adminStatus.runtime.panels !== 3 ||
            adminStatus.runtime.cpuRows < 1 ||
            adminStatus.runtime.trafficSegments !== 3 ||
            !adminStatus.runtime.sparkline ||
            adminStatus.runtime.counters !== 4 ||
            adminStatus.runtime.histograms !== 2 ||
            adminStatus.runtime.histogramBars < 32 ||
            !adminStatus.runtime.resetButton)
            throw new Error(`invalid modern admin status page: ${JSON.stringify(adminStatus)}`);
        if (adminUsage.heading !== 'Usage' ||
            adminUsage.summaryCards !== 8 ||
            adminUsage.cardContrast.some(theme => theme.value < 4.5 || theme.label < 4.5) ||
            adminUsage.monthBars < 1 ||
            adminUsage.geoBars < 1 ||
            adminUsage.geoRows < 1 ||
            !adminUsage.geoText.includes('Stress test') ||
            !adminUsage.geoText.includes('sessions') ||
            adminUsage.heatmapCells !== 168 ||
            adminUsage.heatmapRows !== 7 ||
            adminUsage.partialCells !== 1 ||
            adminUsage.selectedMetric !== 'unique' ||
            adminUsage.dayBars !== 24 ||
            adminUsage.hourSummary !== 4 ||
            !adminUsage.overflowVisible.includes('frequency') ||
            !adminUsage.memoryText.includes('KiB') ||
            !adminUsage.enabledControl ||
            !adminUsage.deleteControl ||
            adminUsage.rawIpVisible ||
            adminUsage.injectedMarkup ||
            !adminUsage.escapedMarkupVisible ||
            !adminUsage.fits ||
            !adminUsage.horizontalHeatmapScroll)
            throw new Error(`invalid admin usage page: ${JSON.stringify(adminUsage)}`);
        if (usageFileAudit.count < 4 ||
            usageFileAudit.rawIpFound ||
            usageFileAudit.largest > 1024 * 1024)
            throw new Error(`invalid usage report files: ${JSON.stringify(usageFileAudit)}`);
        if (fixtureUpdate.checkStart.label !== 'Checking for updates' ||
            !fixtureUpdate.checkStart.disabled ||
            !fixtureUpdate.checkStart.progressVisible ||
            fixtureUpdate.check.label !== 'Install' ||
            !fixtureUpdate.check.release.includes('Fixture release') ||
            fixtureUpdate.installStart.label !== 'Installing' ||
            !fixtureUpdate.installStart.disabled ||
            fixtureUpdate.installStart.progressVisible ||
            fixtureUpdate.install.label !== 'Restart' ||
            !fixtureUpdate.install.result.includes('Fixture release') ||
            fixtureUpdate.stats.latest !== 2 ||
            fixtureUpdate.stats.primary !== 3 ||
            fixtureUpdate.stats.mirror !== 1 ||
            fixtureUpdate.releaseMarker !== 'fixture release installed\n' ||
            fixtureUpdate.configSentinel !== 'local configuration\n' ||
            !fixtureUpdate.archiveCached)
            throw new Error(`invalid update fixture flow: ${JSON.stringify(fixtureUpdate)}`);
        if (adminUpgrade.primary.initial.label !== 'Check for updates' ||
            adminUpgrade.primary.initial.disabled ||
            !adminUpgrade.primary.initial.rightAligned ||
            adminUpgrade.primary.checking.label !== 'Checking for updates' ||
            !adminUpgrade.primary.checking.disabled ||
            !adminUpgrade.primary.checking.progressVisible ||
            Math.abs(adminUpgrade.primary.checking.progressHeight - 2) > 0.1 ||
            adminUpgrade.primary.install.label !== 'Install' ||
            adminUpgrade.primary.install.disabled ||
            adminUpgrade.primary.install.progressVisible ||
            !adminUpgrade.primary.install.result.includes('Newest release') ||
            adminUpgrade.primary.install.lastCheck !== 'Last checked: 5 minutes ago' ||
            adminUpgrade.primary.install.relativeDays !== '2 days ago' ||
            adminUpgrade.primary.restart !== 'Restart' ||
            adminUpgrade.primary.noNewUpdates.label !== 'Check for updates' ||
            !adminUpgrade.primary.noNewUpdates.resultHidden ||
            !adminUpgrade.primary.noNewUpdates.message.includes('No new updates') ||
            !adminUpgrade.primary.transferLabel.includes('Downloading release package') ||
            !adminUpgrade.previous.hidden ||
            !adminUpgrade.previous.expanded ||
            adminUpgrade.releasePicker.cards !== 4 ||
            adminUpgrade.releasePicker.nativeListbox ||
            !adminUpgrade.releasePicker.scrollable ||
            adminUpgrade.releasePicker.selected !== '20260919' ||
            !adminUpgrade.releasePicker.details?.includes('newest.zip') ||
            adminUpgrade.releasePicker.local !== 'Available on SD card' ||
            !adminUpgrade.releasePicker.desktopFlow ||
            !adminUpgrade.releasePicker.separateActions ||
            !adminUpgradeMobile.verticalFlow ||
            adminUpgradeMobile.documentOverflow)
            throw new Error(`invalid admin upgrade actions: ${JSON.stringify(adminUpgrade)}`);
        if (adminControl.heading !== 'Receiver control' ||
            adminControl.sections.join(',') !==
                'Service lifecycle,Radio configuration,Listener availability,' +
                'Session management,Connection time limits,Receiver capacity,SNR monitoring' ||
            !adminControl.fits ||
            adminControl.actionSections.restart !== 'Service lifecycle' ||
            adminControl.actionSections.reboot !== 'Service lifecycle' ||
            adminControl.actionSections.kick !== 'Session management' ||
            adminControl.actionSections.measure !== 'SNR monitoring' ||
            adminControl.radioConfiguration.bandModeSection !== 'Radio configuration' ||
            adminControl.radioConfiguration.airbandClockSection !== 'Radio configuration' ||
            adminControl.radioConfiguration.visibleFields !== 4 ||
            !adminControl.radioConfiguration.aligned ||
            !adminControl.lifecycleAligned)
            throw new Error(`invalid modern admin control page: ${JSON.stringify(adminControl)}`);
        if (!adminControlMobile.fits ||
            !adminControlMobile.actionColumns ||
            !adminControlMobile.fullWidthButtons)
            throw new Error(
                `invalid modern mobile admin control page: ${JSON.stringify(adminControlMobile)}`);
        if (adminConnect.heading !== 'Internet access' ||
            adminConnect.sections.join(',') !==
                'Public address,Busy-server redirect,Dynamic DNS,Reverse proxy' ||
            adminConnect.selectorDisplay !== 'flex' ||
            adminConnect.selectorPosition !== 'static' ||
            adminConnect.sourceRows.length !== 3 ||
            adminConnect.sourceRows.some(row =>
                row.display !== 'grid' ||
                !row.columns.includes(' ') ||
                row.missing !== row.emptyText ||
                row.background === 'rgba(0, 0, 0, 0)' ||
                row.border === row.background ||
                row.color === row.background) ||
            adminConnect.dynamicDns.groups !== 2 ||
            !adminConnect.dynamicDns.fieldColumns.includes(' ') ||
            !adminConnect.dynamicDns.controlColumns.includes(' ') ||
            !adminConnect.dynamicDns.hostWide ||
            !adminConnect.dynamicDns.actionButton ||
            !adminConnect.dynamicDns.status)
            throw new Error(`invalid modern admin Connect page: ${JSON.stringify(adminConnect)}`);
        if (adminConfig.heading !== 'Configuration' ||
            adminConfig.sections.join(',') !==
                'Startup defaults,Default passbands,Display & calibration,External interfaces,Clocking,ADC behavior' ||
            adminConfig.fields < 20 ||
            adminConfig.clockingLabels.join(',') !==
                'External Reference clock?,GPS correction of ADC clock,' +
                'External output clock frequency (enter in Hz)' ||
            adminConfig.clockingLayout.rows !== 3 ||
            adminConfig.clockingLayout.display.some(display => display !== 'grid') ||
            adminConfig.clockingLayout.columns.some(columns => !columns.includes(' ')) ||
            !adminConfig.clockingLayout.aligned ||
            adminConfig.clockingLayout.descriptions !== 3 ||
            adminConfig.clockingOverlap ||
            !adminConfig.adcCentered ||
            JSON.stringify(adminConfig.sliders) !== JSON.stringify({
                waterfallFloor: { type: 'range', min: '-30', max: '0', step: '1' },
                waterfallCeil: { type: 'range', min: '0', max: '30', step: '1' },
                zoom: { type: 'range', min: '0', max: '14', step: '1' },
                sMeter: { type: 'range', min: '-50', max: '50', step: '1' },
                waterfall: { type: 'range', min: '-50', max: '50', step: '1' },
                identLength: { type: 'range', min: '16', max: '64', step: '1' }
            }))
            throw new Error(`invalid modern admin config page: ${JSON.stringify(adminConfig)}`);
        if (adminWebpage.heading !== 'Receiver webpage' ||
            adminWebpage.sections.join(',') !==
                'Titles & messages,Location & station photo,Delivery & custom markup' ||
            adminWebpage.previews !== 3)
            throw new Error(`invalid modern admin webpage page: ${JSON.stringify(adminWebpage)}`);
        if (adminPublic.heading !== 'Public listing' ||
            adminPublic.sections.join(',') !==
                'Directory registration,Station identity & coverage' ||
            !adminPublic.registrationStatus)
            throw new Error(`invalid modern admin public page: ${JSON.stringify(adminPublic)}`);
        if (adminDX.heading !== 'DX labels & bands' ||
            adminDX.sections.join(',') !== 'Label database,Stored labels,Band presentation' ||
            !adminDX.labelList ||
            adminDX.searchFields !== 3)
            throw new Error(`invalid modern admin DX page: ${JSON.stringify(adminDX)}`);
        if (adminUpdate.heading !== 'Updates' ||
            adminUpdate.sections.join(',') !==
                'Update status,Available update,Update settings,Previous releases' ||
            !adminUpdate.statusHook ||
            !['Check for updates', 'Install', 'Restart'].includes(adminUpdate.primaryAction) ||
            adminUpdate.previousAction !== 'Previous Releases' ||
            !adminUpdate.updateSettings.alphaStable ||
            !adminUpdate.updateSettings.autoDownload ||
            !adminUpdate.updateSettings.autoInstall)
            throw new Error(`invalid modern admin Update page: ${JSON.stringify(adminUpdate)}`);
        if (adminNetwork.heading !== 'Network' ||
            adminNetwork.sections.join(',') !==
                'Interface & addressing,Reachability,MQTT,Access controls' ||
            !adminNetwork.networkStatus ||
            !adminNetwork.blacklistStatus)
            throw new Error(`invalid modern admin Network page: ${JSON.stringify(adminNetwork)}`);
        for (const [name, scroll] of [
            ['desktop', adminScrollDesktop],
            ['mobile', adminScrollMobile]
        ]) {
            if (scroll.overflowY !== 'auto' ||
                !scroll.viewportHeight ||
                scroll.maxScroll <= 0 ||
                !scroll.reachedBottom)
                throw new Error(`invalid admin ${name} scrolling: ${JSON.stringify(scroll)}`);
        }
        if (!adminScrollDesktop.stickyHeader)
            throw new Error(
                `invalid sticky admin header: ${JSON.stringify(adminScrollDesktop)}`);
        if (adminGPS.heading !== 'GPS' ||
            adminGPS.sections.join(',') !== 'Receiver solution,Satellite channels' ||
            !adminGPS.infoTable ||
            !adminGPS.channelTable ||
            !adminGPS.skyCanvas)
            throw new Error(`invalid modern admin GPS page: ${JSON.stringify(adminGPS)}`);
        if (adminLog.heading !== 'Server log' ||
            adminLog.sections.join(',') !== 'Log controls,Recent activity' ||
            !adminLog.output ||
            adminLog.actions !== 3)
            throw new Error(`invalid modern admin Log page: ${JSON.stringify(adminLog)}`);
        if (adminConsole.heading !== 'Console' ||
            adminConsole.sections.join(',') !== 'Session & shortcuts,Terminal,Input' ||
            !adminConsole.terminal ||
            !adminConsole.connect)
            throw new Error(`invalid modern admin Console page: ${JSON.stringify(adminConsole)}`);
        if (!consoleOpenOrder.sizeFirst)
            throw new Error(`invalid console open order: ${JSON.stringify(consoleOpenOrder)}`);
        if (!consoleANSI.countedMove ||
            !consoleANSI.savedCursor ||
            !consoleANSI.scrollCursor ||
            !consoleANSI.marginReset ||
            !consoleANSI.oscConsumed ||
            !consoleANSI.carriageReturn)
            throw new Error(`invalid console ANSI handling: ${JSON.stringify(consoleANSI)}`);
        if (adminExtensions.heading !== 'Extensions' ||
            adminExtensions.sections.join(',') !== 'Extension configuration' ||
            !adminExtensions.navigation ||
            !adminExtensions.configuration)
            throw new Error(
                `invalid modern admin Extensions page: ${JSON.stringify(adminExtensions)}`);
        if (adminSecurity.heading !== 'Security' ||
            adminSecurity.sections.join(',') !==
                'Passwords & listener access,Privileged & shared access,Admin session resilience' ||
            !adminSecurity.userPassword ||
            !adminSecurity.adminPassword)
            throw new Error(
                `invalid modern admin Security page: ${JSON.stringify(adminSecurity)}`);
        if (adminExtensionsMobile.navDisplay !== 'flex' ||
            adminExtensionsMobile.navPosition !== 'static' ||
            !adminExtensionsMobile.navScrollable ||
            !adminExtensionsMobile.selectedReadable ||
            !adminExtensionsMobile.configFits)
            throw new Error(`invalid admin extensions mobile layout: ${JSON.stringify(adminExtensionsMobile)}`);
        for (const layout of adminResponsive) {
            if (!layout.modern || layout.theme !== 'midnight' ||
                layout.documentOverflow || layout.themeControlOverlap)
                throw new Error(`invalid responsive admin layout: ${JSON.stringify(layout)}`);
        }

        const dxRows = state.dxLabelRows;
        if (new Set(dxRows.rows.slice(0, 3)).size !== 3 ||
            dxRows.rows[3] !== dxRows.rows[0] ||
            dxRows.rows[2] + dxRows.labelHeight > 70 ||
            dxRows.labelFontSize !== 11 ||
            dxRows.labelPadding !== 3 ||
            dxRows.labelRadius !== 3 ||
            dxRows.firstTypeColor !== 'rgb(18, 52, 86)' ||
            dxRows.secondTypeColor !== 'rgb(171, 205, 239)' ||
            dxRows.firstTypeColor === dxRows.secondTypeColor)
            throw new Error(`invalid three-row DX label layout: ${JSON.stringify(dxRows)}`);

        const passband = state.spectrumPassband;
        if (!state.spectrumPassbandCanvas.present ||
            state.spectrumPassbandCanvas.pointerEvents !== 'none' ||
            state.spectrumPassbandCanvas.width !== 1024 ||
            state.spectrumPassbandCanvas.height !== 200)
            throw new Error(`invalid spectrum passband canvas: ${JSON.stringify(state.spectrumPassbandCanvas)}`);
        const recording = state.squelchRecording;
        if (recording.command !== 'SET squelch=7 param=1.00' ||
            recording.totalSize !== 2560 ||
            !recording.preSequenceMatches ||
            !recording.currentSequenceMatches ||
            recording.preCaptured)
            throw new Error(`invalid squelch recording periods: ${JSON.stringify(recording)}`);
        if (airbandClockAdmin.effective.join(',') !== '0,0,1,1' ||
            !airbandClockAdmin.shownInAirband ||
            !airbandClockAdmin.preferred.includes('98.304-147.456 MHz') ||
            !airbandClockAdmin.preferred.includes('Best rejection') ||
            !airbandClockAdmin.forced.includes('36 kHz audio requires') ||
            !airbandClockAdmin.forcedDisabled ||
            airbandClockAdmin.forcedValue !== 1 ||
            !airbandClockAdmin.advanced.includes('108-110.592 MHz is unavailable') ||
            !airbandClockAdmin.restoredEnabled ||
            !airbandClockAdmin.staleFirstIgnored ||
            !airbandClockAdmin.hiddenInHF ||
            !airbandClockAdmin.hiddenStatusCleared)
            throw new Error(`invalid airband clock admin UI: ${JSON.stringify(airbandClockAdmin)}`);
        if (JSON.stringify(passband.visible) !== '{"left":400,"right":700,"width":300}' ||
            JSON.stringify(passband.clipped) !== '{"left":0,"right":100,"width":100}' ||
            JSON.stringify(passband.axisScaled) !== '{"left":900,"right":950,"width":50}' ||
            JSON.stringify(passband.rightClipped) !== '{"left":940,"right":975,"width":35}' ||
            JSON.stringify(passband.narrow) !== '{"left":998,"right":999,"width":1}' ||
            JSON.stringify(passband.reversed) !== '{"left":400,"right":700,"width":300}' ||
            passband.offscreen !== null)
            throw new Error(`invalid spectrum passband mapping: ${JSON.stringify(passband)}`);

        if (errors.length)
            throw new Error(errors.join('\n'));

        console.log(JSON.stringify({
            ...state, extensionPanelAudit, uiFoundation, extensionThemes, drmThemeAssets,
            extensionFocus,
            cloudControlContrast,
            receiverResponsive, faxMobile, faxCollapse, extensionLayouts, drmPanelControls,
            panelToggle, adminFoundation, adminClassic, adminWarningThemes, adminResponsive,
            adminUsage, usageFileAudit,
            fixtureUpdate,
            adminControl, adminConnect, adminConfig,
            adminWebpage, adminPublic, adminDX, adminUpdate, adminNetwork, adminGPS,
            adminLog, adminConsole, consoleOpenOrder, consoleANSI, adminExtensions, adminSecurity,
            adminControlMobile, adminExtensionsMobile, adminClassicMobile,
            adminScrollDesktop, adminScrollMobile
        }));
    } finally {
        await browser.close();
    }
})().catch(error => {
    console.error(error.stack || error);
    process.exit(1);
});
