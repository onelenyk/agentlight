// Єдина веб-сторінка девайса: статус, тест кольорів, яскравість, налаштування WiFi, дебаг-меню.
#pragma once
#include <Arduino.h>

const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AgentLight</title>
<style>
body{font:16px system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:480px;margin:auto}
h1{font-size:22px;display:flex;align-items:center;gap:12px}
h2{font-size:15px;color:#999;margin:24px 0 8px;text-transform:uppercase}
#dot{width:22px;height:22px;border-radius:50%;background:#333}
.card{background:#1c1c1c;border-radius:10px;padding:12px}
button,select,input{font:inherit;padding:10px;border-radius:8px;border:1px solid #444;background:#222;color:#eee}
button{cursor:pointer}
.row{display:flex;gap:8px;flex-wrap:wrap}
.row>*{flex:1}
label{display:block;margin:8px 0 4px;color:#aaa;font-size:14px}
input,select{width:100%;box-sizing:border-box}
code{display:block;background:#000;padding:8px;border-radius:6px;font-size:13px;overflow-x:auto;white-space:nowrap}
small{color:#888}
.agent{background:#1c1c1c;border-radius:10px;padding:10px 12px;margin-bottom:8px;border-left:5px solid #333}
.agent .head{display:flex;justify-content:space-between;gap:8px;align-items:baseline}
.agent .name{font-weight:600}
.agent .task{margin-top:6px}
.agent .msg{margin-top:4px;color:#aaa;font-size:14px}
.agent .meta{margin-top:6px;color:#666;font-size:12px}
.agent div{overflow-wrap:anywhere}
details{margin-top:24px}
summary{cursor:pointer;font-size:15px;color:#999;text-transform:uppercase;padding:8px 0}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:14px;margin:0}
.kv dt{color:#888}.kv dd{margin:0;overflow-wrap:anywhere}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:0 10px}
input[type=color]{padding:2px;height:42px}
#touchdot{display:inline-block;width:12px;height:12px;border-radius:50%;background:#333;margin-right:6px}
</style></head><body>
<h1><span id="dot"></span>AgentLight <small id="state"></small></h1>

<div id="setup" class="card" hidden>
  Для роботи підключи девайс до WiFi.
</div>

<div id="work" hidden>
  <h2>Агенти</h2>
  <div id="agents"></div>
  <h2>Підключення</h2>
  <div class="card">
    <label>MCP для Claude Code</label>
    <code id="mcpcmd"></code>
    <label>REST (для хуків)</label>
    <code id="restcmd"></code>
  </div>
</div>

<h2>Тест</h2>
<div class="row">
  <button onclick="setState('busy')">🟠 busy</button>
  <button onclick="setState('waiting')">🔴 waiting</button>
  <button onclick="setState('done')">🟢 done</button>
  <button onclick="setState('idle')">⚫ idle</button>
</div>
<label>Яскравість <span id="bval"></span></label>
<input type="range" id="bright" min="5" max="255" onchange="setBright(this.value)">

<h2>WiFi</h2>
<div class="card">
  <div id="wifiinfo"></div>
  <label>Збережені мережі</label>
  <div id="saved"></div>
  <small>Лампа сама підключається до тієї зі збережених, яку бачить поруч. Якщо жодної немає — за 20 с піднімає точку доступу.</small>
  <label>Мережа</label>
  <div class="row"><select id="ssid"></select><button style="flex:0" onclick="scan()">↻</button></div>
  <label>Пароль</label>
  <input type="password" id="pass">
  <div class="row" style="margin-top:12px">
    <button onclick="saveWifi()">Додати і підключитись</button>
    <button onclick="resetWifi()">Забути всі</button>
  </div>
  <small id="msg"></small>
</div>

<details id="debug">
<summary>Дебаг</summary>
<div class="card">
  <dl class="kv" id="info"></dl>
</div>

<h2>Сенсор</h2>
<div class="card">
  <div><span id="touchdot"></span><span id="touchinfo"></span></div>
  <div class="grid2">
    <div><label>Короткий дотик</label><select id="tap"></select></div>
    <div><label>Утримання</label><select id="hold"></select></div>
  </div>
</div>

<h2>Діоди і кольори</h2>
<div class="card">
  <label>Скільки діодів світити (на кожному виході)</label>
  <input type="number" id="leds" min="1">
  <div class="grid2">
    <div><label>Працює</label><input type="color" id="c_busy"></div>
    <div><label>Чекає</label><input type="color" id="c_waiting"></div>
    <div><label>Готово</label><input type="color" id="c_done"></div>
    <div><label>Помилка</label><input type="color" id="c_error"></div>
  </div>
</div>

<h2>Коли гасне, хв (0 — ніколи)</h2>
<div class="card">
  <div class="grid2">
    <div><label>Працює, без подій</label><input type="number" id="ttl_busy" min="0" max="1440"></div>
    <div><label>Готово</label><input type="number" id="ttl_done" min="0" max="1440"></div>
    <div><label>Чекає / помилка</label><input type="number" id="ttl_attention" min="0" max="1440"></div>
  </div>
  <div class="row" style="margin-top:12px"><button onclick="saveCfg()">Зберегти налаштування</button></div>
  <small id="cfgmsg"></small>
</div>

<h2>Дії</h2>
<div class="row">
  <button onclick="act('rainbow')">Веселка 10 с</button>
  <button onclick="act('clear')">Очистити агентів</button>
  <button onclick="setState('error')">🔴 error</button>
</div>
<div class="row" style="margin-top:8px">
  <button onclick="act('unmute')">Увімкнути світло</button>
  <button onclick="act('defaults')">Типові налаштування</button>
  <button onclick="act('reboot')">Перезавантажити</button>
</div>
</details>

<script>
const $=id=>document.getElementById(id);
const colors={idle:'#333',busy:'#ff8000',waiting:'#ff2020',done:'#20d040',error:'#ff2020'};
const labels={busy:'працює',waiting:'чекає на тебе',done:'готово',error:'помилка'};
const dur=s=>s<60?s+' с':s<3600?Math.floor(s/60)+' хв':Math.floor(s/3600)+' год '+Math.floor(s%3600/60)+' хв';
function add(parent,cls,text){const d=document.createElement('div');d.className=cls;d.textContent=text;parent.appendChild(d);return d;}
const post=(url,body)=>fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
let brightTouched=false;
async function refresh(){
  try{
    const s=await (await fetch('/api/status')).json();
    $('dot').style.background=colors[s.state]||'#333';
    $('state').textContent=s.state;
    $('setup').hidden=!s.wifi.portal; $('work').hidden=s.wifi.portal;
    $('wifiinfo').textContent=s.wifi.portal?('Режим налаштування, точка доступу '+s.wifi.ap)
      :(s.wifi.ssid+' · '+s.wifi.ip+' · '+s.wifi.rssi+' dBm');
    const sv=$('saved'); sv.textContent='';
    if(!s.wifi.saved.length) add(sv,'','немає');
    for(const n of s.wifi.saved){
      const r=add(sv,'row','');r.style.marginBottom='6px';r.style.alignItems='center';
      add(r,'',n+(n===s.wifi.ssid?' · підключена':''));
      const b=document.createElement('button');b.textContent='Забути';b.style.flex='0';b.onclick=()=>resetWifi(n);r.appendChild(b);
    }
    const box=$('agents'); box.textContent='';
    if(!s.agents.length) add(box,'card','немає активних');
    for(const a of s.agents){
      const card=add(box,'agent','');card.style.borderLeftColor=colors[a.state]||'#333';
      const head=add(card,'head','');
      add(head,'name',a.name||a.id);
      add(head,'',(labels[a.state]||a.state)+' · '+dur(a.since)).style.color=colors[a.state];
      if(a.task) add(card,'task',a.task);
      if(a.message) add(card,'msg',a.message);
      add(card,'meta',a.id+' · оновлено '+dur(a.age)+' тому');
    }
    $('mcpcmd').textContent='claude mcp add --transport http agentlight http://'+s.wifi.ip+'/mcp';
    $('restcmd').textContent="curl -X POST 'http://"+s.wifi.ip+"/api/status?state=busy'";
    if(!brightTouched){$('bright').value=s.brightness;$('bval').textContent=s.brightness;}
  }catch(e){}
}
function setState(state){post('/api/status',{state,agent_id:'web'}).then(refresh);}
function setBright(v){brightTouched=true;$('bval').textContent=v;post('/api/config',{brightness:+v});}
async function scan(){
  $('msg').textContent='Шукаю мережі…';
  try{
    const list=await (await fetch('/api/scan')).json();
    const sel=$('ssid'); sel.textContent='';
    for(const n of list){const o=document.createElement('option');o.value=n.ssid;
      o.textContent=n.ssid+' ('+n.rssi+' dBm)';sel.appendChild(o);}
    $('msg').textContent=list.length?'':'Мереж не знайдено';
  }catch(e){$('msg').textContent='Помилка сканування';}
}
function saveWifi(){
  if(!$('ssid').value)return;
  post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value});
  $('msg').textContent='Збережено. Девайс перезавантажується і підключається; якщо за 20 с не вийде — знову підніме точку доступу.';
}
function resetWifi(ssid){post('/api/wifi/reset',ssid?{ssid}:{});$('msg').textContent=(ssid?'Мережу «'+ssid+'» забуто':'Усі мережі забуто')+', девайс перезавантажується.';}
const actLabels={none:'нічого',dismiss:'побачив: гасить готово і чекає',brightness:'яскравість по колу',mute:'вимкнути / увімкнути світло'};
const infoLabels={uptime:'Працює',reset:'Причина запуску',build:'Прошивка зібрана',chip:'Чип',mac:'MAC',heap:'Вільна пам\'ять',heap_min:'Мінімум пам\'яті',led_pins:'Піни діодів',led_max:'Діодів на вихід, макс.',touch_pin:'Пін сенсора'};
let cfgLoaded=false;
function fillCfg(d){
  const c=d.settings;
  for(const id of ['tap','hold']){
    const sel=$(id); sel.textContent='';
    for(const a of d.actions){const o=document.createElement('option');o.value=a;o.textContent=actLabels[a]||a;sel.appendChild(o);}
    sel.value=c[id];
  }
  $('leds').max=d.device.led_max; $('leds').value=c.leds;
  for(const k of ['ttl_busy','ttl_done','ttl_attention']) $(k).value=c[k];
  for(const k in c.colors) $('c_'+k).value=c.colors[k];
  cfgLoaded=true;
}
async function refreshDebug(){
  if(!$('debug').open)return;
  try{
    const d=await (await fetch('/api/debug')).json();
    const dl=$('info'); dl.textContent='';
    const row=(k,v)=>{const dt=document.createElement('dt');dt.textContent=k;const dd=document.createElement('dd');dd.textContent=v;dl.append(dt,dd);};
    for(const k in d.device){
      let v=d.device[k];
      if(k==='uptime')v=dur(v); else if(k.startsWith('heap'))v=Math.round(v/1024)+' КБ';
      row(infoLabels[k]||k,v);
    }
    row('WiFi',d.wifi.portal?'точка доступу '+d.wifi.ap:d.wifi.ssid+' · '+d.wifi.rssi+' dBm');
    row('Світло',d.muted?'вимкнене дотиком':'увімкнене');
    $('touchdot').style.background=d.touch.raw?'#20d040':'#333';
    $('touchinfo').textContent=(d.touch.raw?'палець на сенсорі':'не торкаються')+' · дотиків: '+d.touch.count+(d.touch.count?' · останній '+dur(d.touch.ago)+' тому':'');
    if(!cfgLoaded)fillCfg(d);
  }catch(e){}
}
async function saveCfg(){
  const body={leds:+$('leds').value,tap:$('tap').value,hold:$('hold').value,colors:{}};
  for(const k of ['ttl_busy','ttl_done','ttl_attention']) body[k]=+$(k).value;
  for(const k of ['busy','waiting','done','error']) body.colors[k]=$('c_'+k).value;
  try{fillCfg(await (await post('/api/config',body)).json());$('cfgmsg').textContent='Збережено';}
  catch(e){$('cfgmsg').textContent='Не вдалося зберегти';}
}
async function act(action){
  try{const r=await post('/api/action',{action});if(action==='defaults')fillCfg(await r.json());}catch(e){}
  refresh();
}
$('debug').addEventListener('toggle',refreshDebug);
refresh();setInterval(refresh,2000);setInterval(refreshDebug,500);scan();
</script></body></html>)HTML";
