// Prefer a normal HTTP attachment for proxied/embedded previews. Keep explicit
// user-gesture save/copy fallbacks: a sandbox can silently block automatic downloads.
export async function PrepareCsvDownload(Csv,Status){
 Status.replaceChildren();Status.textContent='Preparing CSV download…';
 let Href,BlobUrl,Server=false;
 try{
  const Response=await fetch('/api/telemetry/exports',{method:'POST',headers:{'Content-Type':'text/csv;charset=utf-8'},body:Csv,signal:AbortSignal.timeout(15000)});
  if(!Response.ok)throw new Error('Export server unavailable');
  const Result=await Response.json();
  if(typeof Result.url!=='string'||!/^\/api\/telemetry\/exports\/[a-f0-9]{32}\.csv$/.test(Result.url))throw new Error('Invalid download URL');
  Href=Result.url;Server=true;
 }catch{
  BlobUrl=URL.createObjectURL(new Blob(['\ufeff',Csv],{type:'text/csv;charset=utf-8'}));Href=BlobUrl;
 }
 Status.replaceChildren();
 const Notice=document.createElement('span');Notice.textContent=Server?'CSV ready. Download requested; if nothing appears, click Save CSV below. Link available for up to 10 minutes.':'CSV ready using a browser file. If the preview blocks saving, use Show CSV and copy the selected text.';
 const Link=document.createElement('a');Link.href=Href;Link.download='frontier-session.csv';Link.target='_blank';Link.rel='noopener';Link.textContent='Save CSV';Link.className='action-button';
 const Show=document.createElement('button');Show.type='button';Show.className='action-button';Show.textContent='Show CSV / copy manually';
 let Text;
 Show.addEventListener('click',()=>{
  if(!Text){Text=document.createElement('textarea');Text.className='telemetry-select';Text.readOnly=true;Text.rows=8;Text.setAttribute('aria-label','Complete session CSV');Text.value=Csv;Status.appendChild(Text);}
  Text.focus();Text.select();Notice.textContent='Complete CSV selected. Press Ctrl+C / Cmd+C to copy; paste into a .csv file.';
 });
 Status.append(Notice,document.createElement('br'),Link,Show);
 // Attach before click and retain long enough for browsers/proxies to consume it.
 const Timer=BlobUrl?setTimeout(()=>URL.revokeObjectURL(BlobUrl),600000):null;
 return {Link,Server,Dispose(){if(Timer)clearTimeout(Timer);if(BlobUrl)URL.revokeObjectURL(BlobUrl);}};
}
