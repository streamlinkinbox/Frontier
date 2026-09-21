(() => {
  let failed=false;
  window.gpuFluidFailure=error=>{
    if(failed)return;failed=true;window.gpuFluidStopped=true;
    document.documentElement.dataset.state='error';document.getElementById('status').hidden=false;
    document.getElementById('statusTitle').textContent='GPU fluid could not start';
    document.getElementById('statusDetail').textContent=String(error?.message||error);
    document.getElementById('retry').hidden=false;
    document.getElementById('deviceBadge').textContent='WebGPU unavailable or failed';
    console.error(error);
  };
  document.getElementById('retry').onclick=()=>location.reload();
  window.addEventListener('error',event=>{if(event.error)window.gpuFluidFailure(event.error);});
  window.addEventListener('unhandledrejection',event=>window.gpuFluidFailure(event.reason));
  import('./gpu-fluid-app.js?v=size1').catch(window.gpuFluidFailure);
})();
