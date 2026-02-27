import { defineConfig } from 'vite'
import type { ViteDevServer } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import fs from 'fs'
import path from 'path'
import chokidar from 'chokidar'
import type { IncomingMessage, ServerResponse } from 'http'

function serialMonitorPlugin() {
  return {
    name: 'serial-monitor-plugin',
    configureServer(server: ViteDevServer) {
      server.middlewares.use('/api/serial', (req: IncomingMessage, res: ServerResponse) => {
        if (req.method !== 'GET') {
          res.statusCode = 405;
          res.end('Method Not Allowed');
          return;
        }

        const filePath = path.resolve(process.cwd(), '../build/com1.out')
        
        res.writeHead(200, {
          'Content-Type': 'text/event-stream',
          'Cache-Control': 'no-cache',
          'Connection': 'keep-alive',
        })

        // Send initial content
        if (fs.existsSync(filePath)) {
          const content = fs.readFileSync(filePath, 'utf-8')
          res.write(`data: ${JSON.stringify({ type: 'init', content })}\n\n`)
        }

        // Watch for changes
        const watcher = chokidar.watch(filePath, { persistent: true })
        
        watcher.on('change', () => {
          if (fs.existsSync(filePath) && !res.writableEnded) {
            const content = fs.readFileSync(filePath, 'utf-8')
            res.write(`data: ${JSON.stringify({ type: 'update', content })}\n\n`)
          }
        })

        req.on('close', () => {
          watcher.close()
          res.end()
        })
      })
    }
  }
}

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), tailwindcss(), serialMonitorPlugin()],
})
