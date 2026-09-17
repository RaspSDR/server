// Copyright (c) 2026 RaspSDR contributors

var acars = {
   ext_name: 'ACARS',
   first_time: true,
   dataH: 445,
   dataW: 1024,
   ctrlW: 760,
   ctrlH: 160,
   sfmt: 'w3-text-red w3-ext-retain-input-focus',
   pb: { lo: -5000, hi: 5000 },
   stream: '',
   flush_timer: null,
   messages: [],
   max_messages: 200,
   filter: '',
   quality: 0,
   quality_s: [ 'all messages', 'error-free', 'corrected/errors' ],
   view: 0,
   view_s: [ 'message cards', 'raw decoder output' ],
   log_txt: '',
   testing: false,
   freqs: [
      129.125, 130.025, 130.450, 130.825, 131.125, 131.475, 131.525,
      131.550, 131.600, 131.650, 131.725, 131.825, 131.850
   ],
   console_status_msg_p: {
      scroll_only_at_bottom: true,
      process_return_alone: false,
      remove_returns: true,
      cols: 135
   }
};

function ACARS_main()
{
   ext_switch_to_client(acars.ext_name, acars.first_time, acars_recv);
   if (!acars.first_time) acars_controls_setup();
   acars.first_time = false;
}

function acars_recv(data)
{
   if (arrayBufferToStringLen(data, 3) == 'DAT') return;

   var params = arrayBufferToString(data).substring(4).split(' ');
   for (var i = 0; i < params.length; i++) {
      var param = params[i].split('=');
      switch (param[0]) {
         case 'ready':
            acars_controls_setup();
            break;

         case 'chars':
            acars_output_chars(param[1]);
            break;

         case 'decoder':
            acars.in_test_output = (param[1] == 'test');
            acars_status('w3-text-lime', param[1] == 'test'? 'Decoding sample...' : 'Listening');
            break;

         case 'freq':
            acars.current_freq = +param[1];
            acars_update_frequency();
            break;

         case 'test_done':
            acars_flush_stream();
            acars.in_test_output = false;
            acars.sample_until = Date.now() + 2000;
            acars.testing = false;
            w3_remove('id-acars-test', 'w3-disabled');
            acars_status('w3-text-lime', 'Sample complete; listening');
            break;

         case 'error':
            if (acars.testing) {
               acars.testing = false;
               w3_remove('id-acars-test', 'w3-disabled');
            }
            acars_status('w3-text-red', kiwi_decodeURIComponent('', param[1]));
            break;
      }
   }
}

function acars_output_chars(encoded)
{
   var decoded = kiwi_decodeURIComponent('ACARS', encoded);
   acars.log_txt += kiwi_remove_escape_sequences(decoded);
   acars.stream += decoded.replace(/\r/g, '');

   acars.console_status_msg_p.s = encoded;
   kiwi_output_msg('id-acars-console-msgs', 'id-acars-console-msg', acars.console_status_msg_p);

   kiwi_clearTimeout(acars.flush_timer);
   acars.flush_timer = setTimeout(acars_flush_stream, 100);
}

function acars_flush_stream()
{
   kiwi_clearTimeout(acars.flush_timer);
   acars.flush_timer = null;

   var start = acars.stream.indexOf('[#');
   if (start < 0) {
      if (acars.stream.length > 8192) acars.stream = '';
      return;
   }
   if (start > 0) acars.stream = acars.stream.substring(start);

   while (true) {
      var next = acars.stream.indexOf('\n[#', 2);
      if (next < 0) break;
      acars_add_block(acars.stream.substring(0, next));
      acars.stream = acars.stream.substring(next + 1);
   }

   if (acars.stream.indexOf('\n') >= 0) {
      acars_add_block(acars.stream);
      acars.stream = '';
   }
}

function acars_parse_block(block)
{
   block = block.replace(/\r/g, '').trim();
   var lines = block.split('\n');
   var header = lines[0] || '';
   var h = header.match(/^\[#(\d+)\s+\((?:F:([\d.]+)\s+)?L:([-\d.]+)\/([-\d.]+)\s+E:(\d+)\)\s*(.*?)\s*-{4,}/);
   if (!h) return null;

   var msg = {
      channel: +h[1],
      decoder_freq: h[2]? +h[2] : null,
      level: +h[3],
      noise: +h[4],
      errors: +h[5],
      time: h[6].trim(),
      mode: '',
      label: '',
      block_id: '',
      ack: '',
      tail: '',
      flight: '',
      msgno: '',
      text: '',
      raw: block
   };

   var meta = block.match(/Mode\s*:\s*(\S+)\s+Label\s*:\s*(\S+)\s+Id\s*:\s*(\S+)\s+(\S+)/);
   if (meta) {
      msg.mode = meta[1];
      msg.label = meta[2];
      msg.block_id = meta[3];
      msg.ack = meta[4];
   }

   var aircraft = block.match(/Aircraft reg:\s*(\S*)\s+Flight id:\s*(\S*)/);
   if (aircraft) {
      msg.tail = aircraft[1];
      msg.flight = aircraft[2];
   }

   var no_line = -1;
   for (var i = 1; i < lines.length; i++) {
      var no = lines[i].match(/^No:\s*(\S*)/);
      if (no) {
         msg.msgno = no[1];
         no_line = i;
         break;
      }
   }
   if (no_line >= 0)
      msg.text = lines.slice(no_line + 1).join('\n').trim();

   msg.direction = /^[0-9]$/.test(msg.block_id)? 'downlink' :
      (/^[A-Z]$/.test(msg.block_id)? 'uplink' : '');
   return msg;
}

function acars_add_block(block)
{
   var msg = acars_parse_block(block);
   if (!msg) return;
   msg.sample = !!acars.in_test_output || Date.now() < (acars.sample_until || 0);
   msg.freq = msg.sample? null : (acars.current_freq || (+ext_get_freq_kHz() / 1000));
   acars.messages.unshift(msg);
   if (acars.messages.length > acars.max_messages)
      acars.messages.length = acars.max_messages;
   acars_render_messages();
}

function acars_message_visible(msg)
{
   if (acars.quality == 1 && msg.errors != 0) return false;
   if (acars.quality == 2 && msg.errors == 0) return false;
   if (!acars.filter) return true;

   var haystack = [
      msg.tail, msg.flight, msg.label, msg.msgno, msg.text, msg.direction
   ].join(' ').toUpperCase();
   return haystack.indexOf(acars.filter) >= 0;
}

function acars_add_text(parent, class_name, text)
{
   var el = document.createElement('span');
   el.className = class_name;
   el.textContent = text;
   parent.appendChild(el);
   return el;
}

function acars_render_messages()
{
   var container = w3_el('id-acars-messages');
   if (!container) return;
   container.innerHTML = '';

   var visible = 0;
   acars.messages.forEach(function(msg) {
      if (!acars_message_visible(msg)) return;
      visible++;

      var card = document.createElement('article');
      card.className = 'ui-acars-card';

      var header = document.createElement('div');
      header.className = 'ui-acars-card-header';
      acars_add_text(header, 'ui-acars-time', msg.time || 'live');
      acars_add_text(header, 'ui-acars-frequency',
         msg.sample? 'sample audio' : msg.freq.toFixed(3) +' MHz');
      if (msg.direction)
         acars_add_text(header, 'ui-acars-badge ui-acars-'+ msg.direction, msg.direction);
      acars_add_text(header, 'ui-acars-badge', 'label '+ (msg.label || '--'));
      acars_add_text(header, 'ui-acars-badge', 'errors '+ msg.errors);
      card.appendChild(header);

      var identity = document.createElement('div');
      identity.className = 'ui-acars-identity';
      acars_add_text(identity, 'ui-acars-tail', msg.tail || 'unknown aircraft');
      acars_add_text(identity, 'ui-acars-flight', msg.flight? 'flight '+ msg.flight : 'flight --');
      acars_add_text(identity, 'ui-acars-meta',
         'mode '+ (msg.mode || '--') +' | block '+ (msg.block_id || '--') +
         ' | '+ (msg.ack || '--') +' | msg '+ (msg.msgno || '--'));
      card.appendChild(identity);

      var payload = document.createElement('pre');
      payload.className = 'ui-acars-payload';
      payload.textContent = msg.text || '(empty ACARS payload)';
      card.appendChild(payload);
      container.appendChild(card);
   });

   if (!visible) {
      var empty = document.createElement('div');
      empty.className = 'ui-acars-empty';
      empty.textContent = acars.messages.length? 'No messages match the filter.' : 'Waiting for ACARS messages...';
      container.appendChild(empty);
   }
}

function acars_controls_setup()
{
   acars.current_freq = +ext_get_freq_kHz() / 1000;
   var freq_s = acars.freqs.map(function(freq) { return freq.toFixed(3) +' MHz'; });
   acars.freq_i = 0;

   var data_html =
      time_display_html('acars') +
      w3_div('id-acars-data ui-acars-data|width:'+ acars.dataW +'px; height:'+ acars.dataH +'px',
         w3_div('id-acars-messages ui-acars-messages'),
         w3_div('id-acars-console-msg ui-acars-raw w3-hide w3-text-output w3-scroll-down w3-small w3-text-black',
            '<pre><code id="id-acars-console-msgs"></code></pre>'
         )
      );

   var controls_html =
      w3_div('id-acars-controls w3-text-white',
         w3_col_percent('w3-tspace-8 w3-valign/',
            w3_div('w3-medium w3-text-aqua', '<b>VHF ACARS decoder</b>'), 27,
            w3_div('', 'Based on <b><a href="https://github.com/f00b4r0/acarsdec" target="_blank">acarsdec</a></b> (GPLv2)')
         ),
         w3_inline('w3-tspace-6 w3-valign/w3-margin-between-12',
            w3_select(acars.sfmt, 'Frequency', '', 'acars.freq_i', acars.freq_i, freq_s, 'acars_freq_cb'),
            w3_select(acars.sfmt, 'View', '', 'acars.view', acars.view, acars.view_s, 'acars_view_cb'),
            w3_select(acars.sfmt, 'Quality', '', 'acars.quality', acars.quality, acars.quality_s, 'acars_quality_cb'),
            w3_input('w3-ext-retain-input-focus||size=18', 'Filter', 'acars.filter', '', 'acars_filter_cb')
         ),
         w3_inline('w3-tspace-8 w3-valign/w3-margin-between-12',
            w3_button('id-acars-clear w3-padding-smaller w3-css-yellow', 'Clear', 'acars_clear_cb'),
            w3_button('id-acars-log w3-padding-smaller w3-purple', 'Log', 'acars_log_cb'),
            w3_button('id-acars-test w3-padding-smaller w3-aqua', 'Test', 'acars_test_cb'),
            w3_div('id-acars-status w3-text-lime', 'Starting decoder...'),
            w3_div('id-acars-current-frequency w3-text-css-yellow')
         )
      );

   ext_panel_show(controls_html, data_html, null);
   ext_set_data_height(acars.dataH);
   ext_set_controls_width_height(acars.ctrlW, acars.ctrlH);
   time_display_setup('acars');
   acars_render_messages();

   acars.saved_mode = ext_get_mode();
   acars.saved_passband = ext_get_passband();
   ext_set_mode('am');
   ext_set_passband(acars.pb.lo, acars.pb.hi);
   ext_send('SET start');
   acars_update_frequency();

   var param = ext_param();
   if (param) {
      var p = param.split(',');
      var f = p[0].parseFloatWithUnits('kM', 1e-3);
      if (isNumber(f)) acars_tune(f);
      p.forEach(function(s) {
         if (w3_ext_param('test', s).match) acars_test_cb('', 1, false);
         if (w3_ext_param('raw', s).match) acars_view_cb('acars.view', 1, false);
      });
   }
}

function acars_tune(freq_MHz)
{
   acars.current_freq = freq_MHz;
   ext_tune(freq_MHz * 1000, 'am', ext_zoom.CUR);
   ext_set_passband(acars.pb.lo, acars.pb.hi);
   acars_update_frequency();
}

function acars_freq_cb(path, idx, first)
{
   if (first) return;
   acars.freq_i = +idx;
   w3_set_value(path, acars.freq_i);
   acars_tune(acars.freqs[acars.freq_i]);
}

function acars_view_cb(path, idx, first)
{
   if (first) return;
   acars.view = +idx;
   w3_set_value(path, acars.view);
   w3_hide2('id-acars-messages', acars.view == 1);
   w3_hide2('id-acars-console-msg', acars.view == 0);
}

function acars_quality_cb(path, idx, first)
{
   if (first) return;
   acars.quality = +idx;
   w3_set_value(path, acars.quality);
   acars_render_messages();
}

function acars_filter_cb(path, val, first)
{
   if (first) return;
   acars.filter = (val || '').trim().toUpperCase();
   acars_render_messages();
}

function acars_clear_cb(path, val, first)
{
   if (first) return;
   acars.messages = [];
   acars.stream = '';
   acars.log_txt = '';
   acars.console_status_msg_p.s = encodeURIComponent('\f');
   kiwi_output_msg('id-acars-console-msgs', 'id-acars-console-msg', acars.console_status_msg_p);
   acars_render_messages();
}

function acars_log_cb()
{
   var txt = new Blob([acars.log_txt], { type: 'text/plain' });
   var a = document.createElement('a');
   a.style = 'display:none';
   a.href = window.URL.createObjectURL(txt);
   a.download = kiwi_timestamp_filename('ACARS.', '.log.txt');
   document.body.appendChild(a);
   a.click();
   window.URL.revokeObjectURL(a.href);
   document.body.removeChild(a);
}

function acars_test_cb(path, val, first)
{
   if (first || acars.testing) return;
   acars.testing = true;
   w3_add('id-acars-test', 'w3-disabled');
   acars_status('w3-text-css-yellow', 'Starting sample...');
   ext_send('SET test');
}

function acars_status(color, text)
{
   var el = w3_el('id-acars-status');
   if (!el) return;
   el.className = color;
   el.textContent = text;
}

function acars_update_frequency()
{
   var el = w3_el('id-acars-current-frequency');
   if (el) el.textContent = 'Tuned '+ acars.current_freq.toFixed(3) +' MHz AM';
}

function ACARS_environment_changed(changed)
{
   if (changed.freq || changed.mode) {
      acars.current_freq = +ext_get_freq_kHz() / 1000;
      acars_update_frequency();
   }
}

function ACARS_blur()
{
   kiwi_clearTimeout(acars.flush_timer);
   ext_send('SET stop');
   if (isDefined(acars.saved_mode))
      ext_set_mode(acars.saved_mode);
   if (acars.saved_passband)
      ext_set_passband(acars.saved_passband.low, acars.saved_passband.high);
}

function ACARS_help(show)
{
   if (show) {
      var s =
         w3_text('w3-medium w3-bold w3-text-aqua', 'VHF ACARS decoder help') +
         w3_div('w3-margin-T-8',
            'Select an active ACARS channel for your region, then leave the receiver in AM mode. ' +
            'The decoder displays aircraft identity, flight ID, label, block direction, error count, and payload. ' +
            'Use the raw view when troubleshooting weak or unusual messages.' +
            '<br><br>The Test button injects a 12 kHz signed-16-bit PCM sample through the live audio path. ' +
            'URL parameters: <span style="color:orange">frequency &nbsp; raw &nbsp; test</span>.'
         );
      confirmation_show_content(s, 600, 260);
   }
   return true;
}

function ACARS_config_html()
{
   ext_config_html(acars, 'acars', 'ACARS', 'ACARS configuration');
}
