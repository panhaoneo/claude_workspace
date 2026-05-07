import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { VitePWA } from 'vite-plugin-pwa';

export default defineConfig({
  plugins: [
    react(),
    VitePWA({
      registerType: 'autoUpdate',
      includeAssets: ['favicon.svg'],
      manifest: {
        name: '英文阅读助手',
        short_name: 'EnReader',
        description: 'CEFR-based English reading assistant with Chinese annotations',
        theme_color: '#4A90D9',
        background_color: '#ffffff',
        display: 'standalone',
        orientation: 'portrait',
        start_url: '/',
        icons: [
          { src: '/icon-192.png', sizes: '192x192', type: 'image/png' },
          { src: '/icon-512.png', sizes: '512x512', type: 'image/png' }
        ]
      },
      workbox: {
        globPatterns: ['**/*.{js,css,html,svg,woff2}'],
        runtimeCaching: [
          {
            urlPattern: /^https:\/\/www\.gutenberg\.org\/.*/,
            handler: 'CacheFirst',
            options: { cacheName: 'gutenberg-cache', expiration: { maxAgeSeconds: 86400 * 30 } }
          }
        ]
      }
    })
  ],
  optimizeDeps: {
    include: ['epubjs', 'pdfjs-dist', 'idb']
  }
});
