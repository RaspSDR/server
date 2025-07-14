// Copyright (c) 2018-2023 Kari Karvonen, OH1KK

var ant_sw = {
    ext_name: 'ant_switch',    // NB: must match ant_sw.c:ant_switch_ext.name
    first_time: true,
    not_configured: false,
    denymixing: 0,
    thunderstorm: 0,
    
    n_ant: 8,
    exantennas: 0,    // to avoid console.log spam on timer updates
    
    poll_interval: null,
    url_ant: null,
    url_deselected: false,
    url_idx: 0,
    desc_lc: [],
 
    last_offset: -1,
    last_high_side: -1,
    
    // ordering for backward compatibility with cfg.ant_switch.denyswitching
    EVERYONE: 0,
    LOCAL_CONN: 1,
    LOCAL_OR_PWD: 2,
    denyswitching: 0,
    deny_s: [ 'everyone', 'local connections only', 'local connections or user password only' ],
    denied_because_multiuser: false,
 };
 
 // initially set to blank so "extension not configured" condition can be detected
 ant_sw.denyswitching = ext_get_cfg_param_string('ant_switch.denyswitching', '', EXT_NO_SAVE);
 ant_sw.not_configured = (ant_sw.denyswitching == '');
 ant_sw.denymixing = ext_get_cfg_param_string('ant_switch.denymixing', '', EXT_NO_SAVE);
 ant_sw.thunderstorm = ext_get_cfg_param_string('ant_switch.thunderstorm', '', EXT_NO_SAVE);
 
 function ant_switch_main()
 {
    ext_switch_to_client(ant_sw.ext_name, ant_sw.first_time, ant_switch_recv);     // tell server to use us (again)
    if (!ant_sw.first_time)
       ant_switch_controls_setup();
    ant_sw.first_time = false;
 }
 
 function ant_switch_recv(data)
 {
    var firstChars = arrayBufferToStringLen(data, 3);
    
    // process data sent from server/C by ext_send_data_msg()
    if (firstChars == "DAT") {
       var ba = new Uint8Array(data, 4);
       var cmd = ba[0];
       console.log('ant_switch_recv: DATA UNKNOWN cmd='+ cmd +' len='+ (ba.length-1));
       return;
    }
    
    // process command sent from server/C by ext_send_msg() or ext_send_encoded_msg()
    var stringData = arrayBufferToString(data);
    var params = stringData.substring(4).split(" ");
 
    for (var i=0; i < params.length; i++) {
       var param = params[i].split("=");
 
       switch (param[0]) {
 
          case "ready":
             ant_switch_controls_setup();
             break;
          case "backend_ver":
             var ver = param[1].split('.');
             if (ver.length == 2 && (ver[0] || ver[1])) {
                console.log('ant_switch: backend v'+ ver[0] +'.'+ ver[1]);
             }
             break;
          case "channels":
             var n_ch = +param[1];
             if (n_ch >= 1) {
                ant_sw.n_ant = n_ch;
                console.log('ant_switch: channels='+ n_ch);
                ant_switch_buttons_setup();
             }
             break;
          case "Antenna":
             ant_switch_process_reply(param[1]);
             break;
          case "AntennaDenySwitching":
             ant_sw.denyswitching = (param[1] == 0)? ant_sw.EVERYONE : ant_sw.LOCAL_CONN;
             ant_sw.denied_because_multiuser = (param[1] == 2);
             ant_switch_showpermissions();
             break;
          case "AntennaDenyMixing":
             if (param[1] == 1) ant_sw.denymixing = 1; else ant_sw.denymixing = 0;
             ant_switch_showpermissions(); 
             break;
          case "Thunderstorm":
             if (param[1] == 1) {
                ant_sw.thunderstorm = 1;
                ant_sw.denyswitching = ant_sw.LOCAL_CONN;
             } else {
                 ant_sw.thunderstorm = 0;
             }
             ant_switch_showpermissions();
             break;
          default:
             console.log('ant_switch_recv: UNKNOWN CMD '+ param[0]);
             break;
       }
    }
 }
 
 function ant_switch_buttons_setup()
 {
    var antdesc = [ ];
    var tmp;
    for (tmp=1; tmp <= ant_sw.n_ant; tmp++) antdesc[tmp] = ext_get_cfg_param_string('ant_switch.ant'+ tmp +'desc', '', EXT_NO_SAVE);
    
    console.log('ant_switch: Antenna configuration');
    var buttons_html = '';
    var n_ant = 0;
    for (tmp = 1; tmp <= ant_sw.n_ant; tmp++) {
       if (antdesc[tmp] == undefined || antdesc[tmp] == null || antdesc[tmp] == '') {
          antdesc[tmp] = ''; 
       }  else {
          buttons_html += w3_div('w3-valign w3-margin-T-8',
             w3_button('id-ant-sw-btn', 'Antenna '+tmp, 'ant_switch_select_antenna_cb', tmp),
             w3_div('w3-margin-L-8', antdesc[tmp])
          );
          n_ant++;
       }
       ant_sw.desc_lc[tmp] = antdesc[tmp].toLowerCase();
       console.log('ant_switch: Antenna '+ tmp +': '+ antdesc[tmp]);
    }
    w3_innerHTML('id-ant_switch-user', buttons_html);
    ext_set_controls_width_height(400, 90 + Math.round(n_ant * 40));
 }
 
 function ant_switch_controls_setup()
 {
    //console.log('ant_switch: Antenna g: Ground all antennas');
    var controls_html =
       w3_div('id-ant_display-controls w3-text-white',
          w3_div('',
             w3_div('w3-medium w3-text-aqua', '<b>Antenna switch</b>'),
             w3_div('id-ant-display-selected w3-margin-T-8', 'Selected antenna: unknown'),
             w3_div('id-ant-display-permissions', 'Permissions: unknown'),
             w3_div('id-ant_switch-user')
          )
       );
 
    ext_panel_show(controls_html, null, null);
    ant_switch_buttons_setup();
    ant_switch_poll();
 
     var p = ext_param();
     console.log('ant_switch: URL param = <'+ p +'>');
     if (p) {
        ant_sw.url_ant = p.split(',');
     }
 }
 
 function ant_switch_blur()
 {
    kiwi_clearInterval(ant_sw.poll_interval);
    //console.log('### ant_switch_blur');
 }
 
 // called to display HTML for configuration parameters in admin interface
 function ant_switch_config_html2(n_ch)
 {
    if (n_ch) ant_sw.n_ant = n_ch;
    console.log('ant_switch_config_html2 n_ch='+ n_ch);
    var s = '';
 
    for (var i = 1; i <= ant_sw.n_ant; i++) {
       s +=
          w3_inline_percent('w3-margin-T-16 w3-valign-center/',
             w3_input_get('', 'Antenna '+ i +' description', 'ant_switch.ant'+ i +'desc', 'w3_string_set_cfg_cb', ''), 50,
             '&nbsp;', 5,
             w3_checkbox_get_param('//w3-label-inline', 'High-side injection', 'ant_switch.ant'+ i +'high_side', 'admin_bool_cb', false), 10,
             '&nbsp;', 3,
             w3_input_get('', 'Frequency scale offset (kHz)', 'ant_switch.ant'+ i +'offset', 'w3_int_set_cfg_cb', 0)
          );
    }
    w3_innerHTML('id-ant_switch-admin', s);
 }
 
 function ant_switch_config_html()
 {
    ext_send('ADM get_ant_switch_nch');
    var s = w3_div('id-ant_switch-admin');
 
    var deny_select = ext_get_cfg_param('ant_switch.denyswitching', '', EXT_NO_SAVE);
    if (deny_select == '') deny_select = ant_sw.EVERYONE;
 
    // *_no_yes: 0 -> 'No', 1 -> 'Yes' in w3_switch() below
    var denymixing_no_yes = ext_get_cfg_param('ant_switch.denymixing', '', EXT_NO_SAVE)? 0:1;
    var denymultiuser_no_yes = ext_get_cfg_param('ant_switch.denymultiuser', '', EXT_NO_SAVE)? 0:1;
    var thunderstorm_no_yes = ext_get_cfg_param('ant_switch.thunderstorm', '', EXT_NO_SAVE)? 0:1;
 
    ext_admin_config(ant_sw.ext_name, 'Antenna switch',
       w3_div('id-ant_switch w3-text-teal w3-hide', '<b>Antenna switch configuration</b>' + '<hr>' +
          w3_div('',
             w3_div('', 'Version 0.5: 16 Jun 2023 <br><br>' +
                'If antenna switching is denied then users cannot switch antennas. <br>' +
                'Admin can always switch antennas from a connection on the local network.' +
                'The last option allows anyone connecting using a password to switch antennas <br>' +
                'i.e. time limit exemption password on the admin page control tab, not the user login password. <br>' +
                'Other connections made without passwords are denied.'
             ),
             w3_select('w3-width-auto w3-label-inline w3-margin-T-8|color:red', 'Allow antenna switching by:', '',
                'ant_switch.denyswitching', deny_select, ant_sw.deny_s, 'ant_switchdeny_cb'
             ),
 
             w3_div('w3-margin-T-16','If Single antenna mode is selected then users can select only one antenna at time.'),
             w3_div('w3-margin-T-8', '<b>Single Antenna Mode?</b> ' +
                w3_switch('', 'No', 'Yes', 'ant_switch.denymixing', denymixing_no_yes, 'ant_switch_confdenymixing')
             ),
 
             w3_div('w3-margin-T-16','If Single user mode is selected then antenna switching is disabled when more than one user is online.'),
             w3_div('w3-margin-T-8', '<b>Enable Only single-user switching?</b> ' +
                w3_switch('', 'No', 'Yes', 'ant_switch.denymultiuser', denymultiuser_no_yes, 'ant_switch_confdenymultiuser')
             ),
 
             w3_div('w3-margin-T-16','If thunderstorm mode is activated, all antennas and forced to ground and switching is disabled.'),
             w3_div('w3-margin-T-8', '<b>Enable thunderstorm mode?</b> ' +
                w3_switch('', 'No', 'Yes', 'ant_switch.thunderstorm', thunderstorm_no_yes, 'ant_switch_confthunderstorm')
             ),
 
             w3_div('','<hr><b>Antenna buttons configuration</b><br>'),
             w3_col_percent('w3-margin-T-16/',
                'Leave antenna description field empty if you want to hide antenna button from users. <br>' +
                'For two-line descriptions use break sequence &lt;br&gt; between lines.', 68,
                'Overrides frequency scale offset value on <br> config tab when any antenna selected. <br>' +
                'No effect if antenna mixing enabled.'
             ),
 
             w3_div('',
                s,
                w3_col_percent('w3-margin-T-16/',
                   w3_input_get('', 'Antenna switch failure or unknown status decription', 'ant_switch.ant0desc', 'w3_string_set_cfg_cb', ''), 70
                )
             )
          )
       )
    );
 }
 
 function ant_switchdeny_cb(path, val, first) {
     console.log('ant_switchdeny_cb path='+ path +' val='+ val +' first='+ first);
     w3_int_set_cfg_cb(path, val);
 }
 
 function ant_switch_select_groundall(path,val) {
    setTimeout('w3_radio_unhighlight('+ q(path) +')', w3_highlight_time);
    ant_switch_select_antenna(0);
 }
 
 function ant_switch_select_antenna_cb(path, val) { ant_switch_select_antenna(val); }
 
 function ant_switch_select_antenna(ant) {
    console.log('ant_switch: switching antenna '+ant);
    ext_send('SET Antenna='+ant);
    ext_send('GET Antenna');
 }
 
 function ant_switch_poll() {
    kiwi_clearInterval(ant_sw.poll_interval);
    //ant_sw.poll_interval = setInterval(ant_switch_poll, 10000);
    ant_sw.poll_interval = setInterval(function() {ant_switch_poll(0);}, 10000);
    ext_send('GET Antenna');
 }
 
 function ant_switch_process_reply(ant_selected_antenna) {
    var need_to_inform = false;
    //console.log('ant_switch_process_reply ant_selected_antenna='+ ant_selected_antenna);
    
    ant_sw.denyswitching = ext_get_cfg_param_string('ant_switch.denyswitching', '', EXT_NO_SAVE);
    if (ant_sw.not_configured) {
       ant_switch_display_update('Antenna switch extension is not configured.');
       return;
    }
 
    if (ant_sw.exantennas != ant_selected_antenna) {
       // antenna changed.
       need_to_inform = true;
       ant_sw.exantennas = ant_selected_antenna;
    }
    
    if (ant_selected_antenna == '0') {
       if (need_to_inform) console.log('ant_switch: all antennas grounded');
       ant_switch_display_update('All antennas are grounded.');
    } else {
       if (need_to_inform) console.log('ant_switch: antenna '+ ant_selected_antenna +' in use');
       ant_switch_display_update('Selected antennas are now: '+ ant_selected_antenna);
    }
    
    // update highlight
    var selected_antennas_list = ant_selected_antenna.split(',');
    var re=/^Antenna ([1]?[0-9]+)/i;
 
    w3_els('id-ant-sw-btn',
       function(el, i) {
          if (!el.textContent.match(re)) return;
          w3_unhighlight(el);
          var antN = el.textContent.parseIntEnd();
          if (!isArray(selected_antennas_list)) return;
          if (selected_antennas_list.indexOf(antN.toString()) < 0) return;  // not currently selected
          w3_highlight(el);
       
          // check for frequency offset and high-side injection change
          // but only when one antenna is selected and mixing is disabled
          if (ant_sw.denymixing && selected_antennas_list.length == 1) {
             var s = 'ant_switch.ant'+ antN +'offset';
             var offset = ext_get_cfg_param(s, '', EXT_NO_SAVE);
             offset = +offset;
             if (!isNumber(offset)) offset = 0;
             if (1||offset != ant_sw.last_offset) {
                //console.log('SET freq_offset='+ offset);
                ext_send('SET freq_offset='+ offset);
                ant_sw.last_offset = offset;
             }
 
             var s = 'ant_switch.ant'+ antN +'high_side';
             var high_side = ext_get_cfg_param(s, '', EXT_NO_SAVE);
             if (1||high_side != ant_sw.last_high_side) {
                //console.log('SET high_side='+ high_side);
                ext_send('SET high_side='+ (high_side? 1:0));
                ant_sw.last_high_side = high_side;
             }
          }
       }
    );
 
    // process optional URL antenna list (includes multiuser feature)
    // switching denial is processed on server side to implement ant_sw.LOCAL_OR_PWD
    if (ant_sw.url_ant != null && ant_sw.url_ant.length > 0) {
    
       // Start by deselecting all antennas (backends may have memory of last antenna(s) used).
       // This code works because this routine is being routinely polled.
       console.log('ant_switch: url_deselected='+ ant_sw.url_deselected);
       if (ant_sw.url_deselected == false) {
          ant_switch_select_antenna(0);
          ant_sw.url_deselected = true;
       } else {
          // only allow first antenna if mixing denied
          console.log('ant_switch: URL url_idx='+ ant_sw.url_idx +' denymixing='+ ant_sw.denymixing);
          if (ant_sw.url_idx == 0 || ant_sw.denymixing == 0) {
             var ant = decodeURIComponent(ant_sw.url_ant.shift());
             console.log('ant_switch: URL ant = <'+ ant +'>');
             var n = parseInt(ant);
             if (!(!isNaN(n) && n >= 1 && n <= ant_sw.n_ant)) {
                if (ant == '') {
                   n = 0;
                } else {
                   // try to match on antenna descriptions
                   ant = ant.toLowerCase();
                   for (n = 1; n <= ant_sw.n_ant; n++) {
                      //console.log('ant_switch: CONSIDER '+ n +' <'+ ant +'> <'+ ant_sw.desc_lc[n] +'>');
                      if (ant_sw.desc_lc[n].indexOf(ant) != -1)
                         break;
                   }
                }
             }
             if (n >= 1 && n <= ant_sw.n_ant)
                ant_switch_select_antenna(n);    // this causes poll to re-occur immediately
             ant_sw.url_idx++;
          }
       }
    }
 }
 
 function ant_switch_lock_buttons(lock) {
    w3_els('id-ant-sw-btn',
       function(el, i) {
          // Antenna
          var re=/^Antenna ([1]?[0-9]+)/i;
          if (el.textContent.match(re)) {
             w3_disable(el, lock);
          }
 
          // Ground All
          var re=/^Ground all$/i;
          if (el.textContent.match(re)) {
             w3_disable(el, lock);
          }
       }
    );
 }
 
 function ant_switch_showpermissions() {
    if (ant_sw.not_configured) {
       w3_innerHTML('id-ant-display-permissions', '');
       return;
    }
    if (ant_sw.denyswitching == ant_sw.LOCAL_CONN) {
       ant_switch_lock_buttons(true);
       var reason = ant_sw.denied_because_multiuser? ' More than one user online.' : '';
       w3_innerHTML('id-ant-display-permissions', 'Antenna switching is denied.'+ reason);
    } else {
       ant_switch_lock_buttons(false);
       if (ant_sw.denymixing == 1) {
          w3_innerHTML('id-ant-display-permissions', 'Antenna switching is allowed. Mixing is not allowed.');
       } else {
          w3_innerHTML('id-ant-display-permissions', 'Antenna switching and mixing is allowed.');
       }
    }
    if (ant_sw.thunderstorm == 1) {
       ant_switch_lock_buttons(true);
       w3_innerHTML('id-ant-display-permissions', w3_text('w3-text-css-yellow', 'Thunderstorm. Antenna switching is denied.'));
    }
 }
 
 function ant_switch_display_update(ant) {
    w3_innerHTML('id-ant-display-selected', ant);
 }
 
 function ant_switch_confdenyswitching(id, idx) {
    ext_set_cfg_param(id, idx, EXT_SAVE);
 }
 
 function ant_switch_confdenymixing(id, idx) {
    ext_set_cfg_param(id, idx, EXT_SAVE);
 }
 
 function ant_switch_confdenymultiuser(id, idx) {
    ext_set_cfg_param(id, idx, EXT_SAVE);
 }
 
 function ant_switch_confthunderstorm(id, idx) {
    ext_set_cfg_param(id, idx, EXT_SAVE);
 }
 
 function ant_switch_help(show)
 {
    if (show) {
       var s = 
          w3_text('w3-medium w3-bold w3-text-aqua', 'Antenna switch help') +
          'When starting the extension from the browser URL the antenna(s) to select can be<br>' +
          'specified with a parameter, e.g. my_sdr:8073/?ext=ant,6 would select antenna #6<br>' +
          'and my_sdr:8073/?ext=ant,6,3 would select antennas #6 and #3 if antenna mixing<br>' +
          'is allowed.<br><br>' +
          
          'Instead of an antenna number a string can be specified that matches any<br>' +
          'case insensitive sub-string of the antenna description<br>' +
          'e.g. my_sdr:8073/?ext=ant,loop would match the description "E-W Attic Loop ".<br>' +
          'The first description match wins.' +
          '';
       confirmation_show_content(s, 600, 250);
    }
    return true;
 }