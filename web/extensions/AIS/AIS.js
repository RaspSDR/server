// Copyright (c) 2026 RaspSDR contributors

var ais = {
   ext_name: 'AIS', first_time: true, dataW: 1024, dataH: 445, ctrlW: 760, ctrlH: 125,
   pb: { lo: -6000, hi: 6000 }, freq_i: 0, freqs: [ 161.975, 162.025 ],
   vessels: {}, visible: true, max_age_mins: 30, report_count: 0
};

function AIS_main()
{
   ext_switch_to_client(ais.ext_name, ais.first_time, ais_recv);
   if (!ais.first_time) ais_controls_setup();
   ais.first_time = false;
}

function ais_recv(data)
{
   if (arrayBufferToStringLen(data, 3) == 'DAT') return;
   var params = arrayBufferToString(data).substring(4).split(' ');
   for (var i = 0; i < params.length; i++) {
      var param = params[i].split('=');
      switch (param[0]) {
         case 'ready': ais_controls_setup(); break;
         case 'report': ais_report(kiwi_decodeURIComponent('AIS', param[1])); break;
         case 'freq': ais_update_status(); break;
         case 'decoder': ais_status('w3-text-lime', 'Listening for AIS reports'); break;
         case 'error': ais_status('w3-text-red', kiwi_decodeURIComponent('', param[1])); break;
      }
   }
}

function ais_report(line)
{
   var fields = {};
   line.split('|').forEach(function(field) {
      var p = field.indexOf('=');
      if (p > 0) fields[field.substring(0, p)] = field.substring(p + 1);
   });
   var mmsi = fields.mmsi;
   if (!mmsi) return;
   var vessel = ais.vessels[mmsi] || { mmsi: mmsi, name: '', callsign: '', marker: null };
   vessel.type = +fields.type;
   vessel.name = fields.name || vessel.name;
   vessel.callsign = fields.call || vessel.callsign;
   vessel.sog = fields.sog;
   vessel.cog = fields.cog;
   vessel.hdg = fields.hdg;
   vessel.payload = fields.payload;
   vessel.updated = Date.now();
   if (!fields.lat.startsWith('na:') && !fields.lon.startsWith('na:')) {
      vessel.lat = +fields.lat;
      vessel.lon = +fields.lon;
      ais_place_vessel(vessel);
   }
   ais.vessels[mmsi] = vessel;
   ais.report_count++;
   ais_render_list();
   ais_update_status();
}

function ais_place_vessel(vessel)
{
   if (!ais.kmap || !ais.kmap.map) return;
   var label = vessel.name || vessel.callsign || vessel.mmsi;
   if (!vessel.marker) {
      vessel.marker = kiwi_map_add_marker_div(ais.kmap, kmap.NO_ADD_TO_MAP,
         [vessel.lat, vessel.lon], '', [12, 12], [0, 0], 1.0);
      kiwi_style_marker(ais.kmap, kmap.ADD_TO_MAP, vessel.marker, label,
         'id-ais-vessel id-ais-vessel-'+ vessel.mmsi + (ais.visible? '' : ' w3-hide'), kmap.DIR_RIGHT);
   } else {
      vessel.marker.setLatLng([vessel.lat, vessel.lon]);
   }
}

function ais_render_list()
{
   var el = w3_el('id-ais-list');
   if (!el) return;
   el.innerHTML = '';
   var vessels = Object.keys(ais.vessels).map(function(k) { return ais.vessels[k]; })
      .sort(function(a, b) { return b.updated - a.updated; });
   if (!vessels.length) {
      el.innerHTML = '<div class="ui-ais-empty">Waiting for AIS reports...</div>';
      return;
   }
   vessels.forEach(function(v) {
      var row = document.createElement('div');
      row.className = 'ui-ais-vessel';
      var title = document.createElement('div');
      title.className = 'ui-ais-vessel-name';
      title.textContent = v.name || v.callsign || v.mmsi;
      var meta = document.createElement('div');
      meta.className = 'ui-ais-vessel-meta';
      meta.textContent = 'MMSI '+ v.mmsi +' | type '+ v.type +
         (v.sog && !v.sog.startsWith('na:')? ' | '+ v.sog +' kn' : '') +
         (v.lat !== undefined? ' | '+ v.lat.toFixed(4) +', '+ v.lon.toFixed(4) : '');
      row.appendChild(title); row.appendChild(meta);
      row.onclick = function() { if (v.marker) ais.kmap.map.setView([v.lat, v.lon], 10); };
      el.appendChild(row);
   });
}

function ais_controls_setup()
{
   var freq_s = ais.freqs.map(function(f) { return f.toFixed(3) +' MHz'; });
   var data_html = time_display_html('ais') +
      w3_div('ui-ais-data|width:'+ ais.dataW +'px; height:'+ ais.dataH +'px',
         w3_div('||id="id-ais-map"'), w3_div('id-ais-list ui-ais-list'));
   var controls_html = w3_div('id-ais-controls w3-text-white',
      w3_inline('w3-tspace-8 w3-valign/w3-margin-between-12',
         w3_div('w3-medium w3-text-aqua', '<b>AIS vessel decoder</b>'),
         w3_select('w3-text-red w3-ext-retain-input-focus', 'Channel', '', 'ais.freq_i', ais.freq_i, freq_s, 'ais_freq_cb'),
         w3_checkbox('w3-label-inline w3-label-not-bold', 'Show vessels', 'ais.visible', ais.visible, 'ais_visible_cb'),
         w3_button('w3-padding-smaller w3-css-yellow', 'Clear', 'ais_clear_cb'),
         w3_div('id-ais-status w3-text-lime', 'Starting decoder...')
      ));
   ext_panel_show(controls_html, data_html, null);
   ext_set_data_height(ais.dataH);
   ext_set_controls_width_height(ais.ctrlW, ais.ctrlH);
   time_display_setup('ais');
   ais.kmap = null;
   w3_do_when_rendered('id-ais-map', function() {
      ais.kmap = kiwi_map_init('ais', [0, 0], 2, 17);
      Object.keys(ais.vessels).forEach(function(mmsi) {
         var vessel = ais.vessels[mmsi];
         vessel.marker = null;
         if (vessel.lat !== undefined) ais_place_vessel(vessel);
      });
   });
   ais.saved_mode = ext_get_mode();
   ais.saved_passband = ext_get_passband();
   ext_set_mode('nnfm');
   ext_set_passband(ais.pb.lo, ais.pb.hi);
   ext_send('SET start');
   ais_tune(ais_select_frequency(ext_param()));
   kiwi_clearInterval(ais.age_timer);
   ais.age_timer = setInterval(ais_age_vessels, 60000);
}

function ais_select_frequency(param)
{
   var freq = param? +param.split(',')[0] : NaN;
   if (isNumber(freq)) {
      ais.freq_i = ais.freqs.reduce(function(closest, candidate, index) {
         return Math.abs(candidate - freq) < Math.abs(ais.freqs[closest] - freq)? index : closest;
      }, 0);
   } else {
      freq = ais.freqs[ais.freq_i];
   }
   return freq;
}

function ais_tune(freq)
{
   ext_tune(freq * 1000 - kiwi.freq_offset_kHz, 'nnfm', ext_zoom.CUR);
   ext_set_passband(ais.pb.lo, ais.pb.hi);
   ais.current_freq = freq;
   ais_update_status();
}

function ais_freq_cb(path, idx, first)
{
   if (first) return;
   ais.freq_i = +idx;
   ais_tune(ais.freqs[ais.freq_i]);
}

function ais_visible_cb(path, checked, first)
{
   if (first) return;
   ais.visible = checked;
   kiwi_map_markers_visible('id-ais-vessel', checked);
}

function ais_clear_cb(path, val, first)
{
   if (first) return;
   Object.keys(ais.vessels).forEach(function(k) {
      if (ais.vessels[k].marker) ais.vessels[k].marker.remove();
   });
   ais.vessels = {};
   ais.report_count = 0;
   ais_render_list();
   ais_update_status();
}

function ais_age_vessels()
{
   var old = Date.now() - ais.max_age_mins * 60000;
   Object.keys(ais.vessels).forEach(function(k) {
      var v = ais.vessels[k];
      if (v.updated < old) {
         if (v.marker) v.marker.remove();
         delete ais.vessels[k];
      }
   });
   ais_render_list();
}

function ais_status(color, text)
{
   var el = w3_el('id-ais-status');
   if (el) { el.className = color; el.textContent = text; }
}

function ais_update_status()
{
   ais_status('w3-text-lime', 'AIS '+ (ais.current_freq || ais.freqs[ais.freq_i]).toFixed(3) +
      ' MHz | '+ Object.keys(ais.vessels).length +' vessels | '+ ais.report_count +' reports');
}

function AIS_blur()
{
   kiwi_clearInterval(ais.age_timer);
   if (ais.kmap) kiwi_map_blur(ais.kmap);
   ext_send('SET stop');
   if (isDefined(ais.saved_mode)) ext_set_mode(ais.saved_mode);
   if (ais.saved_passband) ext_set_passband(ais.saved_passband.low, ais.saved_passband.high);
}

function AIS_help(show)
{
   if (show)
      confirmation_show_content(w3_div('w3-margin-T-8',
         'AIS receives maritime reports on AIS 1 (161.975 MHz) or AIS 2 (162.025 MHz). ' +
         'This receiver decodes one selected 12 kHz NFM channel at a time. Valid position reports appear as vessel markers; ' +
         'static reports update vessel names and call signs. Weak signals, frequency error, and overloaded channels reduce decode rate.'), 600, 180);
   return true;
}

function AIS_config_html() { ext_config_html(ais, 'ais', 'AIS', 'AIS configuration'); }
