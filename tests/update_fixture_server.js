const crypto = require('crypto');
const fs = require('fs');
const http = require('http');
const path = require('path');

const crcTable = Array.from({ length: 256 }, (_, index) => {
    let value = index;
    for (let bit = 0; bit < 8; bit++)
        value = value & 1? (value >>> 1) ^ 0xedb88320:value >>> 1;
    return value >>> 0;
});

function crc32(buffer) {
    let value = 0xffffffff;
    for (const byte of buffer)
        value = (value >>> 8) ^ crcTable[(value ^ byte) & 0xff];
    return (value ^ 0xffffffff) >>> 0;
}

function zip(entries) {
    const records = [];
    const directory = [];
    let offset = 0;
    for (const [name, value] of entries) {
        const filename = Buffer.from(name);
        const data = Buffer.from(value);
        const crc = crc32(data);
        const local = Buffer.alloc(30);
        local.writeUInt32LE(0x04034b50, 0);
        local.writeUInt16LE(20, 4);
        local.writeUInt32LE(crc, 14);
        local.writeUInt32LE(data.length, 18);
        local.writeUInt32LE(data.length, 22);
        local.writeUInt16LE(filename.length, 26);
        records.push(local, filename, data);

        const central = Buffer.alloc(46);
        central.writeUInt32LE(0x02014b50, 0);
        central.writeUInt16LE(20, 4);
        central.writeUInt16LE(20, 6);
        central.writeUInt32LE(crc, 16);
        central.writeUInt32LE(data.length, 20);
        central.writeUInt32LE(data.length, 24);
        central.writeUInt16LE(filename.length, 28);
        central.writeUInt32LE(offset, 42);
        directory.push(central, filename);
        offset += local.length + filename.length + data.length;
    }

    const directorySize = directory.reduce((size, record) => size + record.length, 0);
    const end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50, 0);
    end.writeUInt16LE(entries.length, 8);
    end.writeUInt16LE(entries.length, 10);
    end.writeUInt32LE(directorySize, 12);
    end.writeUInt32LE(offset, 16);
    return Buffer.concat([...records, ...directory, end]);
}

function createUpdateFixture({ releases, metadata, metadataStatus = 200, delayMs = 0, wrapped = false }) {
    const stats = { latest: 0, primary: 0, mirror: 0, requests: [] };
    const server = http.createServer((request, response) => {
        const base = `http://127.0.0.1:${server.address().port}/api`;
        const url = new URL(request.url, base);
        stats.requests.push({ path: request.url, range: request.headers.range || null });
        const catalog = {};
        for (const [id, release] of Object.entries(releases)) {
            catalog[id] = {
                ...release.metadata,
                downloads: [{
                    filename: release.filename,
                    sha256: crypto.createHash('sha256').update(release.archive).digest('hex'),
                    link: `${base}/downloads/${id}/primary.zip`,
                    mirror: `${base}/downloads/${id}/mirror.zip`,
                    ...release.download
                }]
            };
        }
        if (url.pathname === '/api/releases' || url.pathname.startsWith('/api/releases/')) {
            let result;
            if (url.pathname === '/api/releases') {
                result = { releases: catalog };
            } else if (url.pathname === '/api/releases/latest') {
                stats.latest++;
                const ids = Object.keys(catalog).filter(id =>
                    url.searchParams.get('stable') !== 'true' || catalog[id].stable === true).sort();
                result = catalog[ids.pop()];
            } else {
                result = catalog[url.pathname.split('/').pop()];
            }
            const body = metadata === undefined ?
                JSON.stringify(wrapped && result && url.pathname !== '/api/releases' ?
                    { release: result } : result) : metadata;
            setTimeout(() => {
                response.writeHead(result ? metadataStatus : 404,
                    { 'Content-Type': 'application/json' });
                response.end(body);
            }, delayMs);
            return;
        }
        const download = url.pathname.match(/^\/api\/downloads\/(\d{8})\/(primary|mirror)\.zip$/);
        if (download && releases[download[1]]) {
            const release = releases[download[1]];
            const source = download[2];
            stats[source]++;
            const status = release[`${source}Status`] || 200;
            if (status !== 200) {
                response.writeHead(status);
                response.end('fixture download unavailable');
                return;
            }
            const body = release.corrupt ? Buffer.from('checksum mismatch') : release.archive;
            const range = !release.ignoreRange && request.headers.range?.match(/^bytes=(\d+)-$/);
            const start = range ? Number(range[1]) : 0;
            response.writeHead(range ? 206 : 200, {
                'Content-Type': 'application/zip',
                'Content-Length': body.length - start,
                ...(range ? { 'Content-Range': `bytes ${start}-${body.length - 1}/${body.length}` } : {})
            });
            if (release.interruptOnce && source === 'primary' && !range && stats.primary === 1) {
                response.write(body.subarray(0, 8192));
                setTimeout(() => response.destroy(), 20);
                return;
            }
            response.end(body.subarray(start));
            return;
        }
        if (url.pathname === '/stats') {
            response.writeHead(200, { 'Content-Type': 'application/json' });
            response.end(JSON.stringify(stats));
            return;
        }
        response.writeHead(404);
        response.end();
    });
    return { server, stats };
}

module.exports = { zip, createUpdateFixture };

if (require.main === module) {
    const root = process.env.WEBSDR_UPDATE_ROOT;
    const port = Number(process.env.WEBSDR_UPDATE_FIXTURE_PORT || 18074);
    if (!root || !path.isAbsolute(root))
        throw new Error('WEBSDR_UPDATE_ROOT must be an absolute path');

    fs.mkdirSync(path.join(root, 'config'), { recursive: true });
    fs.mkdirSync(path.join(root, 'update'), { recursive: true });
    fs.writeFileSync(path.join(root, 'config', 'sentinel.conf'), 'local configuration\n');
    fs.rmSync(path.join(root, 'update', 'fixture-release.zip'), { force: true });
    fs.rmSync(path.join(root, 'websdr.bin'), { force: true });
    fs.rmSync(path.join(root, 'release-marker.txt'), { force: true });

    const { server } = createUpdateFixture({
        releases: {
            '20990101': {
                metadata: {
                    date: '2099-01-01',
                    changes: ['Fixture release for updater integration testing.']
                },
                filename: 'fixture-release.zip',
                primaryStatus: 503,
                archive: zip([
                    ['websdr.bin', 'fixture binary\n'],
                    ['release-marker.txt', 'fixture release installed\n'],
                    ['config/sentinel.conf', 'release configuration must not replace this\n']
                ])
            }
        }
    });
    server.listen(port, '127.0.0.1');
}
