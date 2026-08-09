var space_weather = {
   ext_name: 'space_weather',
   first_time: true,
   refresh_timer: null,
   last_update: '',

   apis: {
      kp:         'https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json',
      kp_forecast:'https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json',
      sfi:        'https://services.swpc.noaa.gov/products/summary/10cm-flux.json',
      ssn:        'https://services.swpc.noaa.gov/json/solar-cycle/observed-solar-cycle-indices.json',
      xray_bg:    'https://services.swpc.noaa.gov/json/goes/primary/xray-background-7-day.json',
      xray_1day:  'https://services.swpc.noaa.gov/json/goes/primary/xrays-1-day.json',
      solar_wind: 'https://services.swpc.noaa.gov/products/summary/solar-wind-mag-field.json'
   },

   data: {},
   loaded: 0,
   TO_LOAD: 7,

   colors: {
      green:  '#4caf50',
      yellow: '#ffeb3b',
      orange: '#ff9800',
      red:    '#f44336',
      cyan:   '#00bcd4',
      white:  '#ffffff',
      gray:   '#9e9e9e'
   }
};

function space_weather_main()
{
   ext_switch_to_client(space_weather.ext_name, space_weather.first_time, space_weather_recv);
   if (!space_weather.first_time) space_weather_controls_setup();
   space_weather.first_time = false;
}

function space_weather_recv(data)
{
   var firstChars = arrayBufferToStringLen(data, 3);
   if (firstChars == "DAT") return;

   var stringData = arrayBufferToString(data);
   var params = stringData.substring(4).split(" ");
   for (var i = 0; i < params.length; i++) {
      var param = params[i].split("=");
      switch (param[0]) {
         case "ready":
            space_weather_controls_setup();
            break;
      }
   }
}

function space_weather_controls_setup()
{
   space_weather.refresh_min = ext_get_cfg_param('space_weather.refresh_min', 60, EXT_SAVE);
   space_weather.refresh_min = Math.max(10, Math.min(180, parseInt(space_weather.refresh_min) || 60));

   space_weather.show_band = ext_get_cfg_param('space_weather.show_band', 1, EXT_SAVE);
   space_weather.show_muf  = ext_get_cfg_param('space_weather.show_muf', 1, EXT_SAVE);

   var controls_html =
      w3_div('id-space-weather-controls w3-text-white w3-padding',
         w3_inline('w3-valign w3-margin-B-4',
            w3_div('w3-medium w3-text-aqua w3-bold', 'Space Weather'),
            w3_div('id-sw-last-update w3-small w3-text-gray w3-margin-L-8', '')
         ),
         w3_div('id-sw-data w3-small', w3_div('w3-text-gray', 'Loading NOAA SWPC data...'))
      );

   ext_panel_show(controls_html, null, null);
   ext_set_controls_width_height(420, 525);

   space_weather_fetch_all();
}

function space_weather_fetch_all()
{
   space_weather.loaded = 0;
   space_weather.data = {};
   space_weather.TO_LOAD = 7;

   space_weather_fetch('kp',         space_weather.apis.kp);
   space_weather_fetch('kp_forecast',space_weather.apis.kp_forecast);
   space_weather_fetch('sfi',        space_weather.apis.sfi);
   space_weather_fetch('ssn',        space_weather.apis.ssn);
   space_weather_fetch('xray_bg',    space_weather.apis.xray_bg);
   space_weather_fetch('xray_1day',  space_weather.apis.xray_1day);
   space_weather_fetch('solar_wind', space_weather.apis.solar_wind);
}

function space_weather_fetch(key, url)
{
   try {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', url, true);
      xhr.timeout = 15000;
      xhr.onreadystatechange = function() {
         if (xhr.readyState == 4) {
            if (xhr.status == 200) {
               try {
                  space_weather.data[key] = JSON.parse(xhr.responseText);
               } catch(e) {
                  console.log('space_weather: parse error for '+ key +': '+ e.message);
               }
            } else {
               console.log('space_weather: HTTP '+ xhr.status +' for '+ key);
            }
            space_weather.loaded++;
            if (space_weather.loaded >= space_weather.TO_LOAD) {
               space_weather_render();
            }
         }
      };
      xhr.ontimeout = function() {
         console.log('space_weather: timeout for '+ key);
         space_weather.loaded++;
         if (space_weather.loaded >= space_weather.TO_LOAD) {
            space_weather_render();
         }
      };
      xhr.send();
   } catch(e) {
      console.log('space_weather: fetch error for '+ key +': '+ e);
      space_weather.loaded++;
      if (space_weather.loaded >= space_weather.TO_LOAD) {
         space_weather_render();
      }
   }
}

function space_weather_kp_color(kp)
{
   if (kp < 3) return space_weather.colors.green;
   if (kp < 5) return space_weather.colors.yellow;
   if (kp < 7) return space_weather.colors.orange;
   return space_weather.colors.red;
}

function space_weather_sfi_color(sfi)
{
   if (sfi < 100) return space_weather.colors.gray;
   if (sfi < 150) return space_weather.colors.green;
   if (sfi < 200) return space_weather.colors.yellow;
   return space_weather.colors.orange;
}

function space_weather_xray_color(bg)
{
   if (bg < 1e-6) return space_weather.colors.green;
   if (bg < 5e-6) return space_weather.colors.yellow;
   if (bg < 1e-5) return space_weather.colors.orange;
   return space_weather.colors.red;
}

function space_weather_band_condition(sfi, kp)
{
   if (sfi > 150 && kp < 3) return 'Good';
   if (sfi > 120 && kp < 4) return 'Fair';
   if (sfi > 100 && kp < 5) return 'Fair';
   return 'Poor';
}

function space_weather_band_condition_color(cond)
{
   switch(cond) {
      case 'Good':  return space_weather.colors.green;
      case 'Fair':  return space_weather.colors.yellow;
      case 'Poor':  return space_weather.colors.red;
      default:      return space_weather.colors.gray;
   }
}

function space_weather_render()
{
   var d = space_weather.data;
   var html = '';

   var sfi_val = '--', sfi = 0;
   if (d.sfi && d.sfi[0]) {
      sfi = d.sfi[0].flux || 0;
      sfi_val = sfi.toString();
   }

   var ssn_val = '--', ssn = 0;
   if (d.ssn && d.ssn.length) {
      var latest = d.ssn[d.ssn.length - 1];
      ssn = latest.ssn || latest.observed_swpc_ssn || 0;
      ssn_val = ssn.toString();
   }

   var kp_val = '--', kp = 0, a_val = '--';
   if (d.kp && d.kp.length) {
      var latest_kp = d.kp[d.kp.length - 1];
      kp = latest_kp.Kp || 0;
      kp_val = kp.toFixed(1);
      a_val = (latest_kp.a_running || '--').toString();
   }

   var xray_bg_val = '--', xray_bg = 0;
   if (d.xray_bg && d.xray_bg.length) {
      var latest_x = d.xray_bg[d.xray_bg.length - 1];
      xray_bg = latest_x.background || 0;
      xray_bg_val = space_weather_xray_class(xray_bg);
   }

   var bt_val = '--', bz_val = '--';
   if (d.solar_wind && d.solar_wind[0]) {
      bt_val = (d.solar_wind[0].bt || '--').toString();
      bz_val = (d.solar_wind[0].bz_gsm != undefined ? d.solar_wind[0].bz_gsm : '--').toString();
      if (bz_val != '--') bz_val = (parseFloat(bz_val) >= 0 ? '+' : '') + bz_val;
   }

   var xray_peak = '--';
   if (d.xray_1day && d.xray_1day.length) {
      var max_flux = 0;
      for (var i = 0; i < d.xray_1day.length; i++) {
         var flux = d.xray_1day[i].flux || 0;
         if (flux > max_flux) max_flux = flux;
      }
      if (max_flux > 0) xray_peak = space_weather_xray_class(max_flux);
   }

   html += space_weather_row('Solar Flux Index', sfi_val, space_weather_sfi_color(sfi));
   html += space_weather_row('Sunspot Number', ssn_val, ssn > 50 ? space_weather.colors.green : space_weather.colors.gray);
   html += space_weather_row('K-Index', kp_val, space_weather_kp_color(kp));
   html += space_weather_row('A-Index', a_val, kp < 3 ? space_weather.colors.green : space_weather.colors.yellow);
   html += space_weather_row('X-Ray Background', xray_bg_val, space_weather_xray_color(xray_bg));
   html += space_weather_row('X-Ray Peak (24h)', xray_peak, space_weather.colors.cyan);
   html += space_weather_row('Solar Wind Bt', bt_val + ' nT', space_weather.colors.cyan);
   html += space_weather_row('Solar Wind Bz', bz_val + ' nT', parseFloat(bz_val) < 0 ? space_weather.colors.red : space_weather.colors.green);

   if (space_weather.show_band) {
      html += '<div class="sw-separator"></div>';
      html += '<div class="sw-section-title">HF Band Conditions</div>';
      html += space_weather_bands_html(sfi, kp, ssn);
   }

   if (space_weather.show_muf) {
      html += '<div class="sw-separator"></div>';
      html += '<div class="sw-section-title">MUF Estimate (3000km)</div>';
      html += space_weather_muf_html(sfi, ssn);
   }

   html += '<div class="sw-separator"></div>';
   html += space_weather_kp_forecast_html(d.kp_forecast);

   var now = new Date();
   var utc = now.toISOString().replace('T', ' ').substring(0, 16) + ' UTC';
   html += '<div class="sw-footer">Data: NOAA SWPC | ' + utc + '</div>';

   w3_innerHTML('id-sw-data', html);

   if (space_weather.refresh_timer) clearTimeout(space_weather.refresh_timer);
   space_weather.refresh_timer = setTimeout(space_weather_fetch_all, space_weather.refresh_min * 60 * 1000);
}

function space_weather_row(label, value, color)
{
   return '<div class="sw-row">' +
      '<span class="sw-label">' + label + '</span>' +
      '<span class="sw-value" style="color:'+ color +'">' + value + '</span>' +
      '</div>';
}

function space_weather_xray_class(flux)
{
   if (flux >= 1e-4) return 'X' + (flux / 1e-4).toFixed(1);
   if (flux >= 1e-5) return 'M' + (flux / 1e-5).toFixed(1);
   if (flux >= 1e-6) return 'C' + (flux / 1e-6).toFixed(1);
   if (flux >= 1e-7) return 'B' + (flux / 1e-7).toFixed(1);
   return 'A' + (flux / 1e-8).toFixed(1);
}

function space_weather_bands_html(sfi, kp, ssn)
{
   var bands = [
      { name: '160m', hz: 1.8, night_only: true },
      { name: '80m',  hz: 3.5, night_only: true },
      { name: '60m',  hz: 5.3, night_only: false },
      { name: '40m',  hz: 7.0, night_only: false },
      { name: '30m',  hz: 10.1, night_only: false },
      { name: '20m',  hz: 14.0, night_only: false },
      { name: '17m',  hz: 18.1, night_only: false },
      { name: '15m',  hz: 21.0, night_only: false },
      { name: '12m',  hz: 24.9, night_only: false },
      { name: '10m',  hz: 28.0, night_only: false },
      { name: '6m',   hz: 50.0, night_only: false }
   ];

   var muf = space_weather_calc_muf(sfi, ssn);
   var now = new Date();
   var utc_hour = now.getUTCHours();
   var is_night = (utc_hour < 6 || utc_hour > 18);

   var html = '<div class="sw-bands">';
   for (var i = 0; i < bands.length; i++) {
      var b = bands[i];
      var cond, color;
      if (b.night_only && is_night) {
         cond = 'Good'; color = space_weather.colors.green;
      } else if (b.night_only && !is_night) {
         cond = 'Poor'; color = space_weather.colors.red;
      } else if (b.hz <= muf * 0.85) {
         cond = 'Good'; color = space_weather.colors.green;
      } else if (b.hz <= muf) {
         cond = 'Fair'; color = space_weather.colors.yellow;
      } else {
         cond = 'Poor'; color = space_weather.colors.red;
      }
      html += '<span class="sw-band" style="background:'+ color +'; color: #000;">' +
         b.name + '<br><span class="sw-band-cond">' + cond + '</span></span>';
   }
   html += '</div>';
   return html;
}

function space_weather_calc_muf(sfi, ssn)
{
   if (!sfi || !ssn) return 14;
   var muf = 0.8 * (sfi / 100) * (1 + ssn / 200) * 15;
   return Math.max(5, Math.min(35, muf));
}

function space_weather_muf_html(sfi, ssn)
{
   var muf = space_weather_calc_muf(sfi, ssn);
   var bands_open;
   if (muf >= 28) bands_open = '10m+';
   else if (muf >= 21) bands_open = '15m+';
   else if (muf >= 14) bands_open = '20m+';
   else if (muf >= 7) bands_open = '40m+';
   else bands_open = '80m+';

   return '<div class="sw-row">' +
      '<span class="sw-label">Estimated MUF</span>' +
      '<span class="sw-value" style="color:'+ space_weather.colors.cyan +'">~' + muf.toFixed(0) + ' MHz</span>' +
      '</div>' +
      '<div class="sw-row">' +
      '<span class="sw-label">Bands likely open</span>' +
      '<span class="sw-value" style="color:'+ space_weather.colors.green +'">' + bands_open + '</span></div>';
}

function space_weather_kp_forecast_html(forecast)
{
   if (!forecast || !forecast.length) return '';

   var days = {};
   for (var i = 0; i < forecast.length; i++) {
      var f = forecast[i];
      var date = f.time_tag ? f.time_tag.substring(0, 10) : '';
      if (!date) continue;
      if (!days[date]) days[date] = { min: 99, max: 0 };
      var kp = f.kp || 0;
      if (kp < days[date].min) days[date].min = kp;
      if (kp > days[date].max) days[date].max = kp;
   }

    var html = '<div class="sw-section-title">K-Index Forecast</div>';
    var today = new Date();
    var todayStr = today.getFullYear() + '-' +
       String(today.getMonth() + 1).padStart(2, '0') + '-' +
       String(today.getDate()).padStart(2, '0');
    var dates = Object.keys(days).sort().filter(function(d) { return d >= todayStr; }).slice(0, 3);
    for (var i = 0; i < dates.length; i++) {
       var date = dates[i];
       var d = days[date];
       var color = space_weather_kp_color(d.max);
       var label = date.substring(5);
       html += '<div class="sw-row">' +
          '<span class="sw-label">' + label + '</span>' +
          '<span class="sw-value" style="color:'+ color +'">Kp ' +
          d.min.toFixed(0) + '-' + d.max.toFixed(0) + '</span>' +
          '</div>';
    }
   return html;
}

function space_weather_blur()
{
   if (space_weather.refresh_timer) {
      clearTimeout(space_weather.refresh_timer);
      space_weather.refresh_timer = null;
   }
}

function space_weather_config_html()
{
   space_weather.refresh_min = ext_get_cfg_param('space_weather.refresh_min', 60, EXT_SAVE);
   space_weather.show_band   = ext_get_cfg_param('space_weather.show_band', 1, EXT_SAVE);
   space_weather.show_muf    = ext_get_cfg_param('space_weather.show_muf', 1, EXT_SAVE);

   var s =
      w3_text('w3-text-black',
         'Space Weather extension displays real-time solar and geomagnetic data from NOAA SWPC.<br>' +
         'Data includes SFI, sunspot number, K/A-index, X-ray flux, solar wind, HF band conditions, and MUF estimates.'
      ) +
      '<hr>' +
      w3_inline_percent('w3-valign-start/',
         w3_divs('/w3-margin-bottom',
            w3_input_get('', 'Refresh interval (minutes)', 'space_weather.refresh_min', 'w3_num_set_cfg_cb', 0),
            w3_select('w3-label-inline', 'Show HF Band Conditions', '', 'space_weather.show_band', space_weather.show_band, ['No', 'Yes'], 'w3_bool_set_cfg_cb'),
            w3_select('w3-label-inline', 'Show MUF Estimate', '', 'space_weather.show_muf', space_weather.show_muf, ['No', 'Yes'], 'w3_bool_set_cfg_cb')
         ), 65
      );

   ext_config_html(space_weather, 'space_weather', 'Space Weather', 'Space Weather extension configuration', s);
}

function space_weather_help(show)
{
   if (show) {
      var s = w3_text('w3-medium w3-bold w3-text-aqua', 'Space Weather Help') + '<br><br>' +
         'Displays real-time solar and geomagnetic data from NOAA Space Weather Prediction Center.<br><br>' +
         '<b>Solar Flux Index (SFI)</b>: Measure of solar radio emission at 10.7cm wavelength<br>' +
         '<b>Sunspot Number</b>: Number of sunspots on the visible solar disk<br>' +
         '<b>K-Index</b>: Geomagnetic disturbance index (0-9, lower is better for HF)<br>' +
         '<b>A-Index</b>: Daily average of geomagnetic activity<br>' +
         '<b>X-Ray Background</b>: GOES X-ray background flux level<br>' +
         '<b>Solar Wind</b>: Interplanetary magnetic field (Bt total, Bz north-south)<br>' +
         '<b>HF Band Conditions</b>: Estimated propagation conditions per band<br>' +
         '<b>MUF</b>: Maximum Usable Frequency estimate<br><br>' +
         'Data refreshes automatically every ' + space_weather.refresh_min + ' minutes.';
      confirmation_show_content(s, 550, 380);
   }
   return true;
}
