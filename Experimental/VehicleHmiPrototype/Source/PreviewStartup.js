// Static dependency failures occur before IlluminationHost can show its own failure panel.
// Catch them here instead of leaving the user on an indefinite "Starting" screen.
import('./IlluminationHost.js?v=lighting-training-2').catch(Error0=>{
    console.error('Frontier startup module failed:',Error0);
    document.getElementById('bring-up')?.classList.add('hidden');
    const Panel=document.getElementById('failure');
    if(Panel){Panel.classList.remove('hidden');Panel.textContent='Could not load the app runtime. '+String(Error0?.message??Error0)+' — check the preview server dependencies, then reload.';}
});
