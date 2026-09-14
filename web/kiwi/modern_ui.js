// Copyright (c) 2026 WEB-888 contributors

var modern_ui = {
   storage_key: 'web888_ui_theme',
   themes: [
      { id:'midnight', label:'Midnight' },
      { id:'ember', label:'Ember' },
      { id:'daylight', label:'Daylight' }
   ]
};

function modern_ui_theme_valid(theme)
{
   return modern_ui.themes.some(function(entry) { return entry.id == theme; });
}

function modern_ui_stored_theme()
{
   var theme = null;
   try {
      theme = window.localStorage? localStorage.getItem(modern_ui.storage_key) : null;
   } catch (ex) {}
   return modern_ui_theme_valid(theme)? theme : 'midnight';
}

function modern_ui_set_theme(theme, persist)
{
   if (!modern_ui_theme_valid(theme)) theme = 'midnight';
   document.documentElement.setAttribute('data-ui-theme', theme);
   document.documentElement.classList.add('ui-modern');
   if (document.body) document.body.classList.add('ui-modern');

   if (persist != false) {
      try {
         if (window.localStorage) localStorage.setItem(modern_ui.storage_key, theme);
      } catch (ex) {}
   }

   var select = document.getElementById('id-ui-theme-select');
   if (select && select.value != theme) select.value = theme;
   return theme;
}

function modern_ui_theme_cb(value)
{
   modern_ui_set_theme(value, true);
}

function modern_ui_mount()
{
   var mount = document.querySelector('[data-ui-theme-mount]');
   if (!mount || document.getElementById('id-ui-theme-select')) return;

   var options = '';
   modern_ui.themes.forEach(function(theme) {
      options += '<option value="'+ theme.id +'">'+ theme.label +'</option>';
   });
   mount.innerHTML =
      '<label class="ui-theme-picker" title="Color theme">' +
         '<span class="ui-theme-picker-label">Theme</span>' +
         '<select id="id-ui-theme-select" class="ui-theme-select" aria-label="Color theme" ' +
            'onchange="modern_ui_theme_cb(this.value)">'+ options +'</select>' +
      '</label>';
   modern_ui_set_theme(modern_ui_stored_theme(), false);
}

modern_ui_set_theme(modern_ui_stored_theme(), false);
document.addEventListener('DOMContentLoaded', modern_ui_mount);
