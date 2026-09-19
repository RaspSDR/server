const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const source = fs.readFileSync('web/extensions/FT8/FT8.js', 'utf8');
const commands = [];
const buttonLabels = [];
const context = {
    console,
    ext_zoom: { ABS: 2 },
    ext_send: command => commands.push(command),
    ext_tune: () => {},
    w3_set_value: () => {},
    w3_el: () => null,
    w3_hide2: () => {},
    w3_innerHTML: (id, text) => {
        if (id === 'id-ft8-test') buttonLabels.push(text);
    },
    w3_add: () => {},
    w3_remove: () => {},
    isNumber: Number.isFinite
};
vm.createContext(context);
vm.runInContext(source, context);

let cleared = 0;
context.ft8_clear_button_cb = () => { cleared++; };
context.ft8_fst4w_test_cb('', 0, false);

assert.strictEqual(cleared, 1);
assert.strictEqual(context.ft8.mode, context.ft8.FST4W_120);
assert.deepStrictEqual(commands, [
    `SET ft8_protocol=${context.ft8.FST4W_120}`,
    'SET ft8_test'
]);

context.ft8_test_state('feeding', 42);
context.ft8_test_state('decoding', 100);
context.ft8_test_state('complete', 100);
assert.deepStrictEqual(buttonLabels, [
    'Syncing...',
    'Feeding: 42%',
    'Decoding...',
    'Test FST4W-120'
]);

const raw = fs.statSync('unix_env/kiwi.config/samples/FST4W-120.raw');
assert.strictEqual(raw.size, 120 * 12000 * 2);
assert.notStrictEqual(fs.readFileSync('unix_env/kiwi.config/samples/FST4W-120.raw', { length: 4 })
    .toString('ascii'), '.snd', 'FST4W test sample must be headerless raw PCM');

console.log('FST4W test button tests passed');
