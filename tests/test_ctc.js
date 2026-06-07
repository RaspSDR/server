#!/usr/bin/env node
// Test CTC (Chinese Telegraph Code) common module
// Run: node tests/test_ctc.js

var fs = require('fs');
var path = require('path');

// Setup minimal browser-like environment
global.window = {};

var base = path.join(__dirname, '..', 'web', 'extensions', 'FSK');
eval(fs.readFileSync(path.join(base, 'CTC_dict.js'), 'utf8'));
eval(fs.readFileSync(path.join(base, 'CTC.js'), 'utf8'));

var passed = 0;
var failed = 0;

function assert_eq(test_name, input, expected) {
   var state = CTC.new_state();
   var output = '';
   CTC.process(input, state, function(s) { output += s; });
   if (output === expected) {
      console.log('  \x1b[32mPASS\x1b[0m ' + test_name);
      passed++;
   } else {
      console.log('  \x1b[31mFAIL\x1b[0m ' + test_name);
      console.log('       input:    "' + input + '"');
      console.log('       expected: "' + expected + '"');
      console.log('       actual:   "' + output + '"');
      failed++;
   }
}

console.log('CTC module tests\n');

// Basic translation
assert_eq('4-digit code translates to Chinese', '0001 ', '一 ');
assert_eq('known code 1004 = 城', '1004 ', '城 ');
assert_eq('multiple codes', '0001,1004 ', '一,城 ');

// Bracket passthrough
assert_eq('digits in () stay raw', '(1234)', '(1234)');
assert_eq('digits in fullwidth () stay raw', '\uff081234\uff09', '\uff081234\uff09');
assert_eq('digits in [] stay raw', '[5678]', '[5678]');

// Letter-adjacent numbers stay raw
assert_eq('letter before digits keeps raw', 'A1234 ', 'A1234 ');
assert_eq('digits followed by letter keeps raw', '1234B', '1234B');

// Non-4-digit sequences stay raw
assert_eq('3 digits stay raw', '123 ', '123 ');
assert_eq('5 digits stay raw', '12345 ', '12345 ');

// Unknown 4-digit code stays raw
assert_eq('unknown code 9999 stays raw', '9999 ', '9999 ');

// Mixed content
assert_eq('text with embedded code', 'hello0001world', 'hello0001world');
assert_eq('code after newline', '\n0001 ', '\n一 ');

// Reset works
(function() {
   var state = CTC.new_state();
   CTC.process('12', state, function() {});  // partial buffer
   CTC.reset(state);
   if (state.buffer === '' && !state.in_bracket && !state.last_was_letter && !state.invalid_num) {
      console.log('  \x1b[32mPASS\x1b[0m reset clears all state');
      passed++;
   } else {
      console.log('  \x1b[31mFAIL\x1b[0m reset clears all state');
      failed++;
   }
})();

// new_state returns clean state
(function() {
   var state = CTC.new_state();
   if (state.buffer === '' && !state.in_bracket && !state.last_was_letter && !state.invalid_num) {
      console.log('  \x1b[32mPASS\x1b[0m new_state returns clean state');
      passed++;
   } else {
      console.log('  \x1b[31mFAIL\x1b[0m new_state returns clean state');
      failed++;
   }
})();

console.log('\nResults: ' + passed + ' passed, ' + failed + ' failed');
process.exit(failed > 0 ? 1 : 0);
