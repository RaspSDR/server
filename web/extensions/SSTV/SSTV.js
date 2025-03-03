// Copyright (c) 2019 John Seamons, ZL4VO/KF6VO

var sstv = {
   ext_name: 'SSTV',    // NB: must match example.c:example_ext.name
   first_time: true,
   
   // marginL | (pg_sp+iw) | (pg_sp+iw) | (pg_sp+iw)
   //
   //                  |768
   // 22 64+320 64+320 64+320
   //    64+496    |560
   //    64+800          |864       // PD290
   
   w: 3*(64+320) - 64,
   h: 256,
   iw: 320,
   isp: 64,
   iws: 320+64,
   page: 0,
   pw: [320, 320, 320],
   ph: [256, 256, 256],
   pg_sp: 64,
   marginL: 22,
   startx: 22+64,
   tw: 0,
   data_canvas: 0,
   image_y: 0,
   shift_second: false,
   auto: true,
   max_height: 256,
   
   CMD_DRAW: 0,
   CMD_REDRAW: 1,
   
   //en.wikipedia.org/wiki/Slow-scan_television#Frequencies
   freqs_s: [ '3630_ANZ', '3730_EU', '3845_NA', 7171, 7180, 14230, 14233, 21340, 28680 ]
};

function SSTV_main()
{
	ext_switch_to_client(sstv.ext_name, sstv.first_time, sstv_recv);		// tell server to use us (again)
	if (!sstv.first_time)
		sstv_controls_setup();
	sstv.first_time = false;
}

function sstv_recv(data)
{
   var i;
   var canvas = sstv.data_canvas;
   var ct = canvas.ctx;
	var firstChars = arrayBufferToStringLen(data, 3);
	
	// process data sent from server/C by ext_send_msg_data()
	if (firstChars == "DAT") {
		var ba = new Uint8Array(data, 4);
		var cmd = ba[0];
		var snr = ba[1];
		var width = (ba[2] << 8) + ba[3];
		var height = (ba[4] << 8) + ba[5];
		var o = 6;
		var len = ba.length-1;
		
      if (cmd == sstv.CMD_DRAW || cmd == sstv.CMD_REDRAW) {
         var imd = canvas.imd;
         for (i = 0; i < width; i++) {
            imd.data[i*4+0] = ba[o];
            imd.data[i*4+1] = ba[o+1];
            imd.data[i*4+2] = ba[o+2];
            imd.data[i*4+3] = 0xff;
            o += 3;
         }
         var x;
         if (sstv.image_y < sstv.h) {
            sstv.image_y++;
         } else {
            x = Math.round(sstv.startx + (sstv.page * sstv.iws));
            ct.drawImage(canvas, x,1,width,sstv.h-1, x,0,width,sstv.h-1);  // scroll up
         }
         x = Math.round(sstv.startx + (sstv.page * sstv.iws));
         ct.putImageData(imd, x, sstv.image_y-1, 0,0,width,1);
         if (cmd == sstv.CMD_DRAW)
            sstv_status_cb('line '+ sstv.line +', SNR '+ ((snr-128).toFixed(0)) +' dB');
         //console.log('SSTV line '+ sstv.line);
         sstv.line++;
      } else
		   console.log('sstv_recv: DATA UNKNOWN cmd='+ cmd +' len='+ len);
		
		return;
	}
	
	// process command sent from server/C by ext_send_msg() or ext_send_msg_encoded()
	var stringData = arrayBufferToString(data);
	var params = stringData.substring(4).split(" ");

	for (i=0; i < params.length; i++) {
		var param = params[i].split("=");

		if (0 && param[0] != "keepalive") {
			if (isDefined(param[1]))
				console.log('sstv_recv: '+ param[0] +'='+ param[1]);
			else
				console.log('sstv_recv: '+ param[0]);
		}

		switch (param[0]) {

			case "ready":
				sstv_controls_setup();
				break;

         case "img_size":
            var a = param[1].split(',');
            var w = +a[0];
            var h = +a[1];
            console.log('SSTV img_size='+ w +'x'+ h);
            sstv.iw = w;
            sstv.iws = w + sstv.isp;
            sstv.h = h;
            sstv.cur_w = w;
            sstv.cur_h = h;

            // height expansion (only grows, never shrinks)
            if (h > sstv.max_height) {
               sstv.max_height = h;
               w3_el('id-sstv-data').style.height = px(h);
               w3_el('id-sstv-data-canvas').height = h;
               ext_set_data_height(h);

              // NB: when canvas height changed color resets to black, so refill entire canvas
               ct.fillStyle = 'dimGray';
               ct.fillRect(sstv.marginL,0, sstv.w+sstv.isp,h);
            }
            
            break;

			case "new_img":
			   var mode_name = decodeURIComponent(param[1]);
			   sstv_clear_display(mode_name);
				break;

			case "redraw":
            sstv.image_y = 0;
				break;

			case "mode_name":
			   sstv.mode_name = decodeURIComponent(param[1]);
				//console.log('mode_name='+ sstv.mode_name);
				sstv_mode_name_cb(sstv.mode_name);
				sstv.line = 1;
				break;

			case "status":
			   sstv.status = decodeURIComponent(param[1]);
				//console.log('status='+ sstv.status);
				sstv_status_cb(sstv.status);
				break;

			case "result":
			   sstv.result = decodeURIComponent(param[1]);
				//console.log('result='+ sstv.result);
				sstv_result_cb(sstv.result);
				break;

			case "fsk_id":
			   sstv.fsk_id = decodeURIComponent(param[1]);
				//console.log('fsk_id='+ sstv.fsk_id);
				sstv_fsk_id_cb(sstv.fsk_id);
				break;

			default:
				console.log('sstv_recv: UNKNOWN CMD '+ param[0]);
				break;
		}
	}
}

function sstv_clear_display(mode_name)
{
   var ct = sstv.data_canvas.ctx;
   var x = sstv.startx + (sstv.page * sstv.iws);
   //var y = sstv.h/2;
   var y = 256/2;
   var ts = 24;
   var isp = sstv.isp;

   // clear previous page marker
   if (sstv.page != -1) {
      ct.fillStyle = 'dimGray';
      ct.fillRect(x-ts,y-ts/2, ts,ts);
   }

   console.log('SSTV PRE page='+ sstv.page +' cur_w|h='+ sstv.cur_w +'|'+ sstv.cur_h +' pw|h='+ sstv.pw[sstv.page] +'|'+ sstv.ph[sstv.page]);
   if (sstv.page == 0 && sstv.pw[sstv.page] != 320 && sstv.cur_w == 320) {
      sstv.page = 2;
   } else
   if (sstv.cur_w > 320) {
      sstv.page = 0;
   } else {
      sstv.page = (sstv.page+1) % 3;
   }
   console.log('SSTV POST page='+ sstv.page);
   x = sstv.startx + (sstv.page * sstv.iws);

   // clear previous page
   if (sstv.page != -1) {
      ct.fillStyle = 'dimGray';
      var pages = (sstv.pw[sstv.page] > 320 || sstv.cur_w > 320)? 2:1;
      var w = pages * (64+320);
      console.log('SSTV CLR pages='+ pages +' x='+ (x-64) +' w='+ w);
      ct.fillRect(x-64,0, w,sstv.ph[sstv.page]);
   }
   sstv.pw[sstv.page] = sstv.cur_w;
   sstv.ph[sstv.page] = sstv.cur_h;

   // draw current page marker
   ct.fillStyle = 'yellow';
   ct.beginPath();
   ct.moveTo(x-ts, y-ts/2);
   ct.lineTo(x, y);
   ct.lineTo(x-ts, y+ts/2);
   ct.closePath();
   ct.fill();
   
   // draw mode text
   ct.font = '16px Arial';
   ct.fillStyle = 'yellow';
   var tx = x - ct.measureText(mode_name).width -4;
   ct.fillText(mode_name, tx, y - ts);

   // background box
   ct.fillStyle = 'lightCyan';
   ct.fillRect(x,0, sstv.iw,sstv.h);
   sstv.image_y = 0;
   sstv.shift_second = false;
}

function sstv_controls_setup()
{
   if (kiwi_isMobile()) sstv.startx = 0;
   sstv.tw = sstv.w + sstv.startx;

   var data_html =
      time_display_html('sstv') +

      w3_div('id-sstv-data|left:0; width:'+ px(sstv.tw) +'; height:'+ px(sstv.h) +'; background-color:black; position:relative;',
   		'<canvas id="id-sstv-data-canvas" class="w3-crosshair" width='+ dq(sstv.tw)+' style="position:absolute;"></canvas>'
      );

	var controls_html =
		w3_div('id-test-controls w3-text-white',
			w3_divs('/w3-tspace-8',
            w3_col_percent('',
				   w3_div('w3-medium w3-text-aqua', '<b>SSTV decoder</b>'), 30,
					w3_div('', 'From <b><a href="http://windytan.github.io/slowrx" target="_blank">slowrx</a></b> by Oona Räisänen, OH2EIQ')
				),
				w3_inline('',
               w3_select('id-sstv-freq-menu w3-text-red', '', 'freq', 'sstv.freq', W3_SELECT_SHOW_TITLE, sstv.freqs_s, 'sstv_freq_cb'),
               w3_checkbox('id-sstv-cbox-auto w3-margin-left w3-label-inline w3-label-not-bold', 'auto adjust', 'sstv.auto', true, 'sstv_auto_cbox_cb'),
				   w3_button('id-sstv-btn-auto w3-margin-left w3-padding-smaller', 'Undo adjust', 'sstv_auto_cb'),
				   w3_button('w3-margin-left w3-padding-smaller w3-css-yellow', 'Reset', 'sstv_reset_cb'),
				   w3_button('w3-margin-left w3-padding-smaller w3-blue', 'Save images', 'sstv_save_cb'),
				   w3_button('w3-margin-left w3-padding-smaller w3-aqua', 'Test', 'sstv_test_cb', 0),
				   dbgUs?
				         w3_button('w3-margin-L-8 w3-padding-smaller w3-aqua', 'T2', 'sstv_test_cb', 1)
				      :
				         ''
				),
            w3_half('', '',
               w3_div('id-sstv-mode-name'),
               w3_div('id-sstv-fsk-id')
            ),
            w3_div('id-sstv-status'),
            w3_div('id-sstv-result w3-hide')
			)
		);

	ext_panel_show(controls_html, data_html, null);
	ext_set_controls_width_height(560, 125);
	sstv.saved_setup = ext_save_setup();
	sstv_mode_name_cb("");
	sstv_status_cb("");
	sstv_result_cb("");
	sstv_fsk_id_cb("");
   time_display_setup('sstv');

	sstv.data_canvas = w3_el('id-sstv-data-canvas');
	sstv.data_canvas.ctx = sstv.data_canvas.getContext("2d");
	sstv.data_canvas.imd = sstv.data_canvas.ctx.createImageData(sstv.w, 1);
	sstv.data_canvas.addEventListener("mousedown", sstv_mousedown, false);
	if (kiwi_isMobile())
		sstv.data_canvas.addEventListener('touchstart', sstv_touchstart, false);
   ext_set_data_height(sstv.h);
   sstv.data_canvas.height = sstv.h;

   var ct = sstv.data_canvas.ctx;
   ct.fillStyle = 'black';
   ct.fillRect(0,0, sstv.tw,sstv.h);
   ct.fillStyle = 'dimGray';
   ct.fillRect(sstv.marginL,0, sstv.w+sstv.isp,sstv.h);
   sstv.page = -1;
   sstv.shift_second = false;

	ext_send('SET start');

   var p = ext_param();
   if (p) {
      p = p.split(',');
      var found = false;
      for (var i=0, len = p.length; i < len; i++) {
         var a = p[i];
         //console.log('SSTV: param <'+ a +'>');
         if (w3_ext_param('help', a).match) {
            ext_help_click();
         } else
         if (w3_ext_param('mmsstv', a).match) {
            ext_send('SET mmsstv');
         } else
         if (w3_ext_param('test', a).match) {
            sstv_test_cb(null, 0);
         } else
         if (w3_ext_param('noadj', a).match) {
            sstv.auto = 1;    // gets inverted by sstv_auto_cbox_cb()
            sstv_auto_cbox_cb('id-sstv-cbox-auto', false);
         } else {
            w3_select_enum('id-sstv-freq-menu', function(option, idx) {
               //console.log('CONSIDER idx='+ idx +' <'+ option.innerHTML +'>');
               if (!found && option.innerHTML.startsWith(a)) {
                  w3_select_value('id-sstv-freq-menu', idx-1);
                  sstv_freq_cb('', idx-1, false);
                  found = true;
               }
            });
         }
      }
   }
}

function sstv_auto_cbox_cb(path, checked, first)
{
   if (first) return;
   w3_checkbox_set(path, checked);
   w3_innerHTML('id-sstv-btn-auto', checked? 'Undo adjust' : 'Auto adjust');
   sstv.auto = sstv.auto? false:true;
   ext_send('SET noadj='+ (sstv.auto? 0:1));
}

function sstv_auto_cb(path, val, first)
{
   //console.log('SET '+ (sstv.auto? 'undo':'auto'));
   ext_send('SET '+ (sstv.auto? 'undo':'auto'));
}

function sstv_freq_cb(path, idx, first)
{
   if (first) return;
   idx = +idx;
   var f = parseInt(sstv.freqs_s[idx]);
   //console.log('SSTV f='+ f);
   if (isNaN(f)) return;
   var lsb = (f < 10000);
   ext_tune(f, lsb? 'lsb':'usb', ext_zoom.ABS, 9);
   var margin = 200;
   var lo = lsb? -2300 : 1200;
   var hi = lsb? -1200 : 2300;
   ext_set_passband(lo - margin, hi + margin);
}

function sstv_mousedown(evt)
{
   sstv_shift(evt, true);
}

function sstv_touchstart(evt)
{
   sstv_shift(evt, false);
}

function sstv_shift(evt, requireShiftKey)
{
	var x = (evt.clientX? evt.clientX : (evt.offsetX? evt.offsetX : evt.layerX));
	var y = (evt.clientY? evt.clientY : (evt.offsetY? evt.offsetY : evt.layerY));
	x -= sstv.startx;
	//console.log('sstv_shift xy '+ x +' '+ y);
	if (x < 0 || sstv.page == -1) { sstv.shift_second = false; return; }
	var xmin = sstv.page * sstv.iws;
	var xmax = xmin + sstv.iw;
	if (x < xmin || x > xmax) { sstv.shift_second = false; return; }
	x -= xmin;
	if (!sstv.shift_second) {
	   sstv.x0 = x; sstv.y0 = y;
	   sstv.shift_second = true;
	   return;
	}
   sstv.shift_second = false;
	//console.log('sstv_shift page='+ sstv.page +' xy '+ sstv.x0 +','+ sstv.y0 +' -> '+ x +','+ y);
	ext_send('SET shift x0='+ sstv.x0 +' y0='+ sstv.y0 +' x1='+ x +' y1='+ y);
}

function sstv_mode_name_cb(mode_name)
{
	w3_el('id-sstv-mode-name').innerHTML = 'Mode: ' +
	   w3_div('w3-show-inline-block w3-text-black w3-background-pale-aqua w3-padding-LR-8', mode_name);
}

function sstv_status_cb(status)
{
	w3_el('id-sstv-status').innerHTML = 'Status: ' +
	   w3_div('w3-show-inline-block w3-text-black w3-background-pale-aqua w3-padding-LR-8', status);
}

function sstv_result_cb(result)
{
	w3_el('id-sstv-result').innerHTML = 'Result: ' +
	   w3_div('w3-show-inline-block w3-text-black w3-background-pale-aqua w3-padding-LR-8', result);
}

function sstv_fsk_id_cb(fsk_id)
{
	w3_el('id-sstv-fsk-id').innerHTML = 'FSK ID: ' +
	   w3_div('w3-show-inline-block w3-text-black w3-background-pale-aqua w3-padding-LR-8', fsk_id);
}

function sstv_reset_cb(path, val, first)
{
	sstv_mode_name_cb("");
	sstv_status_cb("");
	sstv_result_cb("");
	sstv_fsk_id_cb("");
	ext_send('SET reset');
}

function sstv_test_cb(path, val, first)
{
   // mode_name & status fields set in cpp code
	sstv_result_cb("");
	sstv_fsk_id_cb("");
	console.log('sstv_test_cb '+ val);
	ext_send('SET test='+ val);
}

function sstv_save_cb(path, val, first)
{
   if (first) return;
   var imgURL = sstv.data_canvas.toDataURL("image/jpeg", 0.85);
   var dlLink = document.createElement('a');
   dlLink.download = kiwi_timestamp_filename('SSTV.', '.jpg');
   dlLink.href = imgURL;
   dlLink.target = '_blank';  // opens new tab in iOS instead of download
   dlLink.dataset.downloadurl = ["image/jpeg", dlLink.download, dlLink.href].join(':');
   document.body.appendChild(dlLink);
   dlLink.click();
   document.body.removeChild(dlLink);
}

function SSTV_blur()
{
	//console.log('### SSTV_blur');
	ext_send('SET stop');
	ext_restore_setup(sstv.saved_setup);
}

function SSTV_help(show)
{
   if (show) {
      var s = 
         w3_text('w3-medium w3-bold w3-text-aqua', 'SSTV decoder help') +
         w3_div('w3-margin-T-8 w3-scroll-y|height:90%',
            w3_div('w3-margin-R-8',
               'Select an entry from the SSTV freq menu and wait for a signal to begin decoding.<br>' +
               'Sometimes activity is +/- the given frequencies. Try the "test" button.<br>' +
               '<br>Supported modes:' +
               '<ul>' +
                  '<li>Martin: M1 M2 M3 M4</li>' +
                  '<li>Scottie: S1 S2 SDX</li>' +
                  '<li>Robot: R12 R24 R36 R72 R8-BW R12-BW R24-BW</li>' +
                  '<li>Wraase: SC60 SC120 SC180</li>' +
                  '<li>Pasokon: P3 P5 P7</li>' +
                  '<li>PD: PD50 PD90 PD120 PD160 PD180 PD240</li>' +
                  '<li>MMSSTV: MR73 MR90 MR115 MR140 MR175 MP73 MP115 MP140 MP175</li>' +
                  '<li>FAX480</li>' +
               '</ul>' +
               'Unsupported modes:' +
               '<ul>' +
                  '<li>PD: PD290</li>' +
                  '<li>MMSSTV: MN73 MN110 MN140 MC110 MC140 MC180</li>' +
                  '<li>Amiga: AVT24 AVT90 AVT94</li>' +
               '</ul>' +
               'If the image is still slanted or offset after auto adjustment you can make a manual<br>' +
               'correction. If you see what looks like an edge in the image then click in two places along<br>' +
               'the edge. The image will then auto adjust. You can repeat this procedure multiple times<br>' +
               'if necessary.' +
               '<br><br>URL parameters: <br>' +
               w3_text('|color:orange', '(freq menu match) &nbsp; noadj &nbsp; test') +
               '<br><br>' +
               'The first URL parameter can be the number from an entry of the freq menu (e.g. "3730"). ' +
               '<i>noadj</i> un-checks the <i>auto adjust</i> box. <br>' +
               '<br>Keywords are case-insensitive and can be abbreviated. So for example these are valid: <br>' +
               '<i>ext=sstv,14230</i> &nbsp;&nbsp; <i>ext=sstv,7171,no &nbsp;&nbsp; <i>ext=sstv,t</i>' +
               ''
            )
         );

      confirmation_show_content(s, 610, 380);
      w3_el('id-confirmation-container').style.height = '100%';   // to get the w3-scroll-y above to work
   }
   return true;
}


// called to display HTML for configuration parameters in admin interface
function SSTV_config_html()
{
   ext_config_html(sstv, 'sstv', 'SSTV', 'SSTV configuration');
}
