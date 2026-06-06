// CTC (Chinese Telegraph Code) translation - common module
// Used by FSK and NAVTEX extensions

var CTC = {
   // Process a string through CTC translation, outputting via the provided callback
   // state: object with { buffer, in_bracket, last_was_letter, invalid_num }
   // output_fn: function(s) to output translated/raw characters
   process: function(s, state, output_fn) {
      for (var i = 0; i < s.length; i++) {
         var ch = s[i];

         if (ch === '(' || ch === '\uff08' || ch === '[' || ch === '\u3010') {
            state.in_bracket = true;
         }
         if (ch === ')' || ch === '\uff09' || ch === ']' || ch === '\u3011') {
            state.in_bracket = false;
         }

         if (/[0-9]/.test(ch)) {
            if (state.in_bracket) {
               output_fn(ch);
            } else {
               if (state.buffer === '' && state.last_was_letter) {
                  state.invalid_num = true;
               }
               state.buffer += ch;
            }
            state.last_was_letter = false;
         } else {
            if (state.buffer.length > 0) {
               if (/[a-zA-Z]/.test(ch)) state.invalid_num = true;

               if (!state.in_bracket && !state.invalid_num &&
                   state.buffer.length === 4 && window.CTC_DICT && window.CTC_DICT[state.buffer]) {
                  output_fn(window.CTC_DICT[state.buffer]);
               } else {
                  output_fn(state.buffer);
               }
               state.buffer = '';
               state.invalid_num = false;
            }

            state.last_was_letter = /[a-zA-Z]/.test(ch);
            output_fn(ch);
         }
      }
   },

   // Create a fresh state object
   new_state: function() {
      return { buffer: '', in_bracket: false, last_was_letter: false, invalid_num: false };
   },

   // Reset an existing state object
   reset: function(state) {
      state.buffer = '';
      state.in_bracket = false;
      state.last_was_letter = false;
      state.invalid_num = false;
   }
};
