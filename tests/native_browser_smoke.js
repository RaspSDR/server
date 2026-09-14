const { chromium } = require('playwright');

const baseUrl = process.env.WEBSDR_HARNESS_URL || 'http://127.0.0.1:8073/';

(async () => {
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
                compactThemeReadable: window.innerWidth > 760 ||
                    (themePicker && themePicker.width >= 84 &&
                        getComputedStyle(document.getElementById('id-ui-theme-select')).color !==
                            'rgba(0, 0, 0, 0)')
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
        await page.goto(baseUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });
        await page.waitForFunction(() => {
            return window.waterfall_setup_done === 1 &&
                window.ws_snd && window.ws_snd.readyState === WebSocket.OPEN &&
                window.ws_wf && window.ws_wf.readyState === WebSocket.OPEN;
        }, null, { timeout: 30000 });

        const initialLine = await page.evaluate(() => window.wf_canvas_actual_line);
        await page.waitForFunction(line => {
            return Number.isFinite(window.wf_canvas_actual_line) &&
                window.wf_canvas_actual_line !== line;
        }, initialLine, { timeout: 30000 });

        await page.evaluate(() => {
            const focusReturn = document.createElement('button');
            focusReturn.id = 'id-test-focus-return';
            focusReturn.type = 'button';
            focusReturn.textContent = 'focus return';
            focusReturn.style.position = 'fixed';
            focusReturn.style.left = '-10000px';
            document.body.appendChild(focusReturn);
            focusReturn.focus();
            extint_open('space_weather');
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
                const saved = cfg.dx_three_high;
                try {
                    cfg.dx_three_high = true;
                    const rows = [0, 1, 2, 3].map(i => dx_label_top_px(false, i, 35));
                    const eibiRows = [0, 1, 2].map(i => dx_label_top_px(true, i, 40));
                    const label = document.createElement('div');
                    label.className = 'cl-dx-label';
                    label.textContent = 'DX';
                    document.body.appendChild(label);
                    const labelHeight = label.getBoundingClientRect().height;
                    const labelFontSize = parseFloat(getComputedStyle(label).fontSize);
                    label.remove();
                    return { rows, eibiRows, labelHeight, labelFontSize };
                } finally {
                    cfg.dx_three_high = saved;
                }
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
            for (const theme of ['midnight', 'ember']) {
                select.value = theme;
                select.dispatchEvent(new Event('change', { bubbles: true }));
                const style = getComputedStyle(document.documentElement);
                const muted = style.getPropertyValue('--ui-text-muted').trim();
                const surface = style.getPropertyValue('--ui-surface').trim();
                themes.push({
                    theme: document.documentElement.dataset.uiTheme,
                    accent: style.getPropertyValue('--ui-accent').trim(),
                    mutedContrast: Number(contrast(muted, surface).toFixed(2))
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
        const extensionFocus = await page.evaluate(() => {
            const close = document.getElementById('id-ext-controls-close');
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
                const disabled = w3_el('id-airband-adc-clock').disabled;

                return {
                    effective: [
                        airband_adc_clock_effective(0, 0),
                        airband_adc_clock_effective(0, 1),
                        airband_adc_clock_effective(0, 2),
                        airband_adc_clock_effective(1, 2)
                    ],
                    preferred,
                    forced,
                    forcedDisabled,
                    forcedValue,
                    advanced,
                    restoredEnabled,
                    staleFirstIgnored,
                    disabled
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
                return {
                    danger: status?.classList.contains('ui-status-danger'),
                    fontSize: heading? parseFloat(getComputedStyle(heading).fontSize) : 0,
                    text: heading?.textContent
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
        await adminPage.close();

        if (!uiFoundation.selectPresent ||
            uiFoundation.storedTheme !== 'midnight' ||
            uiFoundation.receiverThemeParent !== 'id-rf-theme-actions' ||
            JSON.stringify(uiFoundation.themes) !== JSON.stringify([
                { theme: 'midnight', accent: '#58a6ff', mutedContrast: 6.84 },
                { theme: 'ember', accent: '#e58a3a', mutedContrast: 6.3 }
            ]) ||
            JSON.stringify(uiFoundation.themeOptions) !== JSON.stringify(['midnight', 'ember']) ||
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
            uiFoundation.tabPalette.labels[2] !== 'AUD' ||
            uiFoundation.tabPalette.legacyColorClasses ||
            uiFoundation.tabPalette.inactiveBackgrounds.length !== 1 ||
            uiFoundation.tabPalette.selectedBackground === uiFoundation.tabPalette.inactiveBackgrounds[0])
            throw new Error(`invalid modern UI foundation: ${JSON.stringify(uiFoundation)}`);
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
        for (const layout of receiverResponsive) {
            if (!layout.modern || layout.theme !== 'midnight' ||
                layout.documentOverflow ||
                layout.themeControlOverlap ||
                !layout.controlFitsViewport ||
                !layout.compactThemeReadable ||
                !layout.main || !layout.waterfall ||
                layout.waterfall.width <= 0 || layout.waterfall.width > layout.viewport[0] + 2)
                throw new Error(`invalid responsive receiver layout: ${JSON.stringify(layout)}`);
        }
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
            !adminFoundation.keyboardNavigation.before ||
            adminFoundation.keyboardNavigation.before === adminFoundation.keyboardNavigation.after ||
            adminFoundation.keyboardNavigation.after !== adminFoundation.keyboardNavigation.focused)
            throw new Error(`invalid modern admin foundation: ${JSON.stringify(adminFoundation)}`);
        for (const layout of adminResponsive) {
            if (!layout.modern || layout.theme !== 'midnight' ||
                layout.documentOverflow || layout.themeControlOverlap)
                throw new Error(`invalid responsive admin layout: ${JSON.stringify(layout)}`);
        }

        const dxRows = state.dxLabelRows;
        if (new Set(dxRows.rows.slice(0, 3)).size !== 3 ||
            dxRows.rows[3] !== dxRows.rows[0] ||
            dxRows.rows[2] + dxRows.labelHeight > 70 ||
            dxRows.labelFontSize > 10)
            throw new Error(`invalid three-row DX label layout: ${JSON.stringify(dxRows)}`);
        if (dxRows.eibiRows.join(',') !== '5,45,5')
            throw new Error(`EiBi DX label layout changed: ${dxRows.eibiRows}`);

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
            !airbandClockAdmin.preferred.includes('98.304-147.456 MHz') ||
            !airbandClockAdmin.preferred.includes('Best rejection') ||
            !airbandClockAdmin.forced.includes('36 kHz audio requires') ||
            !airbandClockAdmin.forcedDisabled ||
            airbandClockAdmin.forcedValue !== 1 ||
            !airbandClockAdmin.advanced.includes('108-110.592 MHz is unavailable') ||
            !airbandClockAdmin.restoredEnabled ||
            !airbandClockAdmin.staleFirstIgnored ||
            !airbandClockAdmin.disabled)
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
            ...state, uiFoundation, extensionFocus, receiverResponsive, panelToggle,
            adminFoundation, adminResponsive
        }));
    } finally {
        await browser.close();
    }
})().catch(error => {
    console.error(error.stack || error);
    process.exit(1);
});
