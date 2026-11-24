// Generated from page.html by tools/embed_page.py. Do not edit.
#pragma once
static const char PAGE_HTML[] PROGMEM = R"GHOSTPAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ghost</title>
<style>
:root{--bg:#0d0f14;--card:#171a22;--txt:#e8eaf0;--mut:#8a90a0;--ok:#3ccf8e;--warn:#f5b942;--bad:#ff5c5c;--acc:#7aa2ff}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--txt)}
main{max-width:560px;margin:auto;padding:14px}h1{font-size:20px;margin:4px 0 12px}
.card{background:var(--card);border-radius:14px;padding:14px;margin-bottom:12px}
#state{font-size:34px;font-weight:700;text-align:center;padding:22px 8px;border-radius:14px;transition:background .3s}
#state small{display:block;font-size:14px;font-weight:400;color:var(--mut)}
.s-NO_SIGNAL{background:#2a2d36}.s-EMPTY{background:#1c2a3a}.s-STILL{background:#1d3a2e}.s-MOVING{background:#24413a}
.s-CHECKING{background:#4a3a12}.s-FALL{background:#5a1717;animation:pulse 1s infinite}
@keyframes pulse{50%{background:#8a1f1f}}
canvas{width:100%;height:120px;display:block;background:#0f1218;border-radius:8px}
#wf{height:150px;image-rendering:pixelated}
button{background:#262b38;color:var(--txt);border:0;border-radius:10px;padding:10px 12px;font-size:15px;margin:3px}
button.pri{background:var(--acc);color:#0d0f14;font-weight:600}button.bad{background:var(--bad);color:#fff}
button.on{outline:2px solid var(--warn)}
.row{display:flex;flex-wrap:wrap;gap:4px;align-items:center}.mut{color:var(--mut);font-size:13px}
label{display:flex;justify-content:space-between;margin:6px 0}input{width:110px;background:#0f1218;color:var(--txt);border:1px solid #333;border-radius:6px;padding:4px}
table{width:100%;font-size:13px;border-collapse:collapse}td{padding:3px 2px;border-bottom:1px solid #222}
</style></head><body><main>
<h1>Ghost <span class="mut" id="room"></span></h1>
<div id="state" class="s-NO_SIGNAL">Connecting...<small id="sub"></small></div>
<div class="row" style="justify-content:center;margin:8px 0" id="okrow" hidden><button class="pri" onclick="post('/api/ack')">I'm OK</button></div>

<div class="card"><div class="row" style="justify-content:space-between"><b>Motion</b><span class="mut" id="nums"></span></div>
<canvas id="ch" width="520" height="120"></canvas>
<div class="mut">Blue: motion. Orange: change from the empty room. Dashed: thresholds.</div></div>

<div class="card"><b>Wi-Fi signal, 52 subcarriers</b><canvas id="wf" width="300" height="52"></canvas>
<div class="mut">Each column is a tenth of a second. Brighter means stronger.</div></div>

<div class="card"><b>Set up the room</b>
<p class="mut">Leave the room empty and still, then press Calibrate. Ghost learns what an empty room looks like.</p>
<div class="row"><button class="pri" onclick="calib()">Calibrate (30 s)</button><span id="cal" class="mut"></span></div></div>

<div class="card"><b>Record training data</b>
<p class="mut">Press Record, then tap what you are doing as you do it. Save when done.</p>
<div class="row"><button id="rec" onclick="toggleRec()">Record</button><button onclick="save()">Save</button><span id="recinfo" class="mut"></span></div>
<div class="row" id="labels"></div></div>

<div class="card"><b>Alerts</b>
<p class="mut">Install the free ntfy app and subscribe to topic <b id="topic">...</b> to get fall alerts on any phone.</p>
<div class="row"><button onclick="post('/api/test-alert')">Send test alert</button></div></div>

<div class="card"><b>Recent events</b><table id="ev"></table></div>

<div class="card"><details><summary><b>Settings</b></summary>
<form id="cfg" onsubmit="return saveCfg(event)">
<label>Room name<input name="room"></label>
<label>Motion threshold<input name="motionTh" type="number" step="any"></label>
<label>Room change threshold<input name="devTh" type="number" step="any"></label>
<label>Burst threshold<input name="spikeTh" type="number" step="any"></label>
<label>Seconds still before alert<input name="stillSecs" type="number" step="any"></label>
<label>Longest fall burst (s)<input name="maxBurstSecs" type="number" step="any"></label>
<label>Use trained model (0/1)<input name="modelOn" type="number"></label>
<button class="pri">Save settings</button></form>
<p><button class="bad" onclick="if(confirm('Forget Wi-Fi and restart?'))post('/api/reset-wifi')">Forget Wi-Fi</button></p>
<p class="mut" id="diag"></p></details></div>
</main>
<script>
const NAMES=['NO_SIGNAL','EMPTY','STILL','MOVING','CHECKING','FALL'];
const NICE={NO_SIGNAL:'No signal',EMPTY:'Room empty',STILL:'Someone still',MOVING:'Someone moving',CHECKING:'Checking...',FALL:'Possible fall!'};
const LABELS=['empty','walk','sit','stand still','fall','lie down','get up'];
let frames=[],last=0,st={},rec=null,curLabel=null,cfgLoaded=false;
const $=id=>document.getElementById(id);
function post(u,body){return fetch(u,{method:'POST',body:body||''}).then(r=>r.json()).catch(()=>{})}
function calib(){post('/api/calibrate',new URLSearchParams({secs:30}))}
LABELS.forEach(l=>{const b=document.createElement('button');b.textContent=l;b.onclick=()=>{curLabel=l;
 document.querySelectorAll('#labels button').forEach(x=>x.classList.toggle('on',x===b));
 if(rec)rec.labels.push({t:Date.now(),seq:last,label:l})};$('labels').appendChild(b)});
function toggleRec(){if(rec){rec.stopped=Date.now();$('rec').textContent='Record';$('rec').classList.remove('on')}
 else{rec={started:Date.now(),frames:[],labels:[],events:[]};$('rec').textContent='Stop';$('rec').classList.add('on')}}
function save(){if(!rec)return;rec.cfg=st.cfg;rec.events=st.events;rec.room=st.room;
 const blob=new Blob([JSON.stringify(rec)],{type:'application/json'});const name='ghost-'+new Date().toISOString().replace(/[:.]/g,'-')+'.json';
 const file=new File([blob],name,{type:'application/json'});
 if(navigator.canShare&&navigator.canShare({files:[file]}))navigator.share({files:[file]}).catch(()=>dl(blob,name));else dl(blob,name)}
function dl(blob,name){const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;a.click()}
function saveCfg(e){e.preventDefault();post('/api/config',new URLSearchParams(new FormData($('cfg'))));return false}
async function poll(){
 try{st=await (await fetch('/api/state')).json();render()}catch(e){$('state').className='s-NO_SIGNAL';$('state').firstChild.nodeValue='Board unreachable'}
 try{const r=await (await fetch('/api/frames?since='+last)).json();
  for(const f of r.frames){frames.push(f);last=f[0];if(rec&&!rec.stopped)rec.frames.push(f.concat([curLabel]))}
  if(frames.length>300)frames.splice(0,frames.length-300);draw()}catch(e){}
 setTimeout(poll,500)}
function render(){const s=st.state;$('state').className='s-'+s;$('state').firstChild.nodeValue=NICE[s]||s;
 $('okrow').hidden=s!=='FALL'&&s!=='CHECKING';$('room').textContent=st.room||'';$('topic').textContent=st.topic;
 $('sub').textContent=st.senderSeen<0?'Waiting for the sender board':(st.hasBase?'':'Not calibrated yet');
 $('nums').textContent=st.motion!==undefined?`motion ${st.motion.toFixed(3)} · change ${st.dev.toFixed(3)} · ${st.rate} pkt/s · ${st.rssi} dBm`:'';
 $('cal').textContent=st.calib>0&&st.calib<1?`Calibrating ${(st.calib*100)|0}%... stay out of the room`:(st.hasBase?'Calibrated':'');
 $('recinfo').textContent=rec?`${rec.frames.length} frames, ${rec.labels.length} labels`:'';
 $('diag').textContent=`CSI packets ${st.csi}, dropped ${st.dropped}, uptime ${(st.uptime/1000|0)} s`;
 $('ev').innerHTML=(st.events||[]).slice().reverse().map(e=>`<tr><td>${new Date(Date.now()-(st.uptime-e.t)).toLocaleTimeString()}</td><td>${e.outcome}</td><td>${e.prob.toFixed(2)}</td></tr>`).join('');
 if(!cfgLoaded&&st.cfg){cfgLoaded=true;const f=$('cfg');for(const k in st.cfg)if(f[k])f[k].value=st.cfg[k];f.room.value=st.room||''}}
function draw(){const c=$('ch'),g=c.getContext('2d'),W=c.width,H=c.height;g.clearRect(0,0,W,H);
 const cfg=st.cfg||{motionTh:.05,devTh:.02};const top=Math.max(cfg.motionTh*4,...frames.map(f=>f[2]/1e4),...frames.map(f=>f[4]/1e4),.01);
 const y=v=>H-4-(v/top)*(H-8),x=i=>i*W/299;
 g.setLineDash([4,4]);g.strokeStyle='#7aa2ff55';g.beginPath();g.moveTo(0,y(cfg.motionTh));g.lineTo(W,y(cfg.motionTh));g.stroke();
 g.strokeStyle='#f5b94255';g.beginPath();g.moveTo(0,y(cfg.devTh));g.lineTo(W,y(cfg.devTh));g.stroke();g.setLineDash([]);
 [[2,'#7aa2ff'],[4,'#f5b942']].forEach(([k,col])=>{g.strokeStyle=col;g.lineWidth=2;g.beginPath();
  frames.forEach((f,i)=>i?g.lineTo(x(i),y(f[k]/1e4)):g.moveTo(x(i),y(f[k]/1e4)));g.stroke()});
 const w=$('wf'),wg=w.getContext('2d'),img=wg.createImageData(300,52);
 frames.forEach((f,i)=>{const h=f[8]||'';for(let k=0;k<52;k++){const v=parseInt(h.substr(k*2,2),16)||0;const b=Math.min(255,v*1.6);
  const p=((51-k)*300+i)*4;img.data[p]=b*.5;img.data[p+1]=b*.8;img.data[p+2]=b;img.data[p+3]=255}});
 wg.putImageData(img,0,0)}
poll();
</script></body></html>
)GHOSTPAGE";
