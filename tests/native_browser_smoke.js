const { chromium } = require('playwright');

const baseUrl = process.env.WEBSDR_HARNESS_URL || 'http://127.0.0.1:8073/';

(async () => {
    const browser = await chromium.launch({
        headless: true,
        args: ['--autoplay-policy=no-user-gesture-required']
    });
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
    const errors = [];

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

        const state = await page.evaluate(() => ({
            title: document.title,
            soundSocket: window.ws_snd.readyState,
            waterfallSocket: window.ws_wf.readyState,
            waterfallLine: window.wf_canvas_actual_line,
            waterfallCanvases: window.wf_canvases.length,
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
