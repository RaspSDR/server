const assert = require('assert');
const fs = require('fs');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');
const { chromium } = require('playwright');
const { zip, createUpdateFixture } = require('./update_fixture_server');

const binaryArgument = process.argv.indexOf('--binary');
if (binaryArgument < 0 || !process.argv[binaryArgument + 1])
    throw new Error('usage: node tests/native_update_test.js --binary <native websdr.bin>');
const binary = path.resolve(process.argv[binaryArgument + 1]);
assert(fs.readFileSync(binary).includes(Buffer.from('native harness: reboot ignored')),
    'update tests require a native harness binary, never a production binary');
const configPath = path.join(path.dirname(binary), 'config', 'admin.json');
const originalConfig = fs.readFileSync(configPath);
const root = fs.mkdtempSync(path.join(path.dirname(binary), 'update-test-'));
const rebootEvents = path.join(root, 'reboots');
let browser, serverProcess, fixture, page;
let output = '';

function release(date, options = {}) {
    return {
        metadata: { date, changes: [`Mock release ${date}`], ...options.metadata },
        filename: `fixture-${date}.zip`,
        archive: zip([
            ['websdr.bin', `binary ${date}\n`],
            ['release-marker.txt', date],
            ['config/sentinel.conf', 'must not overwrite local configuration\n']
        ]),
        ...options
    };
}

function catalog() {
    return {
        '20990103': release('2099-01-03'),
        '20990102': release('2099-01-02', { metadata: { date: '2099-01-02', stable: false } }),
        '20990101': release('2099-01-01', { metadata: { date: '2099-01-01', stable: true } })
    };
}

async function waitFor(predicate, label) {
    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) {
        if (serverProcess && (serverProcess.exitCode !== null || serverProcess.signalCode !== null))
            throw new Error(`native server exited while ${label}\n${output}`);
        if (await predicate())
            return;
        await new Promise(resolve => setTimeout(resolve, 100));
    }
    throw new Error(`timed out ${label}\n${output}`);
}

async function unusedPort() {
    const probe = net.createServer();
    await new Promise((resolve, reject) => {
        probe.once('error', reject);
        probe.listen(0, '127.0.0.1', resolve);
    });
    const port = probe.address().port;
    await new Promise(resolve => probe.close(resolve));
    return port;
}

async function stop() {
    if (page) {
        await page.close();
        page = null;
    }
    if (serverProcess && serverProcess.pid &&
            serverProcess.exitCode === null && serverProcess.signalCode === null) {
        const child = serverProcess;
        await new Promise(resolve => {
            const timer = setTimeout(() => child.kill('SIGKILL'), 2000);
            child.once('exit', () => {
                clearTimeout(timer);
                resolve();
            });
            child.kill('SIGTERM');
        });
    }
    serverProcess = null;
    if (fixture) {
        await new Promise(resolve => fixture.server.close(resolve));
        fixture = null;
    }
}

async function start({ config = {}, automatic = false, releases = catalog(), ...mock } = {}) {
    await stop();
    for (const name of ['update', 'config', 'websdr.bin', 'release-marker.txt', 'reboots']) {
        fs.rmSync(path.join(root, name), { recursive: true, force: true });
    }
    fs.mkdirSync(path.join(root, 'update'));
    fs.mkdirSync(path.join(root, 'config'));
    fs.writeFileSync(path.join(root, 'config', 'sentinel.conf'), 'local configuration\n');
    const admin = {
        ...JSON.parse(originalConfig),
        admin_password: '',
        admin_auto_login: true,
        update_channel: true,
        update_check: false,
        update_install: false,
        update_reboot: false,
        ...config
    };
    if (config.update_channel === null)
        delete admin.update_channel;
    fs.writeFileSync(configPath, JSON.stringify(admin));
    fixture = createUpdateFixture({ releases, ...mock });
    await new Promise((resolve, reject) => {
        fixture.server.once('error', reject);
        fixture.server.listen(0, '127.0.0.1', resolve);
    });
    const port = await unusedPort();
    const fixturePort = fixture.server.address().port;
    output = '';
    serverProcess = spawn(binary, [], {
        env: {
            ...process.env,
            WEBSDR_HARNESS_PORT: String(port),
            WEBSDR_UPDATE_API_BASE: `http://127.0.0.1:${fixturePort}/api`,
            WEBSDR_UPDATE_ROOT: root,
            WEBSDR_REBOOT_EVENTS: rebootEvents,
            WEBSDR_UPDATE_AUTOMATIC: automatic ? '1' : '0'
        },
        stdio: ['ignore', 'pipe', 'pipe']
    });
    serverProcess.stdout.on('data', chunk => { output += chunk; });
    serverProcess.stderr.on('data', chunk => { output += chunk; });
    let spawnError;
    serverProcess.once('error', error => { spawnError = error; });
    const base = `http://127.0.0.1:${port}/`;
    await waitFor(async () => {
        if (spawnError)
            throw spawnError;
        // Startup connection refusals are expected until the listener is ready.
        try {
            return (await fetch(`${base}status`)).ok;
        } catch (error) {
            if (error.cause?.code !== 'ECONNREFUSED')
                throw error;
            return false;
        }
    }, 'waiting for HTTP listener');
    assert(output.includes('native harness host'));
    page = await browser.newPage();
    await page.goto(`${base}admin`, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(() => window.adm && typeof update_status_cb === 'function' &&
        document.getElementById('id-nav-update'));
    await page.evaluate(() => {
        window.testUpdateStatuses = [];
        const original = update_status_cb;
        update_status_cb = status => {
            window.testUpdateStatuses.push(status);
            original(status);
        };
        document.getElementById('id-nav-update').click();
    });
    await page.waitForSelector('.id-update-primary-action');
}

async function statusAfter(command, action) {
    await page.evaluate(({ command, action }) => {
        window.testUpdateStatuses = [];
        if (action === 'primary')
            update_primary_cb();
        else if (action === 'duplicate-install') {
            ext_send('SET force_check=1 force_build=1 force_reboot=0');
            ext_send('SET force_check=1 force_build=1 force_reboot=1');
        } else if (command)
            ext_send(command);
    }, { command, action });
    let status;
    await waitFor(async () => {
        await page.evaluate(() => ext_send('SET update_status'));
        status = await page.evaluate(requireStart => {
            const statuses = window.testUpdateStatuses;
            const start = statuses.findIndex(value => value.message === 'Getting latest release information');
            if (requireStart && start < 0)
                return null;
            return statuses.slice(requireStart ? start + 1 : 0)
                .filter(value => value.last_check).pop();
        }, !!(command || action));
        return status && !status.in_progress;
    }, `waiting for updater completion: ${command || 'automatic startup'}`);
    return status;
}

const check = () => statusAfter('SET force_check=1 force_build=0');
const install = (reboot = false) =>
    statusAfter(`SET force_check=1 force_build=1 force_reboot=${reboot ? 1 : 0}`);
const file = name => path.join(root, name);
const exists = name => fs.existsSync(file(name));
const downloads = () => fixture.stats.primary + fixture.stats.mirror;

function assertInstalled(date, reboots = 0) {
    assert.strictEqual(fs.readFileSync(file('websdr.bin'), 'utf8'), `binary ${date}\n`);
    assert.strictEqual(fs.readFileSync(file('release-marker.txt'), 'utf8'), date);
    assert.strictEqual(fs.readFileSync(file('config/sentinel.conf'), 'utf8'), 'local configuration\n');
    assert.strictEqual(exists('reboots') ? fs.readFileSync(rebootEvents, 'utf8') : '',
        'reboot\n'.repeat(reboots));
    if (reboots) {
        const installed = output.indexOf('UPDATE: build took');
        const rebooted = output.indexOf('native harness: reboot ignored');
        assert(installed >= 0 && rebooted > installed, 'reboot must follow completed install');
    }
}

function assertNotInstalled() {
    assert(!exists('websdr.bin'), 'failed/check-only update must not install');
    assert(!exists('release-marker.txt'));
    assert(!exists('reboots'), 'failed/check-only update must not reboot');
    assert.strictEqual(fs.readFileSync(file('config/sentinel.conf'), 'utf8'), 'local configuration\n');
}

let passed = 0;
async function test(name, run) {
    try {
        await run();
        passed++;
        console.log(`PASS ${name}`);
    } catch (error) {
        throw new Error(`${name}: ${error.stack}\n${output}`);
    }
}

(async () => {
    try {
        browser = await chromium.launch({ headless: true });
        await test('missing channel defaults to Alpha; check never downloads or installs', async () => {
            await start({ config: { update_channel: null, update_install: true, update_reboot: true } });
            const status = await check();
            assert.strictEqual(status.release_date, '2099-01-03');
            assert.strictEqual(status.fail_reason, 0);
            assert(fixture.stats.requests.some(request => request.path === '/api/releases/latest'));
            assert.strictEqual(downloads(), 0);
            assertNotInstalled();
        });
        await test('Stable selects only explicit stable:true; channel switch rechecks Alpha', async () => {
            await start({ config: { update_channel: false } });
            assert.strictEqual((await check()).release_date, '2099-01-01');
            assert(fixture.stats.requests.some(request =>
                request.path === '/api/releases/latest?stable=true'));
            await page.evaluate(() => update_channel_cb('adm.update_channel', 0, false));
            await waitFor(() => fixture.stats.requests.some(request =>
                request.path === '/api/releases/latest'), 'waiting for Alpha channel recheck');
            assert.strictEqual((await statusAfter()).release_date, '2099-01-03');
            assertNotInstalled();
        });
        await test('manual install works with Auto install off; Restart UI safely requests reboot', async () => {
            await start();
            await check();
            const status = await statusAfter(null, 'primary');
            assert.strictEqual(status.fail_reason, 0);
            assert.strictEqual(status.restart_required, 1);
            assertInstalled('2099-01-03');
            await page.waitForFunction(() => update_primary_mode === 'restart');
            await page.evaluate(() => update_primary_cb());
            await waitFor(() => exists('reboots'), 'waiting for mocked Restart');
            assertInstalled('2099-01-03', 1);
        });
        await test('manual install-and-reboot occurs only after successful installation', async () => {
            await start();
            assert.strictEqual((await install(true)).fail_reason, 0);
            assertInstalled('2099-01-03', 1);
        });
        await test('overlapping install request is ignored, including its reboot request', async () => {
            await start({ delayMs: 250 });
            assert.strictEqual((await statusAfter(null, 'duplicate-install')).fail_reason, 0);
            assert.strictEqual(fixture.stats.latest, 1);
            assert.strictEqual(downloads(), 1);
            assertInstalled('2099-01-03');
        });
        await test('wrapped release JSON is accepted', async () => {
            await start({ wrapped: true });
            assert.strictEqual((await install()).fail_reason, 0);
            assertInstalled('2099-01-03');
        });
        await test('Stable never falls back to unmarked, false, or string-true releases', async () => {
            const releases = catalog();
            delete releases['20990101'];
            releases['20990104'] = release('2099-01-04',
                { metadata: { date: '2099-01-04', stable: 'true' } });
            await start({ releases, config: { update_channel: false } });
            assert.strictEqual((await install(true)).fail_reason, 5);
            assert.strictEqual(downloads(), 0);
            assertNotInstalled();
        });
        await test('verified cached package avoids network downloads', async () => {
            const releases = catalog();
            await start({ releases });
            fs.writeFileSync(file(`update/${releases['20990103'].filename}`),
                releases['20990103'].archive);
            assert.strictEqual((await install()).fail_reason, 0);
            assert.strictEqual(downloads(), 0);
            assertInstalled('2099-01-03');
        });
        await test('corrupt cached package is replaced, not trusted', async () => {
            await start();
            fs.writeFileSync(file('update/fixture-2099-01-03.zip'), 'corrupt cache');
            assert.strictEqual((await install()).fail_reason, 0);
            assert.strictEqual(downloads(), 1);
            assertInstalled('2099-01-03');
        });
        await test('primary retries three times, then falls back to mirror', async () => {
            const releases = catalog();
            releases['20990103'].primaryStatus = 503;
            await start({ releases });
            assert.strictEqual((await install()).fail_reason, 0);
            assert.strictEqual(fixture.stats.primary, 3);
            assert.strictEqual(fixture.stats.mirror, 1);
            assertInstalled('2099-01-03');
        });
        await test('interrupted transfer resumes with Range and verifies full checksum', async () => {
            const releases = catalog();
            releases['20990103'].archive = zip([
                ['websdr.bin', 'binary 2099-01-03\n'],
                ['release-marker.txt', '2099-01-03'],
                ['padding', 'x'.repeat(32768)]
            ]);
            releases['20990103'].interruptOnce = true;
            await start({ releases });
            assert.strictEqual((await install()).fail_reason, 0);
            assert(fixture.stats.requests.some(request => request.range === 'bytes=8192-'));
            assertInstalled('2099-01-03');
        });
        await test('server rejecting Range restarts the interrupted transfer from zero', async () => {
            const releases = catalog();
            releases['20990103'].archive = zip([
                ['websdr.bin', 'binary 2099-01-03\n'],
                ['release-marker.txt', '2099-01-03'],
                ['padding', 'x'.repeat(32768)]
            ]);
            releases['20990103'].interruptOnce = true;
            releases['20990103'].ignoreRange = true;
            await start({ releases });
            assert.strictEqual((await install()).fail_reason, 0);
            const requests = fixture.stats.requests.filter(request => request.path.includes('/downloads/'));
            assert.deepStrictEqual(requests.map(request => request.range), [null, 'bytes=8192-', null]);
            assertInstalled('2099-01-03');
        });
        for (const failure of ['unavailable', 'checksum', 'invalid zip', 'path traversal']) {
            await test(`${failure} fails without installation or reboot`, async () => {
                const releases = catalog();
                const latest = releases['20990103'];
                if (failure === 'unavailable') {
                    latest.primaryStatus = latest.mirrorStatus = 503;
                } else if (failure === 'checksum') {
                    latest.corrupt = true;
                } else if (failure === 'invalid zip') {
                    latest.archive = Buffer.from('not a ZIP, but its checksum matches');
                } else {
                    latest.archive = zip([['../escaped', 'must not extract']]);
                }
                await start({ releases });
                assert.strictEqual((await install(true)).fail_reason,
                    failure === 'unavailable' || failure === 'checksum' ? 6 : 8);
                if (failure === 'unavailable' || failure === 'checksum') {
                    assert.strictEqual(fixture.stats.primary, 3);
                    assert.strictEqual(fixture.stats.mirror, 3);
                }
                assertNotInstalled();
                assert(!fs.existsSync(path.join(root, '..', 'escaped')));
            });
        }
        for (const [name, mock] of [
            ['malformed JSON', { metadata: '{' }],
            ['HTTP error', { metadataStatus: 503 }],
            ['missing downloads', { metadata: JSON.stringify({ date: '2099-01-03' }) }],
            ['unsafe filename', { download: { filename: '../bad.zip' } }],
            ['invalid checksum', { download: { sha256: 'invalid' } }],
            ['invalid URL', { download: { link: 'file:///etc/passwd' } }]
        ]) {
            await test(`${name} rejects metadata before downloading or rebooting`, async () => {
                const releases = catalog();
                if (mock.download)
                    releases['20990103'].download = mock.download;
                await start({ releases, ...(mock.download ? {} : mock) });
                assert.strictEqual((await install(true)).fail_reason, 5);
                assert.strictEqual(downloads(), 0);
                assertNotInstalled();
            });
        }
        await test('extraction failure reports an error and never reboots', async () => {
            const releases = catalog();
            releases['20990103'].archive = zip([['blocked/file', 'cannot install']]);
            await start({ releases });
            fs.writeFileSync(file('blocked'), 'not a directory');
            assert.strictEqual((await install(true)).fail_reason, 8);
            assertNotInstalled();
        });
        await test('exact same version is not offered as a new update', async () => {
            const releases = catalog();
            await start({ releases });
            const running = await check();
            const date = `${running.vmaj}-${String(Math.floor(running.vmin / 100)).padStart(2, '0')}-` +
                String(running.vmin % 100).padStart(2, '0');
            for (const key of Object.keys(releases))
                delete releases[key];
            releases[date.replace(/-/g, '')] = release(date);
            const status = await check();
            assert.strictEqual(status.pmaj, status.vmaj);
            assert.strictEqual(status.pmin, status.vmin);
            assert.strictEqual(await page.evaluate(() => update_primary_mode), 'check');
            assert.strictEqual(downloads(), 0);
            assertNotInstalled();
        });
        await test('same/older release check reports no update; explicit install allows downgrade', async () => {
            const releases = { '20000101': release('2000-01-01') };
            await start({ releases });
            const status = await check();
            assert(status.pmaj < status.vmaj);
            assert.strictEqual(downloads(), 0);
            assertNotInstalled();
            assert.strictEqual((await install()).fail_reason, 0);
            assertInstalled('2000-01-01');
        });
        await test('Previous Releases filters to cached packages; selected release installs from SD', async () => {
            const releases = catalog();
            const previous = releases['20990101'];
            previous.filename = 'web-888-alpine-3.20-armv7-20990101.zip';
            await start({ releases, config: { update_channel: false } });
            fs.writeFileSync(file(previous.filename), previous.archive);
            await page.evaluate(() => update_previous_releases_cb());
            await page.waitForFunction(() => Object.keys(update_release_catalog).length === 1);
            assert.deepStrictEqual(await page.evaluate(() => Object.keys(update_release_catalog)),
                ['20990101']);
            assert.strictEqual((await statusAfter('SET release_install date=20990101 reboot=1')).fail_reason, 0);
            assert.strictEqual(downloads(), 0);
            assert(fixture.stats.requests.some(request => request.path === '/api/releases/20990101'));
            assertInstalled('2099-01-01', 1);
        });
        for (const policy of [
            { name: 'disabled', config: {}, installed: false, downloaded: false },
            { name: 'download only', config: { update_check: true }, installed: false, downloaded: true },
            { name: 'install without reboot', config: { update_install: true }, installed: true, downloaded: true },
            { name: 'install and reboot', config: { update_install: true, update_reboot: true },
                installed: true, downloaded: true }
        ]) {
            await test(`automatic startup policy: ${policy.name}`, async () => {
                await start({ automatic: true, config: policy.config });
                const status = await statusAfter();
                assert.strictEqual(status.fail_reason, 0);
                assert.strictEqual(downloads() > 0, policy.downloaded);
                assert.strictEqual(exists('update/fixture-2099-01-03.zip'), policy.downloaded);
                if (policy.installed)
                    assertInstalled('2099-01-03', policy.config.update_reboot ? 1 : 0);
                else
                    assertNotInstalled();
            });
        }
        await test('automatic download failure is reported; no install or reboot', async () => {
            const releases = catalog();
            releases['20990103'].primaryStatus = releases['20990103'].mirrorStatus = 503;
            await start({ automatic: true, releases, config: { update_check: true, update_reboot: true } });
            assert.strictEqual((await statusAfter()).fail_reason, 6);
            assertNotInstalled();
        });
        await test('automatic install failure never requests reboot', async () => {
            const releases = catalog();
            releases['20990103'].corrupt = true;
            await start({ automatic: true, releases, config: { update_install: true, update_reboot: true } });
            assert.strictEqual((await statusAfter()).fail_reason, 6);
            assertNotInstalled();
        });
        await test('automatic same/older release never downloads, installs, or reboots', async () => {
            await start({
                automatic: true,
                releases: { '20000101': release('2000-01-01') },
                config: { update_check: true, update_install: true, update_reboot: true }
            });
            assert.strictEqual((await statusAfter()).fail_reason, 0);
            assert.strictEqual(downloads(), 0);
            assertNotInstalled();
        });
        console.log(`native updater integration: ${passed} scenarios passed`);
    } finally {
        try {
            await stop();
            if (browser)
                await browser.close();
        } finally {
            fs.writeFileSync(configPath, originalConfig);
            fs.rmSync(root, { recursive: true, force: true });
        }
    }
})().catch(error => {
    console.error(error.stack || error);
    process.exitCode = 1;
});
