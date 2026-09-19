const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

function loadExtension(path) {
    const context = {
        console,
        setTimeout,
        clearTimeout,
        setInterval,
        clearInterval,
        kiwi: { freq_offset_kHz: 100761.6 },
        ext_zoom: { CUR: 4 },
        isNumber: Number.isFinite
    };
    vm.createContext(context);
    vm.runInContext(fs.readFileSync(path, 'utf8'), context);
    return context;
}

const acars = loadExtension('web/extensions/ACARS/ACARS.js');
let acarsTune;
acars.ext_tune = (...args) => { acarsTune = args; };
acars.ext_set_passband = () => {};
acars.acars_update_frequency = () => {};
acars.acars_tune(129.125);
assert(Math.abs(acarsTune[0] - 28363.4) < 1e-6);
assert.deepStrictEqual(acarsTune.slice(1, 3), ['am', 4]);

const ais = loadExtension('web/extensions/AIS/AIS.js');
let aisTune;
ais.ext_tune = (...args) => { aisTune = args; };
ais.ext_set_passband = () => {};
ais.ais_update_status = () => {};
ais.ais_tune(161.975);
assert(Math.abs(aisTune[0] - 61213.4) < 1e-6);
assert.deepStrictEqual(aisTune.slice(1, 3), ['nnfm', 4]);
assert.strictEqual(ais.ais_select_frequency('162.025'), 162.025);
assert.strictEqual(ais.ais.freq_i, 1);

const dx = JSON.parse(fs.readFileSync('unix_env/kiwi.config/dist.dx.json', 'utf8')).dx;
const expected = [
    ['ACARS', 129.125], ['ACARS', 130.025], ['ACARS', 130.450],
    ['ACARS', 130.825], ['ACARS', 131.125], ['ACARS', 131.475],
    ['ACARS', 131.525], ['ACARS', 131.550], ['ACARS', 131.600],
    ['ACARS', 131.650], ['ACARS', 131.725], ['ACARS', 131.825],
    ['ACARS', 131.850], ['AIS', 161.975], ['AIS', 162.025]
];

for (const [extension, frequency] of expected) {
    const label = dx.find(entry => Math.abs(entry[0] - frequency * 1000) < 1e-6);
    assert(label, `missing ${extension} label at ${frequency.toFixed(3)} MHz`);
    assert(label[2].includes(extension),
        `label name missing extension for ${frequency.toFixed(3)} MHz`);
    assert.strictEqual(label[4].p, `${extension},${frequency.toFixed(3)}`);
}

console.log('extension frequency offset tests passed');
