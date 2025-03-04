// Copyright (c) 2018 John Seamons, ZL4VO/KF6VO

var cw = {
   ext_name: 'CW_skimmer',    // NB: must match cw_skimmer.cpp:cw_skimmer_ext.name
   first_time: true,
   wspace: true,
   test: 0,
   texts: [],

   sig_color_auto: 'black',
   sig_color_pos: 'black',
   sig_color_neg: 'red',

   threshold_dB: 47,
   is_auto_thresh: 0,
   auto_thresh_dB: 0,
   
   auto_wpm: 0,
   fixed_wpm: 0,
   default_wpm: 10,

   // must set "remove_returns" so output lines with \r\n (instead of \n alone) don't produce double spacing
   console_status_msg_p: { scroll_only_at_bottom: true, process_return_alone: false, remove_returns: true, cols: 135 },

   log_mins: 0,
   log_interval: null,

   last_last: 0
};

function CW_skimmer_main()
{
	ext_switch_to_client(cw.ext_name, cw.first_time, cw_skimmer_recv);		// tell server to use us (again)
	if (!cw.first_time)
		cw_skimmer_controls_setup();
	cw.first_time = false;
}

function cw_skimmer_recv(data)
{
	var firstChars = arrayBufferToStringLen(data, 3);
	
	// process data sent from server/C by ext_send_msg_data()
	if (firstChars == "DAT") {
		var ba = new Uint8Array(data, 4);
		var cmd = ba[0];
		var o = 1;
		var len = ba.length-1;

		console.log('cw_skimmer_recv: DATA UNKNOWN cmd='+ cmd +' len='+ len);
		return;
	}
	
	// process command sent from server/C by ext_send_msg() or ext_send_msg_encoded()
	var stringData = arrayBufferToString(data);
	var params = stringData.substring(4).split(" ");

	for (var i=0; i < params.length; i++) {
		var param = params[i].split("=");

		if (0 && param[0] != "keepalive") {
			if (isDefined(param[1]))
				console.log('cw_skimmer_recv: '+ param[0] +'='+ param[1]);
			else
				console.log('cw_skimmer_recv: '+ param[0]);
		}

		switch (param[0]) {

			case "ready":
				cw_skimmer_controls_setup();
				break;

			case "cw_chars":
				cw_skimmer_output_chars(param[1]);
				break;

			case "bar_pct":
			   if (cw.test) {
               var pct = w3_clamp(parseInt(param[1]), 0, 100);
               //if (pct > 0 && pct < 3) pct = 3;    // 0% and 1% look weird on bar
               var el = w3_el('id-cw-bar');
               if (el) el.style.width = pct +'%';
               //console.log('bar_pct='+ pct);
            }
			   break;

			default:
				console.log('cw_skimmer_recv: UNKNOWN CMD '+ param[0]);
				break;
		}
	}
}

function cw_skimmer_output_chars(input)
{
   // split cfreq from input like "a1234" to a and 1234
   input = kiwi_decodeURIComponent('CW', input);
   var msg = {
      text: input.charAt(0),
      freq: parseInt(input.substring(1))
   };

   // Current time
       var now = Date.now();

       // Modify or add a new entry
       var j = cw.texts.findIndex(function(x) { return x.freq >= msg.freq });
       if (j < 0) {
           // Append a new entry
           if (msg.text.trim().length > 0) {
               cw.texts.push({ freq: msg.freq, text: msg.text, ts: now });
           }
       } else if (cw.texts[j].freq == msg.freq) {
           // Update existing entry
           cw.texts[j].text = (cw.texts[j].text + msg.text).slice(-120);
           cw.texts[j].ts   = now;
       } else {
           // Insert a new entry
           if (msg.text.trim().length > 0) {
               cw.texts.splice(j, 0, { freq: msg.freq, text: msg.text, ts: now });
           }
       }
   
       // Generate table body
       var body = '';
       for (var j = 0 ; j < cw.texts.length ; j++) {
           // Limit the lifetime of entries depending on their length
           var cutoff = 5000 * cw.texts[j].text.length;
           if (now - cw.texts[j].ts >= cutoff) {
               cw.texts.splice(j--, 1);
           } else {
               var f = Math.floor(cw.texts[j].freq / 100.0) / 10.0;
               body +=
                   '<tr style="color:black;background-color:' + (j&1? '#E0FFE0':'#FFFFFF') +
                   ';"><td class="freq">' + f.toFixed(1) +
                   '</td><td class="text">' + cw.texts[j].text + '</td></tr>\n';
           }
       }
   
       w3_innerHTML('id-cw-console-table-body', body);		// overwrites last status msg
}

function cw_skimmer_controls_setup()
{
	var p = ext_param();
	if (p) {
      p = p.split(',');
      var do_test = false, do_help = false;
      p.forEach(
         function(a, i) {
            console.log('p'+ i +'='+ a);
            var r;
            if (i == 0 && isNumber(a)) {
               cw.pboff_locked = a;
            } else
            if (w3_ext_param('test', a).match) {
               do_test = true;
            } else
            if (w3_ext_param('help', a).match) {
               do_help = true;
            }
         }
      );
   }
	
   var data_html =
      time_display_html('cw') +
      
      w3_div('id-cw-data|left:150px; width:1044px; height:300px; overflow:hidden; position:relative; background-color:mediumBlue;',
			w3_div('id-cw-console-msg w3-text-output w3-scroll-down w3-small w3-text-black|width:1024px; position:absolute; overflow-x:hidden;',
            '<table width="100%" height="100%">' +
            '<thead><tr>' +
                '<th class="freq">Freq</th>' +
                '<th class="text">Text</th>' +
            '</tr></thead>' +
            '<tbody id="id-cw-console-table-body"></tbody>' +
            '</table>'
			)
      );

	var controls_html =
		w3_div('id-cw-controls w3-text-white',
			w3_divs('',
            w3_col_percent('',
               w3_div('',
				      w3_div('w3-medium w3-text-aqua', '<b>CW Skimmer</b>')
				   ), 32,
					w3_div('', 'Luarvique, <br> <b><a href="https://github.com/luarvique/csdr-cwskimmer" target="_blank">CSDR Based CW Skimmer</a></b> &copy; 2022'
					), 50
				),
            
				w3_inline('w3-margin-T-10/w3-margin-between-16',
               w3_div('',
                  w3_inline('',
                     w3_button('id-cw-test w3-padding-smaller w3-green', 'Test', 'cw_test_cb', 0),
                     w3_div('id-cw-bar-container w3-margin-L-16 w3-progress-container w3-round-large w3-white w3-hide|width:70px; height:16px',
                        w3_div('id-cw-bar w3-progressbar w3-round-large w3-light-green|width:0%', '&nbsp;')
                     )
                  ),
                  w3_inline('',
                     w3_button('w3-margin-T-8 w3-padding-smaller', 'Clear', 'cw_clear_button_cb', 0),
                  )
               )
				)
			)
		);
	
	cw.saved_setup = ext_save_setup();
	ext_set_mode('cw');
	ext_panel_show(controls_html, data_html, null);
	time_display_setup('cw');

   ext_set_data_height(300);
	ext_set_controls_width_height(380, 200);
	
   ext_send('SET cw_start');

   cw_clear_button_cb();
	if (do_test) cw_test_cb();
	if (do_help) extint_help_click();
}

function cw_clear_button_cb(path, idx, first)
{
   if (first) return;
   cw.console_status_msg_p.s = encodeURIComponent('\f');
   cw.texts = [];
   w3_innerHTML('id-cw-console-table-body', '');
}

function cw_log_mins_cb(path, val)
{
   cw.log_mins = w3_clamp(+val, 0, 24*60, 0);
   console.log('cw_log_mins_cb path='+ path +' val='+ val +' log_mins='+ cw.log_mins);
	w3_set_value(path, cw.log_mins);

   kiwi_clearInterval(cw.log_interval);
   if (cw.log_mins != 0) {
      console.log('CW logging..');
      cw.log_interval = setInterval(function() { cw_log_cb(); }, cw.log_mins * 60000);
   }
}

/*
function cw_log_cb()
{
   for (var j = 0 ; j < cw.texts.length ; j++) {
      cw.log_txt += kiwi_remove_escape_sequences(kiwi_decodeURIComponent('CW', cw.texts[j].text));
      cw.log_txt += "\n";
   }

   var txt = new Blob([cw.log_txt], { type: 'text/plain' });
   var a = document.createElement('a');
   a.style = 'display: none';
   a.href = window.URL.createObjectURL(txt);
   a.download = kiwi_timestamp_filename('CW.', '.log.txt');
   document.body.appendChild(a);
   console.log('cw_log: '+ a.download);
   a.click();
   window.URL.revokeObjectURL(a.href);
   document.body.removeChild(a);
}
*/

function cw_test_cb()
{
   console_nv('cw_test_cb', 'cw.test');
   cw.test = cw.test? 0:1;    // if already running stop if clicked again
   w3_colors('id-cw-test', '', 'w3-red', cw.test);
	w3_el('id-cw-bar').style.width = '0%';
   w3_show_hide('id-cw-bar-container', cw.test);
   ext_send('SET cw_test='+ cw.test);
}

function CW_skimmer_blur()
{
	ext_set_data_height();     // restore default height
	ext_send('SET cw_stop');
   kiwi_clearInterval(cw.log_interval);
	ext_restore_setup(cw.saved_setup);
}

// called to display HTML for configuration parameters in admin interface
function CW_skimmer_config_html()
{
   ext_config_html(cw, 'cw', 'CW', 'CW decoder configuration');
}

function CW_skimmer_help(show)
{
   if (show) {
      var s = 
         w3_text('w3-medium w3-bold w3-text-aqua', 'CW Skimmer help') +
         w3_div('w3-margin-T-8 w3-scroll-y|height:90%',
            w3_div('w3-margin-R-8',
               'Skimmer is automatically decode the multi cw stream in the 12K bandwidth.'
            )
         );
      confirmation_show_content(s, 600, 350);
      w3_el('id-confirmation-container').style.height = '100%';   // to get the w3-scroll-y above to work
   }
   return true;
}
