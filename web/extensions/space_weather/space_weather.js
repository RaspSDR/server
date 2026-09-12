var space_weather = {
   ext_name: 'space_weather',
   first_time: true,
   refresh_timer: null,
   requests: [],
   generation: 0,

   apis: {
      kp: 'https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json',
      kp_forecast: 'https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json',
      sfi: 'https://services.swpc.noaa.gov/products/summary/10cm-flux.json',
      xray_bg: 'https://services.swpc.noaa.gov/json/goes/primary/xray-background-7-day.json',
      solar_wind: 'https://services.swpc.noaa.gov/products/summary/solar-wind-mag-field.json'
   },

   colors: {
      green: '#4caf50',
      yellow: '#ffeb3b',
      orange: '#ff9800',
      red: '#f44336',
      cyan: '#00bcd4',
      gray: '#9e9e9e'
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
   if (firstChars == 'DAT') return;

   var params = arrayBufferToString(data).substring(4).split(' ');
   for (var i = 0; i < params.length; i++) {
      var param = params[i].split('=');
      if (param[0] == 'ready') space_weather_controls_setup();
   }
}

function space_weather_controls_setup()
{
   space_weather.refresh_min = ext_get_cfg_param('space_weather.refresh_min', 60, EXT_SAVE);
   space_weather.refresh_min = w3_clamp(+space_weather.refresh_min, 10, 180, 60);

   var controls_html =
      w3_div('id-space-weather-controls w3-text-white w3-padding|',
         w3_div('w3-medium w3-text-aqua w3-bold', 'Space Weather'),
         w3_div('id-sw-data w3-small|', w3_div('w3-text-gray', 'Loading NOAA SWPC data...'))
      );

   ext_panel_show(controls_html, null, null);
   ext_set_controls_width_height(420, 390);
   space_weather_fetch_all();
}

function space_weather_fetch_all()
{
   space_weather_cancel_requests();
   var generation = ++space_weather.generation;
   var keys = Object.keys(space_weather.apis);
   var pending = keys.length;
   var data = {};
   var errors = [];

   keys.forEach(function(key) {
      space_weather_fetch(key, space_weather.apis[key], function(value, error) {
         if (generation != space_weather.generation) return;
         if (error) errors.push(key);
         else data[key] = value;
         if (--pending == 0) {
            space_weather.requests = [];
            space_weather_render(data, errors);
            space_weather.refresh_timer =
               setTimeout(space_weather_fetch_all, space_weather.refresh_min * 60 * 1000);
         }
      });
   });
}

function space_weather_fetch(key, url, done)
{
   var xhr = new XMLHttpRequest();
   var completed = false;
   var finish = function(error) {
      if (completed) return;
      completed = true;
      var value = null;
      if (!error) {
         try {
            value = JSON.parse(xhr.responseText);
         } catch (ex) {
            error = 'parse';
         }
      }
      done(value, error);
   };

   xhr.open('GET', url, true);
   xhr.timeout = 15000;
   xhr.onreadystatechange = function() {
      if (xhr.readyState == 4)
         finish(xhr.status == 200? null : 'HTTP '+ xhr.status);
   };
   xhr.onerror = function() { finish('network'); };
   xhr.ontimeout = function() { finish('timeout'); };
   xhr.send();
   space_weather.requests.push(xhr);
}

function space_weather_cancel_requests()
{
   if (space_weather.refresh_timer) {
      clearTimeout(space_weather.refresh_timer);
      space_weather.refresh_timer = null;
   }
   space_weather.requests.forEach(function(xhr) { xhr.abort(); });
   space_weather.requests = [];
}

function space_weather_latest(a)
{
   return a && a.length? a[a.length - 1] : null;
}

function space_weather_number(value)
{
   if (value == null || value === '') return null;
   value = +value;
   return isFinite(value)? value : null;
}

function space_weather_xray_class(flux)
{
   if (flux == null) return '--';
   if (flux >= 1e-4) return 'X'+ (flux / 1e-4).toFixed(1);
   if (flux >= 1e-5) return 'M'+ (flux / 1e-5).toFixed(1);
   if (flux >= 1e-6) return 'C'+ (flux / 1e-6).toFixed(1);
   if (flux >= 1e-7) return 'B'+ (flux / 1e-7).toFixed(1);
   return 'A'+ (flux / 1e-8).toFixed(1);
}

function space_weather_kp_color(kp)
{
   if (kp == null) return space_weather.colors.gray;
   if (kp < 3) return space_weather.colors.green;
   if (kp < 5) return space_weather.colors.yellow;
   if (kp < 7) return space_weather.colors.orange;
   return space_weather.colors.red;
}

function space_weather_row(label, value, color)
{
   return w3_div('sw-row',
      w3_span('sw-label', label),
      w3_span('sw-value|color:'+ color, value)
   );
}

function space_weather_forecast_html(forecast)
{
   if (!forecast || !forecast.length) return '';
   var days = {};
   forecast.forEach(function(item) {
      if (item.observed != 'predicted' || !item.time_tag) return;
      var kp = space_weather_number(item.kp);
      if (kp == null) return;
      var date = item.time_tag.substring(0, 10);
      if (!days[date]) days[date] = { min:kp, max:kp };
      days[date].min = Math.min(days[date].min, kp);
      days[date].max = Math.max(days[date].max, kp);
   });

   var html = w3_div('sw-section-title', 'Planetary K-index forecast');
   Object.keys(days).sort().slice(0, 3).forEach(function(date) {
      var day = days[date];
      html += space_weather_row(
         date, 'Kp '+ day.min.toFixed(1) +' - '+ day.max.toFixed(1),
         space_weather_kp_color(day.max));
   });
   return html;
}

function space_weather_render(data, errors)
{
   var kp_row = space_weather_latest(data.kp);
   var sfi_row = data.sfi && data.sfi[0];
   var xray_row = space_weather_latest(data.xray_bg);
   var wind_row = data.solar_wind && data.solar_wind[0];
   var kp = space_weather_number(kp_row && kp_row.Kp);
   var a = space_weather_number(kp_row && kp_row.a_running);
   var sfi = space_weather_number(sfi_row && sfi_row.flux);
   var xray = space_weather_number(xray_row && xray_row.background);
   var bt = space_weather_number(wind_row && wind_row.bt);
   var bz = space_weather_number(wind_row && wind_row.bz_gsm);

   var html =
      space_weather_row('Solar flux (10.7 cm)', sfi == null? '--' : sfi.toFixed(0),
         sfi == null? space_weather.colors.gray : space_weather.colors.cyan) +
      space_weather_row('Planetary K-index', kp == null? '--' : kp.toFixed(2),
         space_weather_kp_color(kp)) +
      space_weather_row('Running A-index', a == null? '--' : a.toFixed(0),
         space_weather_kp_color(kp)) +
      space_weather_row('X-ray background', space_weather_xray_class(xray),
         xray == null? space_weather.colors.gray : space_weather.colors.cyan) +
      space_weather_row('Solar wind Bt', bt == null? '--' : bt.toFixed(1) +' nT',
         bt == null? space_weather.colors.gray : space_weather.colors.cyan) +
      space_weather_row('Solar wind Bz', bz == null? '--' : (bz >= 0? '+' : '') + bz.toFixed(1) +' nT',
         bz == null? space_weather.colors.gray :
            (bz < 0? space_weather.colors.red : space_weather.colors.green)) +
      space_weather_forecast_html(data.kp_forecast);

   if (errors.length)
      html += w3_div('w3-text-yellow w3-margin-T-8', 'Unavailable feeds: '+ errors.join(', '));
   html += w3_div('sw-footer', 'Source: NOAA SWPC | '+ new Date().toISOString().substring(0, 16).replace('T', ' ') +' UTC');
   w3_innerHTML('id-sw-data', html);
}

function space_weather_blur()
{
   ++space_weather.generation;
   space_weather_cancel_requests();
}

function space_weather_config_html()
{
   space_weather.refresh_min = ext_get_cfg_param('space_weather.refresh_min', 60, EXT_SAVE);
   var s =
      w3_text('w3-text-black',
         'Displays current solar and geomagnetic observations from NOAA SWPC.'
      ) +
      '<hr>' +
      w3_input_get('', 'Refresh interval (10-180 minutes)',
         'space_weather.refresh_min', 'w3_num_set_cfg_cb', 0);

   ext_config_html(space_weather, 'space_weather', 'Space Weather',
      'Space Weather extension configuration', s);
}

function space_weather_help(show)
{
   if (show) {
      var s =
         w3_text('w3-medium w3-bold w3-text-aqua', 'Space Weather Help') + '<br><br>' +
         'Displays compact NOAA SWPC feeds for solar flux, planetary K/A indices, ' +
         'GOES X-ray background, solar-wind magnetic field and the three-day K-index forecast.';
      confirmation_show_content(s, 520, 190);
   }
   return true;
}
