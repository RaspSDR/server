const assert = require('assert');
const fs = require('fs');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

const binaryArgument = process.argv.indexOf('--binary');
if (binaryArgument === -1 || !process.argv[binaryArgument + 1])
    throw new Error('usage: node tests/native_mqtt_test.js --binary <websdr.bin>');

const binary = path.resolve(process.argv[binaryArgument + 1]);
const configPath = path.join(path.dirname(binary), 'config', 'admin.json');
const expectedClientId = '8880000000000001';
const expectedUsername = 'native-user';
const expectedPassword = 'native-password';
const timeoutMs = Number(process.env.WEBSDR_MQTT_TEST_TIMEOUT_MS || 20000);

let serverProcess;
let originalConfig;
let broker;
let output = '';

function decodeRemainingLength(buffer, offset) {
    let multiplier = 1;
    let value = 0;
    let bytes = 0;

    while (offset + bytes < buffer.length && bytes < 4) {
        const byte = buffer[offset + bytes];
        value += (byte & 0x7f) * multiplier;
        bytes++;
        if ((byte & 0x80) === 0)
            return { value, bytes };
        multiplier *= 128;
    }
    return null;
}

function readString(buffer, state) {
    assert(state.offset + 2 <= buffer.length, 'truncated MQTT string length');
    const length = buffer.readUInt16BE(state.offset);
    state.offset += 2;
    assert(state.offset + length <= buffer.length, 'truncated MQTT string');
    const value = buffer.toString('utf8', state.offset, state.offset + length);
    state.offset += length;
    return value;
}

function parseConnect(body) {
    const state = { offset: 0 };
    assert.strictEqual(readString(body, state), 'MQTT');
    assert.strictEqual(body[state.offset++], 4, 'expected MQTT 3.1.1');
    const flags = body[state.offset++];
    state.offset += 2;

    const clientId = readString(body, state);
    if (flags & 0x04) {
        state.offset++;
        state.offset += 2;
        readString(body, state);
        readString(body, state);
    }

    const username = flags & 0x80 ? readString(body, state) : null;
    const password = flags & 0x40 ? readString(body, state) : null;
    return { clientId, username, password, cleanSession: !!(flags & 0x02) };
}

function parsePublish(header, body) {
    const state = { offset: 0 };
    const topic = readString(body, state);
    const qos = (header >> 1) & 0x03;
    if (qos > 0)
        state.offset += 2;
    return { topic, payload: body.toString('utf8', state.offset), qos };
}

function waitForBroker() {
    return new Promise((resolve, reject) => {
        const messages = [];
        let connect;
        let settled = false;

        function finish(error) {
            if (settled)
                return;
            settled = true;
            clearTimeout(timer);
            error ? reject(error) : resolve({ connect, messages });
        }

        const timer = setTimeout(() => {
            finish(new Error(`timed out waiting for MQTT publications\n${output}`));
        }, timeoutMs);

        broker = net.createServer(socket => {
            let pending = Buffer.alloc(0);
            socket.on('data', chunk => {
                try {
                    pending = Buffer.concat([pending, chunk]);
                    while (pending.length >= 2) {
                        const remaining = decodeRemainingLength(pending, 1);
                        if (!remaining)
                            return;
                        const packetLength = 1 + remaining.bytes + remaining.value;
                        if (pending.length < packetLength)
                            return;

                        const header = pending[0];
                        const type = header >> 4;
                        const body = pending.subarray(1 + remaining.bytes, packetLength);
                        pending = pending.subarray(packetLength);

                        if (type === 1) {
                            connect = parseConnect(body);
                            socket.write(Buffer.from([0x20, 0x02, 0x00, 0x00]));
                        } else if (type === 3) {
                            messages.push(parsePublish(header, body));
                            const topics = new Set(messages.map(message => message.topic));
                            if (topics.has(`web888/${expectedClientId}/start`) &&
                                topics.has(`web888/${expectedClientId}/stat`))
                                finish();
                        } else if (type === 12) {
                            socket.write(Buffer.from([0xd0, 0x00]));
                        }
                    }
                } catch (error) {
                    finish(error);
                }
            });
            socket.on('error', finish);
        });
        broker.on('error', finish);
        broker.listen(0, '127.0.0.1', () => {
            const mqttPort = broker.address().port;
            startServer(mqttPort, finish).catch(finish);
        });
    });
}

async function unusedTcpPort() {
    return new Promise((resolve, reject) => {
        const probe = net.createServer();
        probe.on('error', reject);
        probe.listen(0, '127.0.0.1', () => {
            const port = probe.address().port;
            probe.close(error => error ? reject(error) : resolve(port));
        });
    });
}

async function startServer(mqttPort, fail) {
    const httpPort = await unusedTcpPort();
    originalConfig = fs.readFileSync(configPath);
    const config = {
        ...JSON.parse(originalConfig.toString('utf8')),
        mqtt_server: '127.0.0.1',
        mqtt_port: mqttPort,
        mqtt_user: expectedUsername,
        mqtt_password: expectedPassword
    };
    fs.writeFileSync(configPath, `${JSON.stringify(config, null, 2)}\n`);

    serverProcess = spawn(binary, [], {
        env: { ...process.env, WEBSDR_HARNESS_PORT: String(httpPort) },
        stdio: ['ignore', 'pipe', 'pipe']
    });
    serverProcess.stdout.on('data', chunk => {
        output = `${output}${chunk}`.slice(-20000);
    });
    serverProcess.stderr.on('data', chunk => {
        output = `${output}${chunk}`.slice(-20000);
    });
    serverProcess.on('error', fail);
    serverProcess.on('exit', (code, signal) => {
        fail(new Error(
            `native server exited before MQTT verification: code=${code} signal=${signal}\n${output}`));
    });
}

async function cleanup() {
    if (serverProcess && serverProcess.exitCode === null) {
        serverProcess.kill('SIGTERM');
        await new Promise(resolve => {
            const timer = setTimeout(() => {
                if (serverProcess.exitCode === null)
                    serverProcess.kill('SIGKILL');
                resolve();
            }, 2000);
            serverProcess.once('exit', () => {
                clearTimeout(timer);
                resolve();
            });
        });
    }
    if (broker && broker.listening)
        await new Promise(resolve => broker.close(resolve));
    if (originalConfig)
        fs.writeFileSync(configPath, originalConfig);
}

(async () => {
    try {
        const result = await waitForBroker();
        assert.deepStrictEqual(result.connect, {
            clientId: expectedClientId,
            username: expectedUsername,
            password: expectedPassword,
            cleanSession: true
        });

        const start = result.messages.find(message =>
            message.topic === `web888/${expectedClientId}/start`);
        const stat = result.messages.find(message =>
            message.topic === `web888/${expectedClientId}/stat`);
        assert.strictEqual(start.qos, 0);
        assert.strictEqual(stat.qos, 0);

        const startPayload = JSON.parse(start.payload);
        const statPayload = JSON.parse(stat.payload);
        assert.strictEqual(startPayload.server, expectedClientId);
        assert.match(startPayload.timestamp, /^\d+$/);
        assert.deepStrictEqual(Object.keys(startPayload).sort(), ['server', 'timestamp']);
        assert.strictEqual(statPayload.server, expectedClientId);
        assert.strictEqual(statPayload.temp, 25);
        assert.strictEqual(typeof statPayload.cpu, 'number');

        console.log('native MQTT regression test passed');
    } finally {
        await cleanup();
    }
})().catch(error => {
    console.error(error.stack || error);
    process.exitCode = 1;
});
