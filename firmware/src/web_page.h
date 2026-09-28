// The configuration page, served from flash by web_server.cpp.
//
// One self-contained file — no external fonts, scripts or styles — because
// the page also has to work over the device's own hotspot, with no internet.
// The form is built from the schema in the script (FIELDS): every setting's
// label, help text, validation and JSON path live in one place, and saving
// sends only what changed. The CRT look is CSS only and can be switched off.
#pragma once

static const char INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PET-PC</title>
<style>
:root{--fg:#3dff72;--dim:#1c8f40;--bg:#040704;--panel:#08110a;--line:#15361d;--glow:rgba(61,255,114,.5)}
[data-ph=amber]{--fg:#ffb31a;--dim:#a8740c;--bg:#070500;--panel:#110c02;--line:#3a2906;--glow:rgba(255,179,26,.45)}
[data-ph=white]{--fg:#e6eeff;--dim:#8891a5;--bg:#05060a;--panel:#0c0e14;--line:#262b38;--glow:rgba(220,232,255,.35)}
*{box-sizing:border-box}
html{background:var(--bg)}
body{margin:0;min-height:100vh;background:var(--bg);color:var(--fg);
 font:15px/1.5 ui-monospace,"SF Mono",Menlo,Consolas,"Liberation Mono",monospace}
.fx{text-shadow:0 0 4px var(--glow)}
.fx::before{content:"";position:fixed;inset:0;pointer-events:none;z-index:40;
 background:repeating-linear-gradient(to bottom,transparent 0 2px,rgba(0,0,0,.28) 2px 3px)}
.fx::after{content:"";position:fixed;inset:0;pointer-events:none;z-index:41;
 background:radial-gradient(ellipse at center,transparent 55%,rgba(0,0,0,.6) 100%)}
.screen{max-width:760px;margin:0 auto;padding:18px 16px 96px}
.fx .screen{animation:on .6s ease-out}
@keyframes on{0%{transform:scale(.55,.004);filter:brightness(5)}35%{transform:scale(1,.004);filter:brightness(3)}
 100%{transform:none;filter:none}}
header{border-bottom:1px dashed var(--line);padding-bottom:10px;margin-bottom:12px}
.title{font-size:20px;letter-spacing:.12em;font-weight:bold}
.ready{margin:2px 0}
.cur{display:inline-block;width:.6em;height:1.05em;background:var(--fg);vertical-align:-3px;margin-left:2px;
 animation:blink 1.1s steps(1) infinite}
@keyframes blink{50%{opacity:0}}
.status{color:var(--dim);font-size:13px;word-break:break-word}
.prefs{float:right;font-size:12px;color:var(--dim)}
.prefs button{padding:1px 6px;margin:0 0 0 4px;font-size:12px}
nav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:14px}
button,select,input{font:inherit;color:inherit}
button{background:transparent;color:var(--fg);border:1px solid var(--fg);border-radius:0;padding:6px 12px;cursor:pointer;
 text-transform:uppercase;letter-spacing:.05em;text-shadow:inherit}
button:hover:not(:disabled),button.on{background:var(--fg);color:var(--bg);text-shadow:none}
button:disabled{opacity:.35;cursor:not-allowed}
nav button{padding:4px 10px}
nav button.mod::after{content:"*"}
.tab{display:none}
.tab.show{display:block}
.fx .tab.show{animation:wipe .3s steps(12)}
@keyframes wipe{from{clip-path:inset(0 0 100% 0)}to{clip-path:inset(0 0 0 0)}}
h2{font-size:15px;margin:22px 0 6px;letter-spacing:.1em}
h2:first-child{margin-top:4px}
h2::before{content:"■ "}
.intro{color:var(--dim);margin:0 0 12px}
.card{border:1px solid var(--line);background:var(--panel);padding:12px 14px;margin:0 0 12px}
.f{margin:0 0 14px;padding-left:10px;border-left:2px solid transparent}
.f.dirty{border-left-color:var(--fg)}
.f.bad{border-left-color:var(--fg);border-left-style:double;border-left-width:4px}
label{display:block;margin-bottom:3px}
label.chk{display:flex;gap:8px;align-items:baseline;cursor:pointer}
input[type=text],input[type=number],input[type=password],select{width:100%;background:var(--bg);color:var(--fg);
 border:1px solid var(--dim);border-radius:0;padding:7px 8px;outline:none}
input:focus,select:focus{border-color:var(--fg);box-shadow:0 0 6px var(--glow)}
input[type=checkbox]{accent-color:var(--fg);width:16px;height:16px;margin:0}
input::placeholder{color:var(--dim);opacity:.7}
.help{color:var(--dim);font-size:13px;margin:4px 0 0}
.err{margin:4px 0 0;font-size:13px}
.err:empty{display:none}
.err:not(:empty){display:inline-block;background:var(--fg);color:var(--bg);padding:0 6px;text-shadow:none}
.row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1;min-width:130px}
.mon{max-width:420px;margin:0 auto 14px;padding:14px;border:1px solid var(--line);border-radius:14px;background:#000;
 box-shadow:inset 0 0 24px rgba(0,0,0,.9),0 0 18px var(--glow)}
.bezel{background:var(--fg);line-height:0}
.bezel img{width:100%;image-rendering:pixelated;mix-blend-mode:multiply;display:block}
.fx .bezel{animation:flick 5s infinite}
@keyframes flick{0%,100%{opacity:1}48%{opacity:1}50%{opacity:.9}52%{opacity:1}}
.kb{display:flex;justify-content:center;gap:14px;margin:6px 0 4px;user-select:none;-webkit-user-select:none}
.key{width:108px;height:74px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;
 border-width:2px;box-shadow:0 4px 0 var(--dim);touch-action:none}
.key:active,.key.held{transform:translateY(3px);box-shadow:0 1px 0 var(--dim)}
.key b{font-size:22px;line-height:1}
.key small{font-size:10px;line-height:1.2;text-transform:none;max-width:100px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}
.center{text-align:center}
#keyres{min-height:1.5em;text-align:center;color:var(--dim)}
table{border-collapse:collapse;width:100%}
td{padding:3px 8px 3px 0;vertical-align:top}
td:first-child{color:var(--dim);white-space:nowrap;width:1%}
.ent{display:grid;grid-template-columns:2.2fr 1.2fr 1fr .9fr;gap:6px;align-items:center;margin-bottom:6px}
.ent .v{font-size:13px;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.enthead{font-size:12px;color:var(--dim)}
.kact{border:1px solid var(--line);padding:10px 12px;margin-bottom:10px}
.kact h3{margin:0 0 8px;font-size:14px;letter-spacing:.08em}
.hide{display:none!important}
.bar{position:fixed;left:0;right:0;bottom:0;z-index:30;background:var(--bg);border-top:1px solid var(--dim);
 padding:10px 16px;display:flex;gap:10px;align-items:center;justify-content:center;flex-wrap:wrap}
#msg{min-width:200px}
#msg.bad{background:var(--fg);color:var(--bg);padding:1px 6px;text-shadow:none}
a{color:var(--fg)}
@media (max-width:560px){.ent{grid-template-columns:1fr 1fr}.key{width:92px}.prefs{float:none;margin-bottom:6px}}
@media (prefers-reduced-motion:reduce){*{animation:none!important}}
</style></head>
<body class="fx"><div class="screen">
<header>
 <div class="prefs">PHOSPHOR <button data-ph="green">GRN</button><button data-ph="amber">AMB</button><button data-ph="white">WHT</button>
  <button id="fxbtn">CRT FX</button></div>
 <div class="title">*** PET-PC ***</div>
 <div class="ready">READY.<span class="cur"></span></div>
 <div class="status" id="status">LOADING...</div>
</header>
<nav id="tabs"></nav>
<main>
<section class="tab" id="t-live">
 <div class="mon"><div class="bezel"><img id="shot" alt="The PET-PC screen, live"></div></div>
 <div class="kb">
  <button class="key" data-k="left"><b>&#9664;</b><small id="kl-left"></small></button>
  <button class="key" data-k="center"><b>&#9679;</b><small>next screen</small></button>
  <button class="key" data-k="right"><b>&#9654;</b><small id="kl-right"></small></button>
 </div>
 <p class="help center">Click = press &middot; keep it down for a second = hold — the same as the real keys.</p>
 <div id="keyres"></div>
</section>
<section class="tab" id="t-keys">
 <p class="intro">What the two outer keys do. Each has a <b>press</b> action and a <b>hold</b> action (keep the key
 down for ~1 s). The centre key is fixed: press = next screen, hold = refresh the weather — on the BASIC screen it
 runs commands, on HOME it picks and toggles switches.</p>
 <div id="keyeds"></div>
</section>
<section class="tab" id="t-screens"><p class="intro">Which screens exist, which one you start on, and the retro extras.</p></section>
<section class="tab" id="t-display"><p class="intro">Brightness, night hours and OLED care. OLED pixels wear with brightness
 and time — these settings are what keep the panel young.</p></section>
<section class="tab" id="t-ha">
 <p class="intro">Optional. PET-PC reads and switches Home Assistant entities over HA's REST API — nothing to install
 on the HA side. Set it up here, press <b>Save</b>, then <b>Test connection</b>.</p>
</section>
<section class="tab" id="t-time"><p class="intro">Where you are (for the forecast) and how the clock keeps time.</p></section>
<section class="tab" id="t-net"><p class="intro">The WiFi network PET-PC joins. If it cannot join, it opens its own hotspot
 <b>PET-PC-xxxx</b> — connect to it and browse to <b>192.168.4.1</b> to fix things here.</p></section>
<section class="tab" id="t-sys">
 <div class="card"><table id="sysinfo"></table></div>
 <div class="row">
  <button id="dl">Download settings</button>
  <button id="reboot">Restart PET-PC</button>
 </div>
 <p class="help">The download is the settings as JSON, without the WiFi password or HA token — handy before
 experimenting. REST API: <code>GET /api/state</code>, <code>POST /api/settings</code> (a JSON patch in the same shape),
 <code>POST /api/key?key=left|center|right&amp;hold=0|1</code>, <code>GET /shot.bmp</code>.
 Documentation: <a href="https://github.com/Krasnov777/esp32-pet-pc" target="_blank" rel="noopener">github.com/Krasnov777/esp32-pet-pc</a>.</p>
</section>
</main>
<div class="bar">
 <button id="save" disabled>Save</button>
 <button id="revert" disabled>Undo changes</button>
 <span id="msg"></span>
</div>
</div>
<script>
'use strict';
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));

// ── constants ───────────────────────────────────────────────────────────────
// Screen names indexed by the firmware's ui::Mode value; ORDER is the centre key's rotation.
const SCREENS=['Clock','Weather','System','Block clock','LIST','BASIC prompt','HOME (Home Assistant)'];
const ORDER=[0,3,1,6,4,5,2];
const SCREEN_OPTS=ORDER.map(i=>[i,SCREENS[i]]);
const TABS=[['live','Live'],['keys','Keys'],['screens','Screens'],['display','Display'],['ha','Home Assistant'],
 ['time','Weather & time'],['net','Network'],['sys','System']];
const KEY_TYPES=[['none','Nothing'],['http','Send an HTTP request (webhook)'],['screen','Show a screen'],
 ['next','Next screen'],['prev','Previous screen'],['weather','Refresh the weather'],
 ['ha_toggle','Toggle a Home Assistant entity'],['ha_service','Call a Home Assistant service']];
const KEY_SHOW={http:['label','url','method'],screen:['screen'],ha_toggle:['label','entity'],
 ha_service:['label','service','entity']};
const TZ=[['UTC','UTC — no daylight saving','UTC0'],
 ['Europe/London','UK, Ireland, Portugal','GMT0BST,M3.5.0/1,M10.5.0'],
 ['Europe/Amsterdam','Central Europe (Amsterdam, Berlin, Paris, Madrid, Rome)','CET-1CEST,M3.5.0,M10.5.0/3'],
 ['Europe/Helsinki','Eastern Europe (Helsinki, Kyiv, Athens, Bucharest)','EET-2EEST,M3.5.0/3,M10.5.0/4'],
 ['Europe/Moscow','Moscow, Istanbul, Minsk','MSK-3'],
 ['Asia/Dubai','Gulf (Dubai)','<+04>-4'],
 ['Asia/Kolkata','India','IST-5:30'],
 ['Asia/Shanghai','China, Singapore, Perth','CST-8'],
 ['Asia/Tokyo','Japan, Korea','JST-9'],
 ['Australia/Sydney','Sydney, Melbourne, Canberra','AEST-10AEDT,M10.1.0,M4.1.0/3'],
 ['Pacific/Auckland','New Zealand','NZST-12NZDT,M9.5.0,M4.1.0/3'],
 ['America/Sao_Paulo','Brazil (São Paulo)','<-03>3'],
 ['America/New_York','US & Canada Eastern','EST5EDT,M3.2.0,M11.1.0'],
 ['America/Chicago','US & Canada Central','CST6CDT,M3.2.0,M11.1.0'],
 ['America/Denver','US & Canada Mountain','MST7MDT,M3.2.0,M11.1.0'],
 ['America/Phoenix','Arizona — no daylight saving','MST7'],
 ['America/Los_Angeles','US & Canada Pacific','PST8PDT,M3.2.0,M11.1.0']];
const RE_URL=/^https?:\/\/[^\s\/]+/i, RE_ENT=/^[a-z0-9_]+\.[a-z0-9_]+$/;

// ── state ───────────────────────────────────────────────────────────────────
let orig=null;            // settings as last loaded from the device
let loaded=false;         // no Save until this is true — a blank form must never be written back
const fields=[];
const get=(o,p)=>p.split('.').reduce((a,k)=>a==null?a:a[k],o);
function put(o,p,v){const ks=p.split('.');let a=o;for(const k of ks.slice(0,-1))a=a[k]??(a[k]={});a[ks.at(-1)]=v}

// ── field builder ───────────────────────────────────────────────────────────
// o: {label, help, kind:'text'|'num'|'check'|'select'|'pass', min,max,step, opts, numeric, ph, req, pat, patMsg, check, into}
function field(tab,path,o){
 const id='f_'+path.replace(/\W/g,'_'), box=document.createElement('div');
 box.className='f';
 let ctl;
 if(o.kind==='check') ctl=`<label class="chk"><input type="checkbox" id="${id}"><span>${o.label}</span></label>`;
 else{
  const input=o.kind==='select'
   ?`<select id="${id}">${o.opts.map(([v,l])=>`<option value="${esc(v)}">${esc(l)}</option>`).join('')}</select>`
   :`<input id="${id}" type="${o.kind==='pass'?'password':o.kind==='num'?'number':'text'}"`+
    (o.min!=null?` min="${o.min}"`:'')+(o.max!=null?` max="${o.max}"`:'')+(o.step?` step="${o.step}"`:'')+
    ` placeholder="${esc(o.ph||'')}" autocomplete="${o.kind==='pass'?'new-password':'off'}" spellcheck="false">`;
  ctl=`<label for="${id}">${o.label}</label>${input}`;
 }
 box.innerHTML=ctl+(o.help?`<p class="help">${o.help}</p>`:'')+'<p class="err"></p>';
 (o.into||$('#t-'+tab)).appendChild(box);
 const el=box.querySelector('#'+id);
 const f={tab,path,box,el,secret:o.kind==='pass',orig:undefined,
  get(){if(o.kind==='check')return el.checked;
   if(o.kind==='num')return el.value===''?NaN:+el.value;
   if(o.numeric)return +el.value;
   return el.value.trim()},
  set(v){if(o.kind==='check')el.checked=!!v;else if(o.kind==='pass')el.value='';else el.value=v??''},
  check(){const v=this.get();
   if(o.kind==='num'){if(isNaN(v))return 'A NUMBER IS NEEDED';if(v<o.min||v>o.max)return `BETWEEN ${o.min} AND ${o.max}`}
   if(o.kind==='text'){if(o.req&&!v)return 'REQUIRED';if(v&&o.pat&&!o.pat.test(v))return o.patMsg}
   return o.check?o.check(v):''}};
 fields.push(f);
 return f;
}
// A field with its own UI: get/set/check supplied by the caller.
function custom(tab,path,box,impl){const f=Object.assign({tab,path,box,secret:false,orig:undefined,check:()=>''},impl);
 fields.push(f);return f}
function h2(tab,text,into){(into||$('#t-'+tab)).insertAdjacentHTML('beforeend',`<h2>${text}</h2>`)}

// ── the settings, tab by tab ────────────────────────────────────────────────
function build(){
 $('#tabs').innerHTML=TABS.map(([k,l])=>`<button data-tab="${k}">${l}</button>`).join('');

 // KEYS
 for(const side of ['left','right'])for(const phase of ['press','hold']){
  const card=document.createElement('div');card.className='kact';
  card.innerHTML=`<h3>${side==='left'?'&#9664; LEFT':'RIGHT &#9654;'} KEY — ${phase.toUpperCase()}</h3>`;
  $('#keyeds').appendChild(card);
  const p=`keys.${side}.${phase}`;
  const types=phase==='hold'?[['same','Same as press'],...KEY_TYPES]:KEY_TYPES;
  const t=field('keys',p+'.type',{kind:'select',label:'Action',opts:types,into:card});
  const sub={
   label:field('keys',p+'.label',{kind:'text',label:'Label',ph:'e.g. Desk up',into:card,
    help:'Shown on the PET-PC screen while the action runs. Blank = the entity or "Left key".',
    check:v=>v.length>15?'15 CHARACTERS AT MOST':''}),
   url:field('keys',p+'.url',{kind:'text',label:'URL',ph:'http://192.168.1.20/button/desk_up/press',into:card,
    help:'Any webhook — an ESPHome button, a Node-RED/n8n flow, an HA webhook. Spaces are encoded for you. https works.'}),
   method:field('keys',p+'.method',{kind:'select',label:'Method',opts:[['POST','POST (empty body) — ESPHome buttons, webhooks'],['GET','GET']],into:card}),
   screen:field('keys',p+'.screen',{kind:'select',label:'Screen',opts:SCREEN_OPTS,numeric:true,into:card,
    help:'Jumps straight there, even to a screen that is not in the centre key\'s rotation.'}),
   service:field('keys',p+'.service',{kind:'text',label:'Service',ph:'scene.turn_on',into:card,
    help:'domain.service — e.g. <code>scene.turn_on</code>, <code>script.turn_on</code>, '+
     '<code>input_select.select_next</code> (cycle a selector), <code>light.turn_on</code>.'}),
   entity:field('keys',p+'.entity',{kind:'text',label:'Entity',ph:'light.desk_lamp',into:card,
    help:'The entity_id, as in HA → Settings → Entities. Needs the Home Assistant tab set up.'}),
  };
  sub.url.check=function(){const v=this.get();return !v?'REQUIRED':!RE_URL.test(v)?'START WITH http:// OR https://':''};
  sub.entity.check=function(){const v=this.get(),need=t.get()==='ha_toggle';
   return !v?(need?'REQUIRED':''):!RE_ENT.test(v)?'LIKE light.desk_lamp':''};
  sub.service.check=function(){const v=this.get();return !v?'REQUIRED':!RE_ENT.test(v)?'LIKE scene.turn_on':''};
  const test=document.createElement('div');
  test.innerHTML=`<button data-try="${side}" data-hold="${phase==='hold'?1:0}">Try it</button>
   <span class="help">runs the <i>saved</i> action on the device</span>`;
  card.appendChild(test);
  const sync=()=>{const show=KEY_SHOW[t.get()]||[];
   for(const [k,f] of Object.entries(sub))f.box.classList.toggle('hide',!show.includes(k))};
  t.el.addEventListener('change',sync);t.sync=sync;
 }

 // SCREENS
 h2('screens','Rotation');
 const sb=document.createElement('div');sb.className='f';
 sb.innerHTML='<label>Screens on the centre key</label>'+ORDER.map(i=>
  `<label class="chk"><input type="checkbox" data-scr="${i}"><span>${SCREENS[i]}</span></label>`).join('')+
  '<p class="help">The centre key steps through the ticked screens in this order. HOME only appears once Home Assistant is set up.</p><p class="err"></p>';
 $('#t-screens').appendChild(sb);
 custom('screens','retro.screens',sb,{
  get:()=>$$('[data-scr]').reduce((m,c)=>m|(c.checked?1<<+c.dataset.scr:0),0),
  set:v=>$$('[data-scr]').forEach(c=>c.checked=!!(v>>+c.dataset.scr&1)),
  check:()=>$$('[data-scr]').some(c=>c.checked)?'':'TICK AT LEAST ONE'});
 field('screens','display.start_mode',{kind:'select',label:'Start screen',opts:SCREEN_OPTS,numeric:true,
  help:'Shown after the boot banner, every time PET-PC starts.'});
 h2('screens','Retro extras');
 field('screens','retro.saver_min',{kind:'num',label:'Screensaver after (minutes idle)',min:0,max:240,
  help:'After this long without a key press the <code>10 PRINT</code> maze runs for a minute, then the screen comes back. 0 = never. It also rests the OLED.'});
 field('screens','retro.fx',{kind:'check',label:'CRT effects on the OLED',
  help:'Scan-line wipe between screens, a vertical roll for automatic changes, and a power-off collapse when the night blank starts.'});
 field('screens','retro.tape',{kind:'check',label:'"PRESS PLAY ON TAPE" on each weather refresh',
  help:'A ~7 s tape-loading sequence around the real fetch, then <code>RUN</code> prints the reading.'});
 field('screens','retro.boot',{kind:'check',label:'BASIC boot banner',
  help:'<code>*** PET-PC ***</code>, the memory count-up and <code>READY.</code> at power-on, instead of a plain boot card.'});

 // DISPLAY
 h2('display','Brightness');
 field('display','display.contrast_day',{kind:'num',label:'Day brightness (0–255)',min:0,max:255,
  help:'70 reads well in a lit room. Higher is brighter — and wears the OLED faster.'});
 field('display','display.contrast_night',{kind:'num',label:'Night brightness (0–255)',min:0,max:255,
  help:'<b>0 switches the panel off</b> for the night — a key press lights it dimly for 10 s, and still does its job.'});
 h2('display','Night hours');
 const nr=document.createElement('div');nr.className='row';$('#t-display').appendChild(nr);
 field('display','display.night_from',{kind:'num',label:'Night starts at (hour)',min:0,max:23,into:nr});
 field('display','display.night_to',{kind:'num',label:'Night ends at (hour)',min:0,max:23,into:nr});
 $('#t-display').insertAdjacentHTML('beforeend','<p class="help" style="margin:-8px 0 14px 10px">23 → 7 means 23:00 to 07:00. The same hour in both = no night mode. Needs the time to be synced.</p>');
 h2('display','Clock and care');
 field('display','clock_24h',{kind:'check',label:'24-hour clock',help:'Off = 12-hour with AM/PM.'});
 field('display','display.burn_in_shift',{kind:'check',label:'Burn-in pixel shift',
  help:'Nudges the whole picture by up to 2 px every two minutes, so no pixel shows the same thing for months.'});

 // HOME ASSISTANT
 h2('ha','Connection');
 field('ha','ha.url',{kind:'text',label:'Home Assistant URL',ph:'http://homeassistant.local:8123',
  pat:RE_URL,patMsg:'START WITH http:// OR https://',
  help:'With the port. <code>https://</code> works; any certificate is accepted (self-signed is normal on a LAN).'});
 const tok=field('ha','ha.token',{kind:'pass',label:'Long-lived access token',ph:'(saved — leave blank to keep it)',
  help:'In HA: your profile → Security → Long-lived access tokens → Create token. Stored on PET-PC and never shown again. '+
   'Changing the URL clears it, so it is never sent to a server it was not entered for.'});
 tok.check=function(){const v=this.get();return v&&v.length<30?'THAT IS TOO SHORT FOR A TOKEN':''};
 $('#t-ha').insertAdjacentHTML('beforeend','<div class="f"><button id="hatest">Test connection</button> <span id="hares" class="help"></span></div>');
 h2('ha','Entities on the HOME screen');
 $('#t-ha').insertAdjacentHTML('beforeend','<p class="intro"><b>Value</b> shows the state as HA reports it (a temperature, a status). '+
  '<b>Switch</b> shows ON/OFF and is toggled on the HOME screen: centre press to pick it, centre hold to switch it. '+
  'Label is optional (11 characters; blank = HA\'s name). Now = what PET-PC currently reads.</p>');
 const eb=document.createElement('div');eb.className='f';
 eb.innerHTML='<div class="ent enthead"><span>Entity id</span><span>Label</span><span>Type</span><span>Now</span></div>'+
  [0,1,2,3,4,5].map(i=>`<div class="ent"><input type="text" data-e="${i}" placeholder="${i?'':'sensor.living_room_temperature'}" spellcheck="false" autocomplete="off">`+
  `<input type="text" data-l="${i}" maxlength="11"><select data-t="${i}"><option value="value">Value</option><option value="switch">Switch</option></select>`+
  `<span class="v" data-v="${i}"></span></div>`).join('')+'<p class="err"></p>';
 $('#t-ha').appendChild(eb);
 $$('[data-e]').forEach(inp=>inp.addEventListener('change',()=>{
  $(`[data-t="${inp.dataset.e}"]`).value=/^(light|switch|fan|input_boolean|automation|cover)\./.test(inp.value.trim())?'switch':'value';dirty()}));
 custom('ha','ha.entities',eb,{
  get:()=>[0,1,2,3,4,5].map(i=>({id:$(`[data-e="${i}"]`).value.trim().toLowerCase(),
   label:$(`[data-l="${i}"]`).value.trim(),type:$(`[data-t="${i}"]`).value})),
  set:v=>v.forEach((e,i)=>{$(`[data-e="${i}"]`).value=e.id;$(`[data-l="${i}"]`).value=e.label;$(`[data-t="${i}"]`).value=e.type}),
  check(){const bad=this.get().find(e=>e.id&&!RE_ENT.test(e.id));return bad?`"${bad.id}" IS NOT AN ENTITY ID`:''}});
 field('ha','ha.poll_s',{kind:'num',label:'Read everything within (seconds)',min:10,max:600,
  help:'One small request per entity, spread over this time. While the HOME screen is showing, PET-PC reads about one entity per second.'});

 // WEATHER & TIME
 h2('time','Location');
 const lr=document.createElement('div');lr.className='row';$('#t-time').appendChild(lr);
 field('time','loc.lat',{kind:'num',label:'Latitude',min:-90,max:90,step:.0001,into:lr});
 field('time','loc.lon',{kind:'num',label:'Longitude',min:-180,max:180,step:.0001,into:lr});
 $('#t-time').insertAdjacentHTML('beforeend','<div class="f"><button id="geo">Use this browser\'s location</button>'+
  '<p class="help">Only used for the weather forecast. Two decimals (~1 km) is plenty.</p></div>');
 h2('time','Clock');
 const tzsel=document.createElement('div');tzsel.className='f';
 tzsel.innerHTML='<label for="tzp">Time zone</label><select id="tzp"><option value="">Custom (edit below)</option>'+
  TZ.map(([id,l,s])=>`<option value="${esc(s)}">${esc(l)}</option>`).join('')+'</select><p class="help" id="tzhint"></p>';
 $('#t-time').appendChild(tzsel);
 const tzf=field('time','loc.tz',{kind:'text',label:'Time zone rule (POSIX TZ)',req:true,
  help:'Filled in when you pick a zone above; daylight saving is part of the rule. Only edit by hand for a zone not listed.'});
 $('#tzp').addEventListener('change',e=>{if(e.target.value){tzf.el.value=e.target.value;dirty()}});
 tzf.el.addEventListener('input',()=>$('#tzp').value=TZ.some(z=>z[2]===tzf.el.value.trim())?tzf.el.value.trim():'');
 field('time','loc.ntp',{kind:'text',label:'Time server (NTP)',req:true,ph:'pool.ntp.org',
  help:'<code>pool.ntp.org</code> works anywhere. Use your router\'s address if it runs one.'});
 h2('time','Weather');
 field('time','weather.minutes',{kind:'num',label:'Refresh every (minutes)',min:5,max:180,
  help:'Open-Meteo updates its data about every 15 minutes.'});
 field('time','weather.host',{kind:'text',label:'Weather API host',req:true,ph:'api.open-meteo.com',
  help:'Free, no account or key. Only change it to point at a proxy or mirror.'});

 // NETWORK
 field('net','wifi.ssid',{kind:'text',label:'WiFi network (SSID)',req:true,
  help:'2.4 GHz only — the ESP32-C6 does not do 5 GHz. Changing it restarts PET-PC to join the new network.'});
 field('net','wifi.pass',{kind:'pass',label:'WiFi password',ph:'(saved — leave blank to keep it)',
  help:'Stored on PET-PC and never shown again.'});
 field('net','wifi.hostname',{kind:'text',label:'Device name',req:true,pat:/^[a-z0-9-]{1,23}$/i,
  patMsg:'LETTERS, DIGITS AND - ONLY (23 AT MOST)',
  help:'PET-PC answers at <code>http://&lt;name&gt;.local/</code>. Takes effect after a restart.'});
}

// ── load / fill / dirty ─────────────────────────────────────────────────────
function fill(s){
 orig=s;
 for(const f of fields){f.set(get(s,f.path));f.box.classList.remove('dirty','bad');
  const e=f.box.querySelector('.err');if(e)e.textContent=''}
 for(const f of fields)f.orig=f.secret?'':f.get();
 for(const f of fields)if(f.sync)f.sync();
 $('#t-ha').querySelector('#f_ha_token').placeholder=s.ha.token_set?'(saved — leave blank to keep it)':'(none saved)';
 $('#f_wifi_pass').placeholder=s.wifi.pass_set?'(saved — leave blank to keep it)':'(none saved)';
 const tz=TZ.find(z=>z[2]===s.loc.tz);$('#tzp').value=tz?tz[2]:'';
 let bz='';try{bz=Intl.DateTimeFormat().resolvedOptions().timeZone}catch(e){}
 const bt=TZ.find(z=>z[0]===bz);
 $('#tzhint').innerHTML=bz?`This browser is in <b>${esc(bz)}</b>`+(bt&&bt[2]!==s.loc.tz?` — pick “${esc(bt[1])}” to match it.`:'.'):'';
 keyLabels(s);
 dirty();
}
const isDirty=f=>f.secret?f.get()!=='':JSON.stringify(f.get())!==JSON.stringify(f.orig);
function dirty(){
 if(!loaded)return;
 let n=0;
 for(const f of fields){const d=isDirty(f);f.box.classList.toggle('dirty',d);if(d)n++}
 for(const [k] of TABS)$(`[data-tab="${k}"]`).classList.toggle('mod',fields.some(f=>f.tab===k&&isDirty(f)));
 $('#save').disabled=!n;$('#revert').disabled=!n;
 if(n)say(`${n} UNSAVED CHANGE${n>1?'S':''}`);else if(/UNSAVED/.test($('#msg').textContent))say('');
}
function say(t,bad){const m=$('#msg');m.textContent=t;m.classList.toggle('bad',!!bad)}
async function load(){
 for(let i=0;;i++){
  try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw 0;const s=await r.json();
   loaded=true;fill(s);live(s);return}
  catch(e){say(i?'?DEVICE NOT PRESENT  ERROR — RETRYING':'LOADING SETTINGS...',!!i);await new Promise(r=>setTimeout(r,2500))}
 }
}

// ── save ────────────────────────────────────────────────────────────────────
const visible=f=>!f.box.classList.contains('hide')&&!f.box.closest('.hide');
async function save(){
 if(!loaded)return;
 const ch=fields.filter(isDirty);
 if(!ch.length){say('NOTHING TO SAVE.');return}
 let first=null;
 for(const f of fields){const e=isDirty(f)&&visible(f)?f.check():'';
  const el=f.box.querySelector('.err');if(el)el.textContent=e?'?'+e:'';f.box.classList.toggle('bad',!!e);
  if(e&&!first)first=f}
 if(first){showTab(first.tab);(first.el||first.box).scrollIntoView({block:'center'});say('?SYNTAX  ERROR — SEE THE MARKED FIELD',true);return}

 const has=p=>ch.some(f=>f.path===p);
 if((has('wifi.ssid')||has('wifi.pass'))&&!confirm(`PET-PC will restart and join "${get(patchOf(ch),'wifi.ssid')||orig.wifi.ssid}".\n\n`+
  'If it cannot, it opens its PET-PC-xxxx hotspot again within about half a minute. Continue?'))return;
 if(has('ha.url')&&!has('ha.token')&&orig.ha.token_set&&!confirm('Changing the Home Assistant URL clears the saved token, so it is never sent to '+
  'a server it was not entered for.\n\nOK = save without a token (paste it again afterwards). Cancel = go back and paste it now.'))return;

 $('#save').disabled=true;say('SAVING...');
 let j;
 try{const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(patchOf(ch))});
  if(!r.ok)throw new Error('HTTP '+r.status);j=await r.json()}
 catch(e){say('?SAVE ERROR — '+e.message,true);dirty();return}
 if(j.wifi_changed){fetch('/api/reboot',{method:'POST'});loaded=false;
  say('SAVED. RESTARTING TO JOIN THE NEW NETWORK — reconnect there.');return}
 const hostCh=has('wifi.hostname');
 const r=await fetch('/api/state',{cache:'no-store'});fill(await r.json());
 say(hostCh?'SAVED. THE NEW NAME APPLIES AFTER A RESTART (System tab).':'SAVED.');
}
function patchOf(ch){const p={};for(const f of ch)put(p,f.path,f.get());return p}

// ── live status, screen mirror, keys ────────────────────────────────────────
function live(s){
 const l=s.live,up=v=>{const h=Math.floor(v/3600),d=Math.floor(h/24);return d?`${d}d ${h%24}h`:h?`${h}h ${Math.floor(v/60)%60}m`:`${Math.floor(v/60)}m`};
 $('#status').textContent=`${l.ap?'HOTSPOT MODE · ':''}${l.ip} · ${l.rssi?l.rssi+' dBm':'no signal'} · FW ${l.fw} · UP ${up(l.uptime_s)} · `+
  `TIME ${l.time_ok?'OK':'NOT SYNCED'} · HA ${l.ha.status.toUpperCase()}${l.screen_on?'':' · SCREEN OFF (night)'}`;
 l.ha.values.forEach((v,i)=>{const e=$(`[data-v="${i}"]`);if(e)e.textContent=v});
 $('#sysinfo').innerHTML=[['Firmware',l.fw],['Address',`${l.ip}${l.ap?' (hotspot)':''} — http://${s.wifi.hostname}.local/`],
  ['WiFi',l.ap?'not connected (hotspot mode)':`${s.wifi.ssid}, ${l.rssi} dBm`],['Up for',up(l.uptime_s)],
  ['Free memory',`${l.heap} bytes`],['Clock',l.time_ok?'synced':'waiting for the time server'],
  ['Weather',s.wx.valid?`${s.wx.temp.toFixed(1)} °C, ${s.wx.desc}`:'no reading yet'],['Home Assistant',l.ha.status]]
  .map(([k,v])=>`<tr><td>${k}</td><td>${esc(v)}</td></tr>`).join('');
}
async function poll(){
 try{const r=await fetch('/api/state',{cache:'no-store'});live(await r.json())}
 catch(e){$('#status').textContent='?DEVICE NOT PRESENT  ERROR — is PET-PC on and on this network?'}
}
function keyLabels(s){
 const d=a=>({none:'—',http:a.label||'webhook',screen:SCREENS[a.screen],next:'next screen',prev:'prev screen',
  weather:'weather',ha_toggle:a.label||a.entity,ha_service:a.label||a.service})[a.type]||'—';
 for(const side of ['left','right']){const k=s.keys[side];
  $('#kl-'+side).textContent=d(k.press)+(k.hold.type!=='same'?' / hold: '+d(k.hold):'')}
}
let shotTimer=null;
function shot(){$('#shot').src='/shot.bmp?'+Date.now()}
async function key(k,hold){
 $('#keyres').textContent=`${k.toUpperCase()} ${hold?'HOLD':'PRESS'}...`;
 try{const r=await (await fetch(`/api/key?key=${k}&hold=${hold?1:0}`,{method:'POST'})).json();
  $('#keyres').textContent=`${k.toUpperCase()} ${hold?'HOLD':'PRESS'}: ${r.result}`}
 catch(e){$('#keyres').textContent='?DEVICE NOT PRESENT  ERROR'}
 setTimeout(shot,300);
}

// ── tabs & wiring ───────────────────────────────────────────────────────────
function showTab(t){
 if(!TABS.some(x=>x[0]===t))t='live';
 $$('.tab').forEach(s=>s.classList.toggle('show',s.id==='t-'+t));
 $$('#tabs button').forEach(b=>b.classList.toggle('on',b.dataset.tab===t));
 if(location.hash!=='#'+t)history.replaceState(null,'','#'+t);
 clearInterval(shotTimer);
 if(t==='live'){shot();shotTimer=setInterval(shot,2000)}
}
function prefs(){
 let ph='green',fx=true;
 try{ph=localStorage.ph||'green';fx=localStorage.fx!=='0'}catch(e){}
 document.documentElement.dataset.ph=ph;document.body.classList.toggle('fx',fx);
 $$('[data-ph]').forEach(b=>b.classList.toggle('on',b.dataset.ph===ph));$('#fxbtn').classList.toggle('on',fx);
}
function setPref(k,v){try{localStorage[k]=v}catch(e){}prefs()}

build();prefs();
document.addEventListener('input',e=>{if(e.target.closest('main'))dirty()});
document.addEventListener('change',e=>{if(e.target.closest('main'))dirty()});
$('#tabs').addEventListener('click',e=>{const b=e.target.closest('button');if(b)showTab(b.dataset.tab)});
$('#save').onclick=save;
$('#revert').onclick=()=>{if(orig){fill(orig);say('CHANGES UNDONE.')}};
$$('[data-ph]').forEach(b=>b.onclick=()=>setPref('ph',b.dataset.ph));
$('#fxbtn').onclick=()=>setPref('fx',document.body.classList.contains('fx')?'0':'1');
$('#geo').onclick=()=>{if(!navigator.geolocation){say('?NO LOCATION IN THIS BROWSER',true);return}
 navigator.geolocation.getCurrentPosition(p=>{$('#f_loc_lat').value=p.coords.latitude.toFixed(4);
  $('#f_loc_lon').value=p.coords.longitude.toFixed(4);dirty()},
  ()=>say('?LOCATION REFUSED — browsers only allow it on https or localhost; type it in instead.',true))};
$('#hatest').onclick=async()=>{
 const dirtyHa=fields.some(f=>f.tab==='ha'&&isDirty(f));
 $('#hares').textContent=dirtyHa?'Save first — the test uses the saved settings.':'testing...';if(dirtyHa)return;
 try{const r=await (await fetch('/api/ha/test',{method:'POST'})).json();
  $('#hares').textContent=r.ok?'CONNECTED — Home Assistant answered.':r.http===401||r.http===403?`TOKEN REJECTED (HTTP ${r.http}) — create a new one.`:
   r.http===0?'Save a URL and a token first.':r.http<0?'NO ANSWER — check the URL and port, and that HA is up.':`FAILED (HTTP ${r.http}).`}
 catch(e){$('#hares').textContent='?DEVICE NOT PRESENT  ERROR'}};
$('#keyeds').addEventListener('click',e=>{const b=e.target.closest('[data-try]');if(!b)return;
 if(fields.some(f=>f.tab==='keys'&&isDirty(f))){say('SAVE FIRST — "Try it" runs the saved action.',true);return}
 key(b.dataset.try,b.dataset.hold==='1')});
$('#reboot').onclick=async()=>{if(!confirm('Restart PET-PC now?'))return;
 await fetch('/api/reboot',{method:'POST'}).catch(()=>{});say('RESTARTING — this page reconnects by itself.');loaded=false;
 setTimeout(load,6000)};
$('#dl').onclick=()=>{if(!orig)return;const s=JSON.parse(JSON.stringify(orig));delete s.live;delete s.wx;
 const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(s,null,2)],{type:'application/json'}));
 a.download='pet-pc-settings.json';a.click()};
// Virtual keys: a click is a press, a second down is a hold — fired while still held, like the real keys.
$$('.key').forEach(b=>{let t=null,held=false;
 b.addEventListener('pointerdown',e=>{e.preventDefault();held=false;b.classList.add('held');
  t=setTimeout(()=>{held=true;key(b.dataset.k,true)},800)});
 const up=()=>{if(t===null)return;clearTimeout(t);t=null;b.classList.remove('held');if(!held)key(b.dataset.k,false)};
 b.addEventListener('pointerup',up);b.addEventListener('pointerleave',()=>{if(t!==null){clearTimeout(t);t=null;b.classList.remove('held')}})});
window.addEventListener('beforeunload',e=>{if(loaded&&fields.some(isDirty)){e.preventDefault();e.returnValue=''}});
window.addEventListener('hashchange',()=>showTab(location.hash.slice(1)));
showTab(location.hash.slice(1));
load();setInterval(poll,5000);
</script></body></html>)HTML";
