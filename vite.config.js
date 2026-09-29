import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
export default defineConfig({
  server: { host: '0.0.0.0', allowedHosts: ['.e2b.app'], port: 5173 },
  plugins: [{
    name: 'specimen-preview-entry',
    configureServer(server) {
      const specimen=process.env.FRONTIER_SPECIMEN;
      if(!['frog','crab'].includes(specimen)) return;
      server.middlewares.use((req,res,next)=>{
        if(req.url === '/') { res.writeHead(302,{Location:`/${specimen}.html`});res.end(); }
        else next();
      });
    }
  }],
  build: { chunkSizeWarningLimit: 900, rollupOptions: { input: {
    mantis: fileURLToPath(new URL('./index.html',import.meta.url)),
    frog: fileURLToPath(new URL('./frog.html',import.meta.url)),
    crab: fileURLToPath(new URL('./crab.html',import.meta.url)),
  } } }
});
