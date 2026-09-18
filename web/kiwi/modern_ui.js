// Copyright (c) 2026 WEB-888 contributors

var modern_ui = {
   storage_key: 'web888_ui_theme',
   themes: [
      { id:'midnight', label:'Midnight' },
      { id:'ember', label:'Ember' },
      { id:'cloud', label:'Cloud' },
      { id:'classic', label:'Classic' }
   ]
};

function modern_ui_theme_valid(theme)
{
   return modern_ui.themes.some(function(entry) { return entry.id == theme; });
}

function modern_ui_stored_theme()
{
   var theme = kiwi_storeGet(modern_ui.storage_key, 'midnight');
   return modern_ui_theme_valid(theme)? theme : 'midnight';
}

function modern_ui_set_theme(theme, persist)
{
   if (!modern_ui_theme_valid(theme)) theme = 'midnight';
   document.documentElement.setAttribute('data-ui-theme', theme);
   var classic = theme == 'classic';
   document.documentElement.classList.toggle('ui-modern', !classic);
   document.documentElement.classList.toggle('ui-classic', classic);
   if (document.body) {
      document.body.classList.toggle('ui-modern', !classic);
      document.body.classList.toggle('ui-classic', classic);
   }

   if (persist != false) kiwi_storeSet(modern_ui.storage_key, theme);

   var select = document.getElementById('id-ui-theme-select');
   if (select && select.value != theme) select.value = theme;
   return theme;
}

function modern_ui_theme_cb(value)
{
   modern_ui_set_theme(value, true);
}

function modern_ui_mount(target_id)
{
   var mount = target_id? document.getElementById(target_id) : document.querySelector('[data-ui-theme-mount]');
   if (!mount) return;

   var picker = document.querySelector('.ui-theme-picker');
   if (picker) {
      mount.appendChild(picker);
      return;
   }

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
document.addEventListener('DOMContentLoaded', function() { modern_ui_mount(); });
