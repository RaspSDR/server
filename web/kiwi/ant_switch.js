// Copyright (c) 2018-2023 Kari Karvonen, OH1KK
// Ported from extension to core component for Web-888

var ant_sw = {
    ext_name: 'ant_switch',
    first_time: true,
    not_configured: false,
    denymixing: 0,
    thunderstorm: 0,

    n_ant: 8,
    exantennas: 0,

    poll_interval: null,
    url_ant: null,
    url_deselected: false,
    url_idx: 0,
    desc_lc: [],

    last_offset: -1,
    last_high_side: -1,

    EVERYONE: 0,
    LOCAL_CONN: 1,
    LOCAL_OR_PWD: 2,
    denyswitching: 0,
    deny_s: [ 'everyone', 'local connections only', 'local connections or user password only' ],
    denied_because_multiuser: false,

    focus: false,
    isConfigured: false,
    initialized: false
};

function ant_switch_focus()
{
    ant_sw.focus = true;
}

function ant_switch_blur()
{
    ant_sw.focus = false;
}

function ant_switch_view()
{
    if (!ant_sw.focus) return;
    keyboard_shortcut_nav('rf');
}

function ant_switch_user_init()
{
    // init config values here (not at top-level) because ext.js may not be loaded yet
    if (!ant_sw.initialized) {
       ant_sw.denyswitching = ext_get_cfg_param_string('ant_switch.denyswitching', '', EXT_NO_SAVE);
       ant_sw.not_configured = (ant_sw.denyswitching == '');
       ant_sw.denymixing = ext_get_cfg_param_string('ant_switch.denymixing', '', EXT_NO_SAVE);
       ant_sw.thunderstorm = ext_get_cfg_param_string('ant_switch.thunderstorm', '', EXT_NO_SAVE);
       ant_sw.initialized = true;
    }
    var controls_html =
       w3_div('id-antsw-controls w3-text-white',
          w3_div('w3-medium w3-text-aqua', '<b>Antenna switch</b>'),
          w3_div('id-ant-display-selected w3-margin-T-4', 'Selected antenna: unknown'),
          w3_div('id-ant-display-permissions', 'Permissions: unknown'),
          w3_div('id-ant_switch-user')
       );

    var el = w3_el('id-optbar-rf-antsw');
    if (el) el.innerHTML = controls_html;

    snd_send('SET antsw_init');
    ant_switch_poll();

    var p = ext_param();
    console.log('ant_switch: URL param = <' + p + '>');
    if (p) {
        ant_sw.url_ant = p.split(',');
    }
}

function ant_switch_msg(msg_a)
{
    var param_name = msg_a[0];
    var param_val = msg_a[1];

    switch (param_name) {
        case 'antsw_ready':
            ant_switch_buttons_setup();
            break;
        case 'antsw_backend_ver':
            var ver = param_val.split('.');
            if (ver.length == 2 && (ver[0] || ver[1])) {
                console.log('ant_switch: backend v' + ver[0] + '.' + ver[1]);
            }
            break;
        case 'antsw_channels':
            var n_ch = +param_val;
            if (n_ch >= 1) {
                ant_sw.n_ant = n_ch;
                console.log('ant_switch: channels=' + n_ch);
                ant_switch_buttons_setup();
            }
            break;
        case 'antsw_Antenna':
            ant_switch_process_reply(param_val);
            break;
        case 'antsw_AntennaDenySwitching':
            ant_sw.denyswitching = (param_val == 0) ? ant_sw.EVERYONE : ant_sw.LOCAL_CONN;
            ant_sw.denied_because_multiuser = (param_val == 2);
            ant_switch_showpermissions();
            break;
        case 'antsw_AntennaDenyMixing':
            ant_sw.denymixing = (param_val == 1) ? 1 : 0;
            ant_switch_showpermissions();
            break;
        case 'antsw_Thunderstorm':
            if (param_val == 1) {
                ant_sw.thunderstorm = 1;
                ant_sw.denyswitching = ant_sw.LOCAL_CONN;
            } else {
                ant_sw.thunderstorm = 0;
            }
            ant_switch_showpermissions();
            break;
        default:
            return false;
    }
    return true;
}

function ant_switch_buttons_setup()
{
    var antdesc = [];
    var tmp;
    for (tmp = 1; tmp <= ant_sw.n_ant; tmp++)
        antdesc[tmp] = ext_get_cfg_param_string('ant_switch.ant' + tmp + 'desc', '', EXT_NO_SAVE);

    console.log('ant_switch: Antenna configuration');
    var buttons_html = '';
    var n_ant = 0;
    for (tmp = 1; tmp <= ant_sw.n_ant; tmp++) {
        if (antdesc[tmp] == undefined || antdesc[tmp] == null || antdesc[tmp] == '') {
            antdesc[tmp] = '';
        } else {
            buttons_html += w3_div('w3-valign w3-margin-T-8',
               w3_button('id-ant-sw-btn', 'Antenna ' + tmp, 'ant_switch_select_antenna_cb', tmp),
               w3_div('w3-margin-L-8', antdesc[tmp])
            );
            n_ant++;
        }
        ant_sw.desc_lc[tmp] = antdesc[tmp].toLowerCase();
        console.log('ant_switch: Antenna ' + tmp + ': ' + antdesc[tmp]);
    }

    buttons_html += w3_div('w3-valign w3-margin-T-8',
       w3_button('id-ant-sw-btn w3-red', 'Ground All', 'ant_switch_select_groundall', 0),
       w3_div('w3-margin-L-8', 'Ground all antennas')
    );
    n_ant++;

    w3_innerHTML('id-ant_switch-user', buttons_html);
}

function ant_switch_select_groundall(path, val) {
    ant_switch_select_antenna(0);
}

function ant_switch_select_antenna_cb(path, val) {
    ant_switch_select_antenna(val);
}

function ant_switch_select_antenna(ant) {
    console.log('ant_switch: switching antenna ' + ant);
    snd_send('SET antsw_Antenna=' + ant);
    snd_send('SET antsw_GetAntenna');
}

function ant_switch_poll() {
    kiwi_clearInterval(ant_sw.poll_interval);
    ant_sw.poll_interval = setInterval(function() { ant_switch_poll(0); }, 10000);
    snd_send('SET antsw_GetAntenna');
}

function ant_switch_process_reply(ant_selected_antenna) {
    var need_to_inform = false;

    ant_sw.denyswitching = ext_get_cfg_param_string('ant_switch.denyswitching', '', EXT_NO_SAVE);
    if (ant_sw.not_configured) {
        ant_switch_display_update('Antenna switch is not configured.');
        return;
    }

    if (ant_sw.exantennas != ant_selected_antenna) {
        need_to_inform = true;
        ant_sw.exantennas = ant_selected_antenna;
    }

    if (ant_selected_antenna == '0') {
        if (need_to_inform) console.log('ant_switch: all antennas grounded');
        ant_switch_display_update('All antennas are grounded.');
    } else {
        if (need_to_inform) console.log('ant_switch: antenna ' + ant_selected_antenna + ' in use');
        ant_switch_display_update('Selected antennas are now: ' + ant_selected_antenna);
    }

    var selected_antennas_list = ant_selected_antenna.split(',');
    var re = /^Antenna ([1]?[0-9]+)/i;

    w3_els('id-ant-sw-btn',
       function(el, i) {
          if (!el.textContent.match(re)) return;
          w3_unhighlight(el);
          var antN = el.textContent.parseIntEnd();
          if (!isArray(selected_antennas_list)) return;
          if (selected_antennas_list.indexOf(antN.toString()) < 0) return;
          w3_highlight(el);

          if (ant_sw.denymixing && selected_antennas_list.length == 1) {
             var s = 'ant_switch.ant' + antN + 'offset';
             var offset = ext_get_cfg_param(s, '', EXT_NO_SAVE);
             offset = +offset;
             if (!isNumber(offset)) offset = 0;
             if (offset != ant_sw.last_offset) {
                snd_send('SET antsw_freq_offset=' + offset);
                ant_sw.last_offset = offset;
             }

             var s = 'ant_switch.ant' + antN + 'high_side';
             var high_side = ext_get_cfg_param(s, '', EXT_NO_SAVE);
             if (high_side != ant_sw.last_high_side) {
                snd_send('SET antsw_high_side=' + (high_side ? 1 : 0));
                ant_sw.last_high_side = high_side;
             }
          }
       }
    );

    if (ant_sw.url_ant != null && ant_sw.url_ant.length > 0) {
        console.log('ant_switch: url_deselected=' + ant_sw.url_deselected);
        if (ant_sw.url_deselected == false) {
           ant_switch_select_antenna(0);
           ant_sw.url_deselected = true;
        } else {
           console.log('ant_switch: URL url_idx=' + ant_sw.url_idx + ' denymixing=' + ant_sw.denymixing);
           if (ant_sw.url_idx == 0 || ant_sw.denymixing == 0) {
              var ant = decodeURIComponent(ant_sw.url_ant.shift());
              console.log('ant_switch: URL ant = <' + ant + '>');
              var n = parseInt(ant);
              if (!(!isNaN(n) && n >= 1 && n <= ant_sw.n_ant)) {
                 if (ant == '') {
                    n = 0;
                 } else {
                    ant = ant.toLowerCase();
                    for (n = 1; n <= ant_sw.n_ant; n++) {
                       if (ant_sw.desc_lc[n].indexOf(ant) != -1) break;
                    }
                 }
              }
              if (n >= 1 && n <= ant_sw.n_ant)
                 ant_switch_select_antenna(n);
              ant_sw.url_idx++;
           }
        }
    }
}

function ant_switch_lock_buttons(lock) {
    w3_els('id-ant-sw-btn',
       function(el, i) {
          var re = /^Antenna ([1]?[0-9]+)/i;
          if (el.textContent.match(re)) {
             w3_disable(el, lock);
          }
          var re = /^Ground All$/i;
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
       var reason = ant_sw.denied_because_multiuser ? ' More than one user online.' : '';
       w3_innerHTML('id-ant-display-permissions', 'Antenna switching is denied.' + reason);
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

function ant_switch_config_html2(n_ch) {
    if (n_ch) ant_sw.n_ant = n_ch;
    console.log('ant_switch_config_html2 n_ch=' + n_ch);
    var s = '';

    for (var i = 1; i <= ant_sw.n_ant; i++) {
       s +=
          w3_inline_percent('w3-margin-T-16 w3-valign-center/',
             w3_input_get('', 'Antenna ' + i + ' description', 'ant_switch.ant' + i + 'desc', 'w3_string_set_cfg_cb', ''), 50,
             '&nbsp;', 5,
             w3_checkbox_get_param('//w3-label-inline', 'High-side injection', 'ant_switch.ant' + i + 'high_side', 'admin_bool_cb', false), 10,
             '&nbsp;', 3,
             w3_input_get('', 'Frequency scale offset (kHz)', 'ant_switch.ant' + i + 'offset', 'w3_int_set_cfg_cb', 0)
          );
    }
    w3_innerHTML('id-ant_switch-admin', s);
}

function ant_switch_config_html() {
    ext_send('ADM get_ant_switch_nch');
    var s = w3_div('id-ant_switch-admin');

    var deny_select = ext_get_cfg_param('ant_switch.denyswitching', '', EXT_NO_SAVE);
    if (deny_select == '') deny_select = ant_sw.EVERYONE;

    var denymixing_no_yes = ext_get_cfg_param('ant_switch.denymixing', '', EXT_NO_SAVE) ? 0 : 1;
    var denymultiuser_no_yes = ext_get_cfg_param('ant_switch.denymultiuser', '', EXT_NO_SAVE) ? 0 : 1;
    var thunderstorm_no_yes = ext_get_cfg_param('ant_switch.thunderstorm', '', EXT_NO_SAVE) ? 0 : 1;

    var config_html =
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
       );

    ext_config_html(ant_sw, 'ant_switch', 'Antenna switch', 'Antenna switch configuration', config_html);
}

function ant_switchdeny_cb(path, val, first) {
    console.log('ant_switchdeny_cb path=' + path + ' val=' + val + ' first=' + first);
    w3_int_set_cfg_cb(path, val);
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

function ant_switch_help(show) {
    if (show) {
       var s =
          w3_text('w3-medium w3-bold w3-text-aqua', 'Antenna switch help') +
          'When starting from the browser URL the antenna(s) to select can be<br>' +
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
