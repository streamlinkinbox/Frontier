import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
export default defineConfig({
  server: { host: '0.0.0.0', allowedHosts: ['.e2b.app'], port: 5173 },
  plugins: [{
    name: 'frog-preview-entry',
    configureServer(server) {
      if(process.env.FRONTIER_SPECIMEN !== 'frog') return;
      server.middlewares.use((req,res,next)=>{
        if(req.url === '/') { res.writeHead(302,{Location:'/frog.html'});res.end(); }
        else next();
      });
    }
  }],
  build: { chunkSizeWarningLimit: 900, rollupOptions: { input: {
    mantis: fileURLToPath(new URL('./index.html',import.meta.url)),
    frog: fileURLToPath(new URL('./frog.html',import.meta.url)),
  } } }
});
