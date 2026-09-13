const { chromium } = require('playwright');

const baseUrl = process.env.WEBSDR_HARNESS_URL || 'http://127.0.0.1:8073/';

(async () => {
    const browser = await chromium.launch({
        headless: true,
        args: ['--autoplay-policy=no-user-gesture-required']
    });
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
    const errors = [];

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

        await page.evaluate(() => extint_open('space_weather'));
        await page.waitForFunction(() => {
            const el = w3_el('id-sw-data');
            return el && el.textContent.includes('Solar flux') &&
                el.textContent.includes('109') &&
                el.textContent.includes('Kp 2.0 - 3.0') &&
                el.textContent.includes('-4.0 nT');
        }, null, { timeout: 30000 });

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
                    label.remove();
                    return { rows, eibiRows, labelHeight };
                } finally {
                    cfg.dx_three_high = saved;
                }
            })()
        }));

        const dxRows = state.dxLabelRows;
        if (new Set(dxRows.rows.slice(0, 3)).size !== 3 ||
            dxRows.rows[3] !== dxRows.rows[0] ||
            dxRows.rows[2] + dxRows.labelHeight > 70)
            throw new Error(`invalid three-row DX label layout: ${JSON.stringify(dxRows)}`);
        if (dxRows.eibiRows.join(',') !== '5,45,5')
            throw new Error(`EiBi DX label layout changed: ${dxRows.eibiRows}`);

        const passband = state.spectrumPassband;
        if (!state.spectrumPassbandCanvas.present ||
            state.spectrumPassbandCanvas.pointerEvents !== 'none' ||
            state.spectrumPassbandCanvas.width !== 1024 ||
            state.spectrumPassbandCanvas.height !== 200)
            throw new Error(`invalid spectrum passband canvas: ${JSON.stringify(state.spectrumPassbandCanvas)}`);
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

        console.log(JSON.stringify(state));
    } finally {
        await browser.close();
    }
})().catch(error => {
    console.error(error.stack || error);
    process.exit(1);
});
