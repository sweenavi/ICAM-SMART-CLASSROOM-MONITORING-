'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const svgNS = 'http://www.w3.org/2000/svg';
  let mode = new URLSearchParams(location.search).get('source') === 'device' ? 'device' : 'demo';
  let scenario = 'normal';
  let history = [], events = [], previous = null, latest = null;
  let lastSuccess = 0, generation = 0, pending = false, lastHistoryTime = 0;
  let demoBuzzerUntil = 0, toastTimer;
  const clock = time => new Date(time).toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'});
  const sample = (time = Date.now()) => {
    const warm = scenario === 'warm', gas = scenario === 'gas';
    const temperature = +(warm ? 34.6 + .25 * Math.sin(time / 17000) : 30.4 + .25 * Math.sin(time / 17000)).toFixed(1);
    const humidity = +(62 + .7 * Math.sin(time / 23000)).toFixed(1);
    const count = scenario === 'empty' ? 0 : 24;
    const bright = scenario === 'bright';
    return {source:'icam-demo', temperature, humidity, dhtOK:true, studentCount:count,
      occupied:count >= 2, ambientLightDetected:bright, ldrRaw:bright ? 0 : 1, currentAmps:0.18, currentMilliAmps:180,
      fanOn:count >= 2 && temperature > 33, lightRelayOn:count >= 2 && !bright,
      mq2Raw:Math.round((gas ? 2800 : 486) + 12 * Math.sin(time / 9000)),
      gasUnsafe:gas, buzzerOn:Date.now() < demoBuzzerUntil, mode:'AUTO', time};
  };
  function setText(id, value) { $(id).textContent = value; }
  function toast(message) {
    setText('toast', message); $('toast').hidden = false;
    clearTimeout(toastTimer); toastTimer = setTimeout(() => $('toast').hidden = true, 3500);
  }
  function addEvent(title, description, icon = 'i-pulse', time = Date.now()) {
    events.unshift({title,description,icon,time}); events = events.slice(0,50); renderEvents();
  }
  function icon(name) {
    const svg = document.createElementNS(svgNS, 'svg');
    const use = document.createElementNS(svgNS, 'use'); use.setAttribute('href','#'+name); svg.append(use); return svg;
  }
  function renderEvents() {
    const list = $('activity-list'); list.replaceChildren();
    events.slice(0,3).forEach(event => {
      const li = document.createElement('li'), marker = document.createElement('span'), content = document.createElement('div'), title = document.createElement('strong'), desc = document.createElement('p'), time = document.createElement('time');
      marker.className = 'event-marker'; marker.append(icon(event.icon)); content.className = 'event-content';
      title.textContent = event.title; desc.textContent = event.description; content.append(title,desc);
      time.textContent = clock(event.time); time.dateTime = new Date(event.time).toISOString(); li.append(marker,content,time); list.append(li);
    });
    setText('event-count', events.length + ' events');
  }
  function state(id, on, alarming = false) {
    setText(id, on ? (alarming ? 'Sounding' : 'ON') : (alarming ? 'Silent' : 'OFF'));
    $(id).className = 'device-state' + (on ? (alarming ? ' alarm' : ' on') : '');
  }
  function render(data) {
    latest = data; lastSuccess = Date.now();
    setText('temperature', data.dhtOK ? data.temperature.toFixed(1) : '—');
    setText('humidity', data.dhtOK ? Math.round(data.humidity) : '—');
    setText('temp-status', !data.dhtOK ? 'Sensor error' : data.temperature > 33 ? 'Above threshold' : 'Below threshold');
    $('temp-status').classList.toggle('error-text', !data.dhtOK || data.temperature > 33);
    setText('students', data.studentCount); setText('occupancy',data.occupied ? 'Occupied' : 'Empty');
    $('occupancy').className = 'pill ' + (data.occupied ? 'green' : 'neutral');
    const visual = $('occupancy-visual'); visual.replaceChildren();
    for (let i=0; i<12; i++) { const person=icon('i-users'); if (i < Math.min(data.studentCount,12)) person.classList.add('filled'); visual.append(person); }
    setText('gas-status', data.gasUnsafe ? 'Unsafe' : 'Safe'); $('gas-status').classList.toggle('error-text',data.gasUnsafe);
    setText('gas-raw',data.mq2Raw); setText('current-amps',Number.isFinite(data.currentAmps) ? data.currentAmps.toFixed(3) : '—'); setText('current-ma',Number.isFinite(data.currentMilliAmps) ? Math.round(data.currentMilliAmps) : '—'); $('gas-meter-fill').style.width = (data.mq2Raw / 4095 * 100) + '%';
    $('gas-meter-fill').style.background = data.gasUnsafe ? '#ce8b7d' : '#8dbba4';
    state('fan-state',data.fanOn); state('light-state',data.lightRelayOn); state('buzzer-state',data.buzzerOn,true);
    setText('ambient-state', data.ambientLightDetected ? 'Detected · bright' : 'Not detected · dark');
    setText('legend-temp',data.dhtOK ? data.temperature.toFixed(1)+'°C' : 'Error');
    setText('legend-humidity',data.dhtOK ? Math.round(data.humidity)+'%' : 'Error');
    $('source-banner')?.classList.remove('offline');
    document.querySelector('.source-banner').classList.remove('offline');
    if (mode === 'device') {
      setText('source-title','Connected to your ESP32'); setText('source-description','Live, read-only data from the ESP32 offline access point. Outputs remain controlled by firmware.');
      setText('data-badge','● Device connected'); $('data-badge').className='pill green';
    }
    if (previous) {
      if (previous.studentCount !== data.studentCount) addEvent('Student count updated',data.studentCount + ' counted · dual IR sequence','i-users');
      if (previous.fanOn !== data.fanOn) addEvent('Fan turned ' + (data.fanOn ? 'on' : 'off'),'Automatic relay output · GPIO 15','i-fan');
      if (previous.lightRelayOn !== data.lightRelayOn) addEvent('Lighting turned ' + (data.lightRelayOn ? 'on' : 'off'),'Automatic relay output · GPIO 16','i-bulb');
      if (previous.dhtOK !== data.dhtOK) addEvent(data.dhtOK ? 'DHT22 recovered' : 'DHT22 reading failed','Temperature and humidity validity changed','i-temp');
      if (previous.gasUnsafe !== data.gasUnsafe) addEvent(data.gasUnsafe ? 'Gas threshold exceeded' : 'Gas reading below threshold','MQ-2 raw ADC: '+data.mq2Raw+' · threshold > 2500','i-shield');
    } else {
      addEvent('Lighting is ' + (data.lightRelayOn ? 'on' : 'off'),'Initial observed relay state','i-bulb');
      addEvent(data.occupied ? 'Classroom is occupied' : 'Classroom is empty',data.studentCount + ' students counted · threshold ≥ 2','i-users');
      addEvent(mode === 'demo' ? 'Demo monitoring started' : 'Device monitoring connected',mode === 'demo' ? 'Illustrative data · no hardware connected' : 'Read-only snapshot from ESP32','i-wifi');
    }
    previous = {...data};
    if (Date.now() - lastHistoryTime >= 5000 || !history.length) {
      history.push({...data,time:Date.now()}); lastHistoryTime = Date.now();
      history = history.filter(row => row.time >= Date.now() - 5*60*1000).slice(-301);
    }
    drawChart(); updateAge();
  }
  function drawChart() {
    const svg = $('trend-chart'); svg.replaceChildren();
    const now = Date.now(), minutes = Number($('chart-range').value), start = now - minutes*60000;
    const rows = history.filter(row => row.time >= start);
    const x = time => 38 + (time-start)/(now-start)*604;
    const y = value => 174 - Math.min(100,Math.max(0,value))/100*150;
    const node = (tag,attrs,text) => {const el=document.createElementNS(svgNS,tag); Object.entries(attrs).forEach(([key,value])=>el.setAttribute(key,value)); if(text!==undefined)el.textContent=text; svg.append(el); return el;};
    [0,25,50,75,100].forEach(value => {node('line',{x1:38,y1:y(value),x2:642,y2:y(value),class:'grid-line'});node('text',{x:25,y:y(value)+3,'text-anchor':'end'},String(value));});
    for(let i=0;i<=4;i++) { const time=start+(now-start)*i/4; node('text',{x:x(time),y:200,'text-anchor':i===0?'start':i===4?'end':'middle'},new Date(time).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:minutes===1?'2-digit':undefined})); }
    [['humidity','#7ea7d1'],['temperature','#dca16c']].forEach(([key,color]) => {
      let points=[];
      const flush=()=>{if(!points.length)return;const path=points.map((p,i)=>(i?'L':'M')+p[0].toFixed(1)+','+p[1].toFixed(1)).join(' ');node('path',{d:path+' L'+points[points.length-1][0]+',174 L'+points[0][0]+',174 Z',fill:color,opacity:'.055'});node('path',{d:path,stroke:color,'stroke-width':2,fill:'none'});const end=points[points.length-1];node('circle',{cx:end[0],cy:end[1],r:2.6,fill:color,stroke:'white','stroke-width':1});points=[];};
      rows.forEach(row=>{if(row.dhtOK && Number.isFinite(row[key]))points.push([x(row.time),y(row[key])]);else flush();});flush();
    });
    $('chart-empty').hidden = rows.some(row=>row.dhtOK);
  }
  function valid(data) {
    return data && data.source==='icam-esp32' && ['occupied','ambientLightDetected','fanOn','lightRelayOn','gasUnsafe','buzzerOn','dhtOK'].every(key=>typeof data[key]==='boolean') &&
      Number.isInteger(data.studentCount) && data.studentCount>=0 && Number.isInteger(data.mq2Raw) && data.mq2Raw>=0 && data.mq2Raw<=4095 &&
      Number.isFinite(data.currentAmps) && data.currentAmps>=0 && Number.isFinite(data.currentMilliAmps) && data.currentMilliAmps>=0 &&
      (!data.dhtOK || (Number.isFinite(data.temperature) && Number.isFinite(data.humidity)));
  }
  async function poll() {
    if (mode==='demo') {render(sample());return;}
    if(pending)return; pending=true;
    const token = generation, controller=new AbortController(), timeout=setTimeout(()=>controller.abort(),3000);
    try {
      const response=await fetch('/api/status',{cache:'no-store',signal:controller.signal});
      if(!response.ok)throw Error('HTTP '+response.status);
      const data=await response.json(); if(!valid(data))throw Error('Invalid ICAM response');
      if(token!==generation)return;
      const wasOffline=$('data-badge').textContent==='Device offline';
      render({...data,time:Date.now()});
      if(wasOffline)addEvent('Device connection restored','Fresh readings received from ESP32','i-wifi');
    } catch(error) {
      if(token!==generation)return;
      if($('data-badge').textContent!=='Device offline')addEvent('Device not reachable','Open this dashboard on the ESP32’s local IP address','i-wifi');
      setText('data-badge','Device offline'); $('data-badge').className='pill red';
      document.querySelector('.source-banner').classList.add('offline');
      setText('source-title','No connection to the ESP32');
      setText('source-description',lastSuccess ? 'Readings below are stale. Reconnecting; never assume these are current device states.' : 'No device data received. Connect to ICAM-Classroom and open http://192.168.4.1.');
      setText('updated-label',lastSuccess ? 'Stale · last '+clock(lastSuccess) : 'No device readings');
    } finally {clearTimeout(timeout);pending=false;}
  }
  function clearReadings() {
    ['temperature','humidity','students','occupancy','gas-status','gas-raw','current-amps','current-ma','fan-state','light-state','buzzer-state','ambient-state','legend-temp','legend-humidity','temp-status'].forEach(id=>setText(id,'—'));
    ['fan-state','light-state','buzzer-state'].forEach(id=>$(id).className='device-state');
    $('occupancy').className='pill neutral'; $('gas-status').classList.remove('error-text'); $('temp-status').classList.remove('error-text');
    $('gas-meter-fill').style.width='0%'; $('occupancy-visual').replaceChildren();
  }
  function configure(next, nextScenario=scenario) {
    const oldScenario=scenario;
    mode=next;scenario=nextScenario;generation++;history=[];events=[];previous=null;latest=null;lastSuccess=0;lastHistoryTime=0;
    demoBuzzerUntil=next==='demo' && nextScenario==='gas' ? Date.now()+5000 : 0;
    clearReadings(); renderEvents();
    document.querySelector('.source-banner').classList.remove('offline');
    setText('source-title',mode==='demo'?'You’re exploring demo mode':'Connecting to your ESP32…');
    setText('source-description',mode==='demo'?'Simulated readings. No physical classroom devices are connected.':'Requesting /api/status on this page’s host. No commands are sent.');
    setText('data-badge',mode==='demo'?'● Demo data':'Connecting…');$('data-badge').className='pill neutral';
    setText('updated-label','Waiting for readings');
    setText('chart-session',mode==='demo'?'Simulated history':'Device session');
    setText('activity-source',mode==='demo'?'Demo events · Not a device audit log':'Observed changes · Not a persistent device log');
    $('source-button').replaceChildren(document.createTextNode(mode==='demo'?'Connect your ESP32':'Data source & setup'),icon('i-arrow'));
    if(mode==='demo')for(let i=60;i>=1;i--)history.push(sample(Date.now()-i*5000));
    drawChart();poll();
  }
  function updateAge() {
    if(!lastSuccess || $('data-badge').textContent==='Device offline')return;
    const seconds=Math.floor((Date.now()-lastSuccess)/1000);
    setText('updated-label',(mode==='demo'?'Demo · ':'')+(seconds<2?'Updated just now':'Updated '+seconds+'s ago'));
  }
  $('source-button').addEventListener('click',()=>$('source-dialog').showModal());
  $('close-dialog').addEventListener('click',()=>$('source-dialog').close());
  $('source-dialog').addEventListener('click',event=>{if(event.target===$('source-dialog')){const r=event.target.getBoundingClientRect();if(event.clientX<r.left||event.clientX>r.right||event.clientY<r.top||event.clientY>r.bottom)event.target.close();}});
  $('demo-button').addEventListener('click',()=>{configure('demo',$('demo-scenario').value);$('source-dialog').close();toast('Demo scenario loaded. No physical devices are controlled.');});
  $('live-button').addEventListener('click',()=>{configure('device');$('source-dialog').close();});
  $('chart-range').addEventListener('change',drawChart);
  $('database-button').addEventListener('click',()=>{
    if(mode!=='device'){toast('Connect to the ESP32 to download its persistent offline database. Demo readings are not stored on the device.');return;}
    location.href='/api/log.csv';
  });
  $('export-button').addEventListener('click',()=>{
    if(!history.length){toast('No readings available to export yet.');return;}
    const fields=['time','source','temperature','humidity','dhtOK','studentCount','occupied','ldrRaw','ambientLightDetected','mq2Raw','mq2AdcVoltage','currentAmps','currentMilliAmps','fanOn','lightRelayOn','gasUnsafe','buzzerOn'];
    const csv=fields.join(',')+'\n'+history.map(row=>fields.map(key=>key==='time'?new Date(row.time).toISOString():row[key]??'').join(',')).join('\n');
    const url=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'})), link=document.createElement('a');
    link.href=url;link.download='icam-'+mode+'-'+new Date().toISOString().slice(0,10)+'.csv';document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);toast((mode==='demo'?'Simulated':'Device')+' readings exported as CSV.');
  });
  document.querySelectorAll('nav .nav-link').forEach(link=>link.addEventListener('click',()=>{document.querySelectorAll('nav .nav-link').forEach(el=>el.classList.remove('active'));link.classList.add('active');}));
  configure(mode);setInterval(poll,1000);setInterval(updateAge,1000);
})();