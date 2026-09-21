(() => {
  let failed=false;
  window.gpuFluidFailure=error=>{
    if(failed)return;failed=true;window.gpuFluidStopped=true;
    document.documentElement.dataset.state='error';document.getElementById('status').hidden=false;
    document.getElementById('statusTitle').textContent='GPU fluid could not start';
    document.getElementById('statusDetail').textContent=String(error?.message||error)+ ' — This preview has no WebGPU. Opening the WebGL volumetric fallback...';
    document.getElementById('retry').hidden=false;
    document.getElementById('deviceBadge').textContent='WebGPU unavailable or failed';
    const link=document.createElement('a'); link.href='volumetric-webgl.html'; link.textContent='▶ Open WebGL fallback (works in preview)'; link.style.cssText='display:inline-block;margin-top:12px;padding:8px 14px;background:#0ea5e9;color:#fff;border-radius:999px;text-decoration:none;font-weight:600'; document.getElementById('status').appendChild(link);
    setTimeout(()=> location.href='volumetric-webgl.html', 2200);
    console.error(error);
  };
  document.getElementById('retry').onclick=()=>location.reload();
  window.addEventListener('error',event=>{if(event.error)window.gpuFluidFailure(event.error);});
  window.addEventListener('unhandledrejection',event=>window.gpuFluidFailure(event.reason));
  import('./gpu-fluid-app.js?v=size1').catch(window.gpuFluidFailure);
})();
