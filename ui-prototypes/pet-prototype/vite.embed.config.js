import { defineConfig } from 'vite';

// One classic script for the settings page: tools/build-pet-html.mjs inlines it into ui-prototypes/pet.html.
export default defineConfig({
  publicDir: false,
  build: {
    lib: {
      entry: 'src/embed.js',
      name: 'PetPreview',
      formats: ['iife'],
      fileName: () => 'pet-embed.js',
    },
    minify: false,
  },
});
