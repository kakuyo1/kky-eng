import { defineConfig } from 'vite';

export default defineConfig({
  // assets/pet/* is served at /pet/*, and the manifest and PNG paths are relative to it.
  publicDir: 'assets',
});
