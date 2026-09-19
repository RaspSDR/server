const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const source = fs.readFileSync('web/extensions/ACARS/ACARS.js', 'utf8');
const context = {
    console,
    setTimeout,
    clearTimeout
};
vm.createContext(context);
vm.runInContext(source, context);

const decoded = context.acars_parse_block(
`[#1 (L:-10.9/-165.6 E:0)  --------------------------------
Mode : 2 Label : B9 Id : 2 Nak
Aircraft reg: B-18722 Flight id: CI5118
No: L05A
/KLAX.TI2/024KLAXA91A1
`);

assert(decoded);
assert.strictEqual(decoded.channel, 1);
assert.strictEqual(decoded.level, -10.9);
assert.strictEqual(decoded.noise, -165.6);
assert.strictEqual(decoded.errors, 0);
assert.strictEqual(decoded.mode, '2');
assert.strictEqual(decoded.label, 'B9');
assert.strictEqual(decoded.block_id, '2');
assert.strictEqual(decoded.direction, 'downlink');
assert.strictEqual(decoded.tail, 'B-18722');
assert.strictEqual(decoded.flight, 'CI5118');
assert.strictEqual(decoded.msgno, 'L05A');
assert.strictEqual(decoded.text, '/KLAX.TI2/024KLAXA91A1');

const live = context.acars_parse_block(
`[#2 (F:131.825 L:-22.4/-48.1 E:3) 17/09/2026 09:35:12.123 --------------------------------
Mode : 2 Label : Q0 Id : A Ack
Aircraft reg: N123AB Flight id: UA0123
No: M01A
POSITION REPORT
SECOND LINE
`);

assert(live);
assert.strictEqual(live.decoder_freq, 131.825);
assert.strictEqual(live.errors, 3);
assert.strictEqual(live.time, '17/09/2026 09:35:12.123');
assert.strictEqual(live.direction, 'uplink');
assert.strictEqual(live.text, 'POSITION REPORT\nSECOND LINE');

context.acars.stream =
`[#1 (L:-10.9/-165.6 E:0)  --------------------------------
Mode : 2 Label : B9 Id : 2 Nak
`;
assert.strictEqual(context.acars_take_blocks(false).length, 0);

context.acars.stream +=
`Aircraft reg: B-18722 Flight id: CI5118
No: L05A
FIRST MESSAGE
[#2 (L:-12.0/-166.0 E:0)  --------------------------------
`;
const complete = context.acars_take_blocks(false);
assert.strictEqual(complete.length, 1);
assert.strictEqual(context.acars_parse_block(complete[0]).text, 'FIRST MESSAGE');

context.acars.stream +=
`Mode : 2 Label : B9 Id : 2 Nak
Aircraft reg: B-18723 Flight id: CI5119
No: L05B
SECOND MESSAGE
`;
const final = context.acars_take_blocks(true);
assert.strictEqual(final.length, 1);
assert.strictEqual(context.acars_parse_block(final[0]).text, 'SECOND MESSAGE');

console.log('ACARS parser tests passed');
